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

int usb_max_ports = 0;
int usb_connected_count = 0;
uint32_t usb_port_status[32] = {0};
char usb_last_key = 0;
int usb_last_mouse_dx = 0;
int usb_last_mouse_dy = 0;

// ================= xHCI 寄存器与常量 =================
#define XHCI_USBCMD        0x00
#define XHCI_USBSTS        0x04
#define XHCI_CRCR          0x18
#define XHCI_DCBAAP        0x30
#define XHCI_CONFIG        0x38
#define XHCI_PORTSC_BASE   0x400

#define XHCI_IMAN          0x00
#define XHCI_ERSTSZ        0x08
#define XHCI_ERSTBA        0x10
#define XHCI_ERDP          0x18

#define PORTSC_CCS         (1u << 0)
#define PORTSC_PED         (1u << 1)
#define PORTSC_PR          (1u << 4)
#define PORTSC_PRC         (1u << 21)
#define PORTSC_PP          (1u << 9)

#define TRB_NORMAL         1
#define TRB_SETUP          2
#define TRB_DATA           3
#define TRB_STATUS         4
#define TRB_ENABLE_SLOT    9
#define TRB_ADDRESS_DEVICE 11
#define TRB_CONFIGURE_EP   12
#define TRB_TRANSFER_EVENT 32
#define TRB_CMD_COMPLETE   33

typedef struct { uint32_t param_lo; uint32_t param_hi; uint32_t status; uint32_t control; } __attribute__((packed)) trb_t;
typedef struct { uint8_t bmRequestType; uint8_t bRequest; uint16_t wValue; uint16_t wIndex; uint16_t wLength; } __attribute__((packed)) setup_pkt_t;

// ================= 全局状态 =================
static volatile uint8_t  *bar = 0;
static volatile uint32_t *op  = 0;
static volatile uint32_t *rt  = 0;
static uint32_t *doorbell = 0;

static trb_t *cmd_ring = 0;
static uint32_t cmd_enq = 0;
static uint32_t cmd_cycle = 1;

static trb_t *evt_ring = 0;
static uint64_t *erst = 0;
static uint32_t evt_deq = 0;
static uint32_t evt_cycle = 1;

static uint64_t *dcbaa = 0;
static uint32_t n_ports = 0;
static int inited = 0;

static uint32_t kbd_slot = 0;
static uint32_t kbd_dci = 0;
static trb_t *kbd_ring = 0;
static uint32_t kbd_enq = 0;
static uint32_t kbd_cycle = 1;
static uint8_t *kbd_buf = 0;
static int kbd_ready = 0;

// 内存池
static uint8_t usb_mem_pool[512 * 1024] __attribute__((aligned(4096)));
static uint32_t usb_mem_ptr = 0;

static void *usb_alloc(uint32_t size) {
    usb_mem_ptr = (usb_mem_ptr + 63) & ~63;
    if (usb_mem_ptr + size > sizeof(usb_mem_pool)) return 0;
    void *ret = &usb_mem_pool[usb_mem_ptr];
    usb_mem_ptr += size;
    return ret;
}

