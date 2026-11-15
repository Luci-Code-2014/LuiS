// kernel/usb.c
#include "usb.h"
#include "printk.h"

static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

/* ==================== 全局状态 ==================== */
int usb_max_ports = 0;
int usb_connected_count = 0;
uint32_t usb_port_status[32] = {0};
char usb_last_key = 0;
int usb_last_mouse_dx = 0;
int usb_last_mouse_dy = 0;

/* ==================== 内存池 ==================== */
static uint8_t usb_mem_pool[128 * 1024] __attribute__((aligned(4096)));
static uint32_t usb_mem_ptr = 0;
static void *usb_alloc(uint32_t size) {
    if (usb_mem_ptr + size > sizeof(usb_mem_pool)) return 0;
    void *ret = &usb_mem_pool[usb_mem_ptr];
    usb_mem_ptr += size;
    usb_mem_ptr = (usb_mem_ptr + 63) & ~63;
    return ret;
}

/* ==================== 描述符结构 ==================== */
typedef struct { uint8_t bLength; uint8_t bDescriptorType; } __attribute__((packed)) usb_desc_hdr_t;
typedef struct {
    uint8_t bLength; uint8_t bDescriptorType; uint16_t wTotalLength;
    uint8_t bNumInterfaces; uint8_t bConfigurationValue;
    uint8_t iConfiguration; uint8_t bmAttributes; uint8_t bMaxPower;
} __attribute__((packed)) usb_config_desc_t;
typedef struct {
    uint8_t bLength; uint8_t bDescriptorType; uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting; uint8_t bNumEndpoints; uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass; uint8_t bInterfaceProtocol;
} __attribute__((packed)) usb_iface_desc_t;
typedef struct {
    uint8_t bLength; uint8_t bDescriptorType; uint8_t bEndpointAddress;
    uint8_t bmAttributes; uint16_t wMaxPacketSize; uint8_t bInterval;
} __attribute__((packed)) usb_ep_desc_t;

/* ==================== 寄存器偏移 ==================== */
#define XHCI_CAPLENGTH     0x00
#define XHCI_HCSPARAMS1    0x04
#define XHCI_DBOFF         0x14
#define XHCI_RTSOFF        0x18
#define XHCI_USBCMD        0x00
#define XHCI_USBSTS        0x04
#define XHCI_CRCR          0x18
#define XHCI_DCBAAP        0x30
#define XHCI_CONFIG        0x38

typedef struct { uint32_t param_lo; uint32_t param_hi; uint32_t status; uint32_t control; } __attribute__((packed)) trb_t;
typedef struct { uint64_t base; uint32_t size; uint32_t rsv; } __attribute__((packed)) erst_entry_t;

static volatile uint8_t  *xhci_cap = 0;
static volatile uint32_t *xhci_op  = 0;
static volatile uint32_t *xhci_rt  = 0;
static uint32_t *xhci_doorbell = 0;

static trb_t *cmd_ring = 0;
static uint32_t cmd_ring_idx = 0;
static uint32_t cmd_ring_cycle = 1;

static trb_t *ep0_ring = 0;
static trb_t *ep_in_ring = 0;
static uint64_t *dcbaa = 0;
static uint32_t slot_id = 0;
static uint32_t xhci_hid_dci = 0;
static uint8_t hid_ep_maxpkt = 8;
static uint8_t is_mouse = 0;

static trb_t *event_ring = 0;
static erst_entry_t *erst = 0;
static uint32_t event_idx = 0;

static uint8_t input_ctx[4096] __attribute__((aligned(4096)));
static uint8_t dev_ctx[4096] __attribute__((aligned(4096)));
static uint8_t keyboard_report[8] __attribute__((aligned(64)));

static void xhci_ring_doorbell(uint32_t slot, uint32_t target) {
    xhci_doorbell[slot] = target;
}