// 强制刷写缓存, 确保 DMA 可见
static inline void flush_cache(void *addr, uint32_t size) {
    uint64_t start = (uint64_t)(uintptr_t)addr & ~63ULL;
    uint64_t end = ((uint64_t)(uintptr_t)addr + size + 63) & ~63ULL;
    for (uint64_t p = start; p < end; p += 64) {
        __asm__ volatile("clflush (%0)" :: "r"(p) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

// ================= 命令环与事件环操作 =================
static void cmd_push(trb_t t) {
    t.control = (t.control & ~1u) | cmd_cycle;
    cmd_ring[cmd_enq] = t;
    cmd_enq = (cmd_enq + 1) % 256;
    if (cmd_enq == 0) cmd_cycle ^= 1;
    flush_cache(cmd_ring, 256 * 16);
    doorbell[0] = 0; // 响铃
}

// 等待事件, 并正确清除 ERDP 的 EHB 位
static uint32_t wait_event(uint32_t expected_type) {
    for (int t = 0; t < 10000000; t++) {
        trb_t *ev = &evt_ring[evt_deq];
        if ((ev->control & 1) == evt_cycle) {
            uint32_t type = (ev->control >> 10) & 0x3F;
            if (type == expected_type) {
                uint32_t code = (ev->status >> 24) & 0xFF;
                uint32_t slot = (ev->control >> 24) & 0xFF;

                evt_deq = (evt_deq + 1) % 256;
                if (evt_deq == 0) evt_cycle ^= 1;

                uint64_t erdp = (uint64_t)(uintptr_t)&evt_ring[evt_deq];
                rt[XHCI_ERDP/4]     = (uint32_t)erdp | (1u << 3); // Bit 3 = EHB, 写1清除
                rt[XHCI_ERDP/4 + 1] = (uint32_t)(erdp >> 32);

                if (expected_type == TRB_CMD_COMPLETE) return (code == 1) ? slot : 0;
                return code;
            }
            // 非预期事件, 直接跳过
            evt_deq = (evt_deq + 1) % 256;
            if (evt_deq == 0) evt_cycle ^= 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

// ================= xHCI 初始化 =================
static int xhci_init(volatile uint8_t *b) {
    if (inited) return 0;
    bar = b;
    usb_mem_ptr = 0;
    cmd_enq = 0;
    cmd_cycle = 1;
    evt_deq = 0;
    evt_cycle = 1;

    uint8_t caplen = bar[0x00];
    op = (volatile uint32_t *)(bar + caplen);
    rt = (volatile uint32_t *)(bar + (*(volatile uint32_t *)(bar + 0x18) & ~0x1Fu));
    doorbell = (uint32_t *)(bar + (*(volatile uint32_t *)(bar + 0x14) & ~0x3u));

    uint32_t hcsparams1 = *(volatile uint32_t *)(bar + 0x04);
    uint32_t hcsparams2 = *(volatile uint32_t *)(bar + 0x08);
    n_ports   = hcsparams1 & 0xFF;
    uint32_t max_slots = (hcsparams1 >> 24) & 0xFF;
    if (n_ports > 32) n_ports = 32;

    // 1. 停止并复位控制器
    op[XHCI_USBCMD/4] &= ~1u;
    for (int t = 0; t < 1000000; t++) if (op[XHCI_USBSTS/4] & 1u) break;
    op[XHCI_USBCMD/4] |= (1u << 1);
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBCMD/4] & (1u << 1))) break;
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBSTS/4] & (1u << 11))) break;

    // 2. 分配核心数据结构
    dcbaa    = (uint64_t *)usb_alloc(256 * 8);
    cmd_ring = (trb_t *)usb_alloc(256 * 16);
    evt_ring = (trb_t *)usb_alloc(256 * 16);
    erst     = (uint64_t *)usb_alloc(16);
    for (uint32_t i = 0; i < 256 * 8; i++) ((uint8_t *)dcbaa)[i] = 0;
    for (uint32_t i = 0; i < 256 * 16; i++) ((uint8_t *)cmd_ring)[i] = 0;
    for (uint32_t i = 0; i < 256 * 16; i++) ((uint8_t *)evt_ring)[i] = 0;
    for (uint32_t i = 0; i < 16; i++) ((uint8_t *)erst)[i] = 0;

    // 3. Scratchpad Buffer (若需要)
    uint32_t max_scratchpad = (hcsparams2 >> 21) & 0x1F;
    if (max_scratchpad == 0) max_scratchpad = (hcsparams2 >> 27) & 0x1F;
    if (max_scratchpad > 0) {
        uint64_t *scratchpad_array = (uint64_t *)usb_alloc(max_scratchpad * 8);
        for (uint32_t i = 0; i < max_scratchpad; i++) scratchpad_array[i] = (uint64_t)(uintptr_t)usb_alloc(4096);
        dcbaa[0] = (uint64_t)(uintptr_t)scratchpad_array;
    } else {
        dcbaa[0] = 0;
    }

    op[XHCI_DCBAAP/4]   = (uint32_t)(uintptr_t)dcbaa;
    op[XHCI_DCBAAP/4+1] = 0;
    op[XHCI_CRCR/4]     = (uint32_t)(uintptr_t)cmd_ring | 1u;
    op[XHCI_CRCR/4+1]   = 0;

    erst[0] = (uint64_t)(uintptr_t)evt_ring;
    erst[1] = 256;
    rt[XHCI_ERSTSZ/4]   = 1;
    rt[XHCI_ERSTBA/4]   = (uint32_t)(uintptr_t)erst;
    rt[XHCI_ERSTBA/4+1] = 0;

    uint64_t erdp = (uint64_t)(uintptr_t)evt_ring;
    rt[XHCI_ERDP/4]     = (uint32_t)erdp | (1u << 3);
    rt[XHCI_ERDP/4+1]   = (uint32_t)(erdp >> 32);
    rt[XHCI_IMAN/4]     = 0; // 纯轮询, 不依赖中断

    op[XHCI_CONFIG/4] = max_slots;
    flush_cache(dcbaa, 256 * 8);
    flush_cache(erst, 16);
    __asm__ volatile("mfence" ::: "memory");

    // 5. 启动控制器
    op[XHCI_USBCMD/4] = 1;
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBSTS/4] & 1u)) break;

    // 6. 端口上电
    for (int i = 0; i < (int)n_ports; i++) {
        uint32_t s = op[(XHCI_PORTSC_BASE + i*0x10)/4];
        if ((s & PORTSC_CCS) && !(s & PORTSC_PP)) op[(XHCI_PORTSC_BASE + i*0x10)/4] = s | PORTSC_PP;
    }

    inited = 1;
    return 0;
}

// ================= 端口复位与设备枚举 =================
static uint32_t portsc_read(int p) { return op[(XHCI_PORTSC_BASE + p*0x10)/4]; }
static void portsc_write(int p, uint32_t v) { op[(XHCI_PORTSC_BASE + p*0x10)/4] = v; }

static int port_reset(int p) {
    uint32_t s = portsc_read(p);
    if (!(s & PORTSC_CCS)) return -1;
    if (!(s & PORTSC_PP)) {
        portsc_write(p, s | PORTSC_PP);
        for (int t = 0; t < 100000; t++) if ((s = portsc_read(p)) & PORTSC_PP) break;
    }
    portsc_write(p, (s & PORTSC_PED) | PORTSC_PR);
    for (int t = 0; t < 2000000; t++) if ((s = portsc_read(p)) & PORTSC_PRC) break;
    if (!(s & PORTSC_PRC)) return -1;
    portsc_write(p, (s & PORTSC_PED) | PORTSC_PRC);
    for (int t = 0; t < 2000000; t++) if (portsc_read(p) & PORTSC_PED) break;
    return 0;
}

static int ctrl_transfer(uint32_t slot, setup_pkt_t *setup, void *buf) {
    static trb_t *ctrl_ring = 0;
    static uint32_t ctrl_enq = 0;
    static uint32_t ctrl_cycle = 1;
    if (!ctrl_ring) ctrl_ring = (trb_t *)usb_alloc(256 * 16);

    trb_t t = {0};
    t.param_lo = *(uint32_t *)setup;
    t.param_hi = *(uint32_t *)((uint8_t *)setup + 4);
    t.status = (8u << 17);
    t.control = (TRB_SETUP << 10) | (1u << 6) | (1u << 5) |
                ((setup->wLength ? ((setup->bmRequestType & 0x80) ? 3u : 2u) : 0u) << 16) | ctrl_cycle;
    ctrl_ring[ctrl_enq] = t;
    ctrl_enq = (ctrl_enq + 1) % 256; if (ctrl_enq == 0) ctrl_cycle ^= 1;

    if (setup->wLength) {
        trb_t d = {0};
        d.param_lo = (uint32_t)(uintptr_t)buf; d.param_hi = 0;
        d.status = (setup->wLength << 17) | (setup->wLength << 0);
        d.control = (TRB_DATA << 10) | (((setup->bmRequestType & 0x80) ? 1u : 0u) << 16) | (1u << 5) | ctrl_cycle;
        ctrl_ring[ctrl_enq] = d;
        ctrl_enq = (ctrl_enq + 1) % 256; if (ctrl_enq == 0) ctrl_cycle ^= 1;
    }

    trb_t s = {0};
    s.control = (TRB_STATUS << 10) | (1u << 5) |
                ((setup->wLength ? ((setup->bmRequestType & 0x80) ? 0u : 1u) : 1u) << 16) | ctrl_cycle;
    ctrl_ring[ctrl_enq] = s;
    ctrl_enq = (ctrl_enq + 1) % 256; if (ctrl_enq == 0) ctrl_cycle ^= 1;

    flush_cache(ctrl_ring, 256 * 16);
    doorbell[slot] = 1; // EP0 DCI = 1

    return (wait_event(TRB_TRANSFER_EVENT) == 1) ? 0 : -1;
}