// 发送命令并等待事件
static int xhci_send_command(uint32_t trb_type, uint32_t param_lo, uint32_t param_hi, uint32_t status, uint32_t control_flags, const char *tag) {
    trb_t *trb = &cmd_ring[cmd_ring_idx];
    trb->param_lo = param_lo;
    trb->param_hi = param_hi;
    trb->status = status;
    trb->control = (trb_type << 10) | control_flags | cmd_ring_cycle;

    cmd_ring_idx = (cmd_ring_idx + 1) % 256;
    xhci_ring_doorbell(0, 0);

    volatile uint32_t *intr0 = (volatile uint32_t *)((uint8_t *)xhci_rt + 0x20);
    for (int i = 0; i < 1000000; i++) {
        trb_t *ev = &event_ring[event_idx];
        if (ev->control & 1) {
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == 33) { // Command Completion Event
                // 检查返回状态
                uint32_t comp_code = (ev->status >> 24) & 0xFF;
                if (comp_code != 1) { // 1 = Success
                    printk("%[E] usb: [%s] 命令失败, 代码=%d\n", tag, comp_code);
                }
                ev->control &= ~1;
                event_idx = (event_idx + 1) % 64;
                intr0[6] = (uint32_t)(uintptr_t)&event_ring[event_idx] | (1 << 3);
                return comp_code;
            }
            ev->control &= ~1;
            event_idx = (event_idx + 1) % 64;
            intr0[6] = (uint32_t)(uintptr_t)&event_ring[event_idx] | (1 << 3);
            return 0;
        }
        __asm__ volatile("pause");
    }
    printk("%[E] usb: [%s] 超时\n", tag);
    return -1;
}

// 标准控制传输
static void xhci_control_transfer(uint32_t slot, uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, void *data) {
    uint64_t ptr = (uint64_t)(uintptr_t)data;
    uint32_t trb_idx = 0;

    trb_t *trb = &ep0_ring[trb_idx++];
    trb->param_lo = (req_type) | (req << 8) | ((uint32_t)val << 16) | ((uint32_t)idx << 24);
    trb->param_hi = (idx >> 8) | ((uint32_t)len << 16);
    trb->status = 8;
    uint32_t trt = 0;
    if (len > 0) trt = (req_type & 0x80) ? 3 : 2;
    trb->control = (2 << 10) | (trt << 16) | (1 << 6) | (1 << 4) | 1;

    if (len > 0) {
        trb = &ep0_ring[trb_idx++];
        trb->param_lo = (uint32_t)ptr;
        trb->param_hi = ptr >> 32;
        trb->status = len;
        trb->control = (3 << 10) | ((req_type & 0x80) ? (1 << 16) : 0) | (1 << 4) | 1;
    }

    trb = &ep0_ring[trb_idx++];
    trb->param_lo = 0;
    trb->param_hi = 0;
    trb->status = 0;
    trb->control = (4 << 10) | ((req_type & 0x80) ? 0 : (1 << 16)) | (1 << 5) | 1;

    xhci_ring_doorbell(slot, 1);

    // 等待传输完成
    volatile uint32_t *intr0 = (volatile uint32_t *)((uint8_t *)xhci_rt + 0x20);
    for (int i = 0; i < 1000000; i++) {
        trb_t *ev = &event_ring[event_idx];
        if (ev->control & 1) {
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == 32) { // Transfer Event
                uint32_t comp_code = (ev->status >> 24) & 0xFF;
                if (comp_code != 1) {
                    printk("%[E] usb: 控制传输失败, 代码=%d\n", comp_code);
                }
                ev->control &= ~1;
                event_idx = (event_idx + 1) % 64;
                intr0[6] = (uint32_t)(uintptr_t)&event_ring[event_idx] | (1 << 3);
                return;
            }
            ev->control &= ~1;
            event_idx = (event_idx + 1) % 64;
            intr0[6] = (uint32_t)(uintptr_t)&event_ring[event_idx] | (1 << 3);
            return;
        }
        __asm__ volatile("pause");
    }
    printk("%[E] usb: 控制传输超时\n");
}