static uint8_t *cfg_desc = 0;

static int find_kbd_ep(void) {
    uint8_t *p = cfg_desc;
    uint32_t total = p[2] | (p[3] << 8);
    uint32_t off = p[0];
    while (off < total) {
        uint8_t len = p[off], type = p[off + 1];
        if (len == 0) break;
        if (type == 0x04) { // 接口描述符
            uint8_t cls = p[off + 5], sub = p[off + 6], pro = p[off + 7];
            if (cls == 3 && sub == 1 && pro == 1) { // HID Boot Keyboard
                uint32_t ep_off = off + len;
                while (ep_off < total) {
                    uint8_t elen = p[ep_off], etype = p[ep_off + 1];
                    if (elen == 0) break;
                    if (etype == 0x04) break; // 下一个接口
                    if (etype == 0x05) { // 端点描述符
                        uint8_t addr = p[ep_off + 2], attr = p[ep_off + 3];
                        if ((attr & 0x03) == 0x03 && (addr & 0x80)) { // 中断 IN 端点
                            uint8_t ep_num = addr & 0x0F;
                            kbd_dci = (ep_num * 2) + 1; // IN 端点 DCI
                            return ep_num;
                        }
                    }
                    ep_off += elen;
                }
            }
        }
        off += len;
    }
    return 0;
}