static void xhci_init(volatile uint8_t *bar) {
    xhci_cap = bar;
    xhci_op = (volatile uint32_t *)(bar + bar[0]);
    uint32_t rt_off = *(volatile uint32_t *)(bar + XHCI_RTSOFF) & ~0x1F;
    xhci_rt = (volatile uint32_t *)(bar + rt_off);
    uint32_t db_off = *(volatile uint32_t *)(bar + XHCI_DBOFF) & ~0x3;
    xhci_doorbell = (uint32_t *)(bar + db_off);

    uint32_t hcsparams1 = *(volatile uint32_t *)(bar + XHCI_HCSPARAMS1);
    usb_max_ports = hcsparams1 & 0xFF;
    uint32_t max_slots = (hcsparams1 >> 24) & 0xFF;

    xhci_op[XHCI_USBCMD / 4] = (1 << 1);
    while (xhci_op[XHCI_USBCMD / 4] & (1 << 1));
    printk("%[O] usb: 控制器复位完成\n");

    dcbaa = (uint64_t *)usb_alloc(256 * 8);
    cmd_ring = (trb_t *)usb_alloc(256 * 16);
    ep0_ring = (trb_t *)usb_alloc(256 * 16);
    ep_in_ring = (trb_t *)usb_alloc(256 * 16);
    event_ring = (trb_t *)usb_alloc(256 * 16);
    erst = (erst_entry_t *)usb_alloc(sizeof(erst_entry_t));

    erst[0].base = (uint64_t)(uintptr_t)event_ring;
    erst[0].size = 64;
    volatile uint32_t *intr0 = (volatile uint32_t *)((uint8_t *)xhci_rt + 0x20);
    intr0[2] = 1;
    intr0[4] = (uint32_t)(uintptr_t)erst;
    intr0[5] = 0;
    intr0[6] = (uint32_t)(uintptr_t)event_ring | (1 << 3);
    intr0[7] = 0;
    printk("%[O] usb: Event Ring 初始化完成\n");

    xhci_op[XHCI_DCBAAP / 4] = (uint32_t)(uintptr_t)dcbaa;
    xhci_op[XHCI_CRCR / 4] = (uint32_t)(uintptr_t)cmd_ring | 1;
    xhci_op[XHCI_CONFIG / 4] = max_slots;
    xhci_op[XHCI_USBCMD / 4] = 1;

    volatile uint32_t *portsc = (volatile uint32_t *)((uint8_t *)xhci_op + 0x400);
    usb_connected_count = 0;
    uint32_t connected_port = 0;
    for (int i = 0; i < usb_max_ports; i++) {
        uint32_t status = portsc[i];
        if (status & 1) {
            printk("%[I] usb: 端口 %d 发现设备\n", i + 1);
            usb_connected_count++;
            connected_port = i;
            portsc[i] = (status & ~(0xE01C)) | (1 << 4);
            for(int t=0; t<1000000; t++) {
                if (!(portsc[i] & (1 << 4))) break;
            }
            printk("%[O] usb: 端口 %d 复位完成\n", i + 1);
            break;
        }
    }

    if (usb_connected_count == 0) return;

    // 发送 Enable Slot (Type = 11)
    xhci_send_command(11, 0, 0, 0, 0, "Enable Slot");

    input_ctx[0] = 1; input_ctx[1] = 0;
    uint32_t *ep0_ctx = (uint32_t *)(input_ctx + 0x40);
    ep0_ctx[1] = (3 << 1);
    ep0_ctx[2] = (uint32_t)(uintptr_t)ep0_ring | 1;
    ep0_ctx[3] = 8;
    dcbaa[slot_id] = (uint64_t)(uintptr_t)dev_ctx;

    xhci_send_command(12, (uint32_t)(uintptr_t)input_ctx, (uint32_t)((uintptr_t)input_ctx >> 32), 0, (slot_id << 24), "Address Device");

    uint8_t config_data[256];
    for(int i = 0; i < 256; i++) config_data[i] = 0;

    xhci_control_transfer(slot_id, 0x80, 0x06, 0x0200, 0x0000, 9, config_data);
    uint16_t total_len = *(uint16_t*)(config_data + 2);
    printk("%[I] usb: 描述符总长度 = %d\n", total_len);
    if (total_len == 0 || total_len > 256) total_len = 256;

    xhci_control_transfer(slot_id, 0x80, 0x06, 0x0200, 0x0000, total_len, config_data);

    printk("%[I] usb: 描述符数据头: %02x %02x %02x %02x\n", config_data[0], config_data[1], config_data[2], config_data[3]);

    uint8_t *ptr = config_data;
    uint8_t *end = config_data + total_len;
    uint8_t config_value = 1;

    while (ptr < end) {
        usb_desc_hdr_t *hdr = (usb_desc_hdr_t *)ptr;
        if (hdr->bLength == 0 || hdr->bLength > 64) break;

        if (hdr->bDescriptorType == 0x02) config_value = ((usb_config_desc_t *)ptr)->bConfigurationValue;
        else if (hdr->bDescriptorType == 0x04) {
            usb_iface_desc_t *iface = (usb_iface_desc_t *)ptr;
            if (iface->bInterfaceClass == 3) is_mouse = (iface->bInterfaceProtocol == 2);
        } else if (hdr->bDescriptorType == 0x05) {
            usb_ep_desc_t *ep = (usb_ep_desc_t *)ptr;
            if (ep->bEndpointAddress & 0x80) {
                hid_ep_maxpkt = ep->wMaxPacketSize & 0x7FF;
                xhci_hid_dci = ((ep->bEndpointAddress & 0x0F) * 2) + 1;
                printk("%[O] usb: 找到端点 DCI=%d, MaxPacket=%d\n", xhci_hid_dci, hid_ep_maxpkt);
            }
        }
        ptr += hdr->bLength;
    }

    if (xhci_hid_dci == 0) {
        printk("%[E] usb: 未找到端点\n");
        return;
    }

    xhci_control_transfer(slot_id, 0x00, 0x09, config_value, 0x0000, 0, 0);

    uint32_t *ep_in_ctx = (uint32_t *)(input_ctx + (0x20 * xhci_hid_dci));
    ep_in_ctx[1] = (7 << 1) | 3;
    ep_in_ctx[2] = (uint32_t)(uintptr_t)ep_in_ring | 1;
    ep_in_ctx[3] = hid_ep_maxpkt;

    xhci_send_command(13, (uint32_t)(uintptr_t)input_ctx, (uint32_t)((uintptr_t)input_ctx >> 32), 0, (slot_id << 24), "Configure Endpoint");

    printk("%[O] usb: USB 设备枚举全部完成！\n");
}

void usb_init(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF) continue;
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x0C && ((cls >> 16) & 0xFF) == 0x03) {
                    uint32_t prog_if = (cls >> 8) & 0xFF;
                    if (prog_if == 0x30) {
                        uint32_t bar0 = pci_read32(bus, dev, fn, 0x10) & 0xFFFFFFF0;
                        uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                        pci_write32(bus, dev, fn, 0x04, cmd | 0x0006);
                        xhci_init((volatile uint8_t *)(uintptr_t)bar0);
                        return;
                    }
                }
            }
        }
    }
}

void usb_poll(void) {
    if (slot_id == 0 || xhci_hid_dci == 0) return;
    for(int i=0; i<8; i++) keyboard_report[i] = 0;

    trb_t *trb = &ep_in_ring[0];
    trb->param_lo = (uint32_t)(uintptr_t)keyboard_report;
    trb->param_hi = 0;
    trb->status = hid_ep_maxpkt;
    trb->control = (1 << 10) | (1 << 5) | 1;

    xhci_ring_doorbell(slot_id, xhci_hid_dci);

    // 等待中断数据到达
    volatile uint32_t *intr0 = (volatile uint32_t *)((uint8_t *)xhci_rt + 0x20);
    for (int i = 0; i < 100000; i++) {
        trb_t *ev = &event_ring[event_idx];
        if (ev->control & 1) {
            ev->control &= ~1;
            event_idx = (event_idx + 1) % 64;
            intr0[6] = (uint32_t)(uintptr_t)&event_ring[event_idx] | (1 << 3);
            break;
        }
        __asm__ volatile("pause");
    }

    if (is_mouse) {
        int8_t dx = (int8_t)keyboard_report[1];
        int8_t dy = (int8_t)keyboard_report[2];
        if (dx != 0 || dy != 0) {
            usb_last_mouse_dx = dx;
            usb_last_mouse_dy = dy;
        }
    } else {
        uint8_t keycode = keyboard_report[2];
        if (keycode >= 4 && keycode <= 29) {
            usb_last_key = 'a' + (keycode - 4);
            printk("%c", usb_last_key);
        }
    }
}