static int usb_hid_kbd_setup(int port) {
    if (!inited) return -1;
    kbd_slot = 0; kbd_dci = 0; kbd_enq = 0; kbd_cycle = 1; kbd_ready = 0;

    if (port_reset(port) < 0) return -1;

    // 1. Enable Slot
    trb_t cmd = {0};
    cmd.control = (TRB_ENABLE_SLOT << 10) | cmd_cycle;
    cmd_push(cmd);
    uint32_t slot = wait_event(TRB_CMD_COMPLETE);
    if (slot == 0) return -1;
    kbd_slot = slot;

    // 2. Address Device
    uint8_t *dev_ctx = (uint8_t *)usb_alloc(2048);
    for (int i = 0; i < 2048; i++) dev_ctx[i] = 0;

    uint32_t portsc = portsc_read(port);
    uint32_t port_num = port + 1;
    uint32_t speed = (portsc >> 10) & 0xF; if (speed == 0) speed = 1;

    uint32_t *slot_ctx = (uint32_t *)dev_ctx;
    slot_ctx[0] = (1u << 27) | (speed << 20);
    slot_ctx[1] = (port_num << 16);

    extern trb_t *get_ctrl_ring(void); // 占位, 实际我们需要在 ctrl_transfer 里管理

    // 为了单文件简洁, 我们在这里重新分配一个 EP0 ring
    trb_t *ep0_ring = (trb_t *)usb_alloc(256 * 16);
    for (int i = 0; i < 256 * 16; i++) ((uint8_t *)ep0_ring)[i] = 0;

    uint32_t *ep0 = (uint32_t *)(dev_ctx + 32);
    ep0[1] = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (3u << 1);
    ep0[2] = (8u << 16) | (8u << 0);
    ep0[3] = (uint32_t)(uintptr_t)ep0_ring | 1u;

    trb_t addr_cmd = {0};
    addr_cmd.param_lo = (uint32_t)(uintptr_t)dev_ctx;
    addr_cmd.control = (TRB_ADDRESS_DEVICE << 10) | (slot << 24) | (1u << 9) | cmd_cycle;
    cmd_push(addr_cmd);
    if (wait_event(TRB_CMD_COMPLETE) == 0) return -1;

    // 3. 获取设备描述符
    uint8_t dev_desc[18];
    setup_pkt_t setup = {0x80, 0x06, 0x0100, 0x0000, 18};
    if (ctrl_transfer(slot, &setup, dev_desc) < 0) return -1;

    // 4. 获取配置描述符
    cfg_desc = (uint8_t *)usb_alloc(512);
    setup.wValue = 0x0200; setup.wLength = 9;
    if (ctrl_transfer(slot, &setup, cfg_desc) < 0) return -1;
    uint16_t total_len = cfg_desc[2] | (cfg_desc[3] << 8);
    if (total_len > 512) total_len = 512;
    setup.wLength = total_len;
    if (ctrl_transfer(slot, &setup, cfg_desc) < 0) return -1;

    // 5. 查找键盘端点并配置
    if (!find_kbd_ep()) return -1;

    setup.bmRequestType = 0x00; setup.bRequest = 0x09;
    setup.wValue = 1; setup.wIndex = 0; setup.wLength = 0;
    if (ctrl_transfer(slot, &setup, 0) < 0) return -1;

    setup.bmRequestType = 0x21; setup.bRequest = 0x0A;
    setup.wValue = 0; setup.wIndex = 0; setup.wLength = 0;
    ctrl_transfer(slot, &setup, 0); // 失败也无妨

    // 6. 配置中断端点
    kbd_ring = (trb_t *)usb_alloc(256 * 16);
    kbd_buf = (uint8_t *)usb_alloc(8);
    for (int i = 0; i < 256 * 16; i++) ((uint8_t *)kbd_ring)[i] = 0;

    uint32_t *ep_ctx = (uint32_t *)(dev_ctx + 32 * (kbd_dci + 1));
    ep_ctx[1] = (1u << 1) | (1u << 3) | (1u << 4) | (3u << 1);
    ep_ctx[2] = (8u << 16) | (8u << 0);
    ep_ctx[3] = (uint32_t)(uintptr_t)kbd_ring | 1u;

    uint8_t *in_ctx = (uint8_t *)usb_alloc(2048);
    for (int i = 0; i < 2048; i++) in_ctx[i] = 0;

    uint32_t *in_slot = (uint32_t *)in_ctx;
    in_slot[0] = (1u << 27) | (speed << 20);
    in_slot[1] = (port_num << 16) | ((kbd_dci + 1) << 27);

    uint32_t *in_ep = (uint32_t *)(in_ctx + 32);
    in_ep[0] = (kbd_dci << 27);
    for (int i = 1; i < 5; i++) in_ep[i] = ep_ctx[i];

    trb_t cfg_cmd = {0};
    cfg_cmd.param_lo = (uint32_t)(uintptr_t)in_ctx;
    cfg_cmd.control = (TRB_CONFIGURE_EP << 10) | (slot << 24) | (1u << 9) | (kbd_dci << 16) | cmd_cycle;
    cmd_push(cfg_cmd);
    if (wait_event(TRB_CMD_COMPLETE) == 0) return -1;

    // 7. 提交第一个 Normal TRB
    trb_t n = {0};
    n.param_lo = (uint32_t)(uintptr_t)kbd_buf;
    n.status = (8u << 17) | (8u << 0);
    n.control = (TRB_NORMAL << 10) | (1u << 5) | kbd_cycle;
    kbd_ring[kbd_enq] = n;
    kbd_enq = (kbd_enq + 1) % 256; if (kbd_enq == 0) kbd_cycle ^= 1;
    flush_cache(kbd_ring, 256 * 16);
    doorbell[slot] = kbd_dci;

    kbd_ready = 1;
    return 0;
}

// ================= 公开接口 =================
void usb_init(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFF) continue;
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x0C && ((cls >> 16) & 0xFF) == 0x03) {
                    uint32_t prog_if = (cls >> 8) & 0xFF;
                    uint32_t bar0 = pci_read32(bus, dev, fn, 0x10) & 0xFFFFFFF0;
                    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                    pci_write32(bus, dev, fn, 0x04, cmd | 0x0007); // 开启 Bus Master

                    if (prog_if == 0x30) {
                        printk("%[O] usb: 发现 xHCI\n");
                        if (xhci_init((volatile uint8_t *)bar0) < 0) return;

                        usb_max_ports = (int)n_ports;
                        usb_connected_count = 0;
                        for (int p = 0; p < usb_max_ports && p < 32; p++) {
                            uint32_t s = portsc_read(p);
                            usb_port_status[p] = s;
                            if (s & PORTSC_CCS) usb_connected_count++;
                        }
                        printk("%[O] usb: %d 个端口有设备\n", usb_connected_count);

                        int found = 0;
                        for (int p = 0; p < usb_max_ports; p++) {
                            if (!(portsc_read(p) & PORTSC_CCS)) continue;
                            if (usb_hid_kbd_setup(p) == 0) { found = 1; break; }
                        }
                        if (!found) printk("%[W] usb: 未找到 HID 键盘\n");
                        return;
                    }
                }
            }
        }
    }
}

void usb_poll(void) {
    if (!kbd_ready) return;

    // 轮询事件环
    for (;;) {
        trb_t *ev = &evt_ring[evt_deq];
        if ((ev->control & 1) != evt_cycle) break;

        uint32_t type = (ev->control >> 10) & 0x3F;
        if (type == TRB_TRANSFER_EVENT) {
            uint32_t slot = (ev->control >> 24) & 0xFF;
            uint32_t dci  = (ev->control >> 16) & 0x1F;
            if (slot == kbd_slot && dci == kbd_dci) {
                uint8_t key = kbd_buf[2];
                if (key != 0) {
                    usb_last_key = (char)key;
                    // 简单映射
                    char out = 0;
                    if (key >= 0x04 && key <= 0x1D) out = 'a' + (key - 0x04);
                    else if (key >= 0x1E && key <= 0x26) out = '1' + (key - 0x1E);
                    else if (key == 0x27) out = '0';
                    else if (key == 0x28) out = '\n';
                    else if (key == 0x2C) out = ' ';
                    if (out) printk("%c", out);
                }
                // 重新提交 Normal TRB
                trb_t n = {0};
                n.param_lo = (uint32_t)(uintptr_t)kbd_buf;
                n.status = (8u << 17) | (8u << 0);
                n.control = (TRB_NORMAL << 10) | (1u << 5) | kbd_cycle;
                kbd_ring[kbd_enq] = n;
                kbd_enq = (kbd_enq + 1) % 256; if (kbd_enq == 0) kbd_cycle ^= 1;
                flush_cache(kbd_ring, 256 * 16);
                doorbell[kbd_slot] = kbd_dci;
            }
        }

        // 推进事件环
        evt_deq = (evt_deq + 1) % 256;
        if (evt_deq == 0) evt_cycle ^= 1;
        uint64_t erdp = (uint64_t)(uintptr_t)&evt_ring[evt_deq];
        rt[XHCI_ERDP/4]     = (uint32_t)erdp | (1u << 3);
        rt[XHCI_ERDP/4 + 1] = (uint32_t)(erdp >> 32);

        // 一次只处理一个按键, 避免覆盖
        break;
    }
}