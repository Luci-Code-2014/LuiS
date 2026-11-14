// kernel/usb_hid.c
#include "usb_hid.h"
#include "xhci.h"
#include "printk.h"

typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) setup_pkt_t;

static uint32_t kbd_slot = 0;
static uint32_t kbd_dci  = 0;
static trb_t *kbd_ring = 0;
static uint32_t kbd_enq = 0;
static uint32_t kbd_cycle = 1;
static uint8_t *kbd_buf = 0;
static uint8_t *kbd_dev_ctx = 0;
static int kbd_ready = 0;

static trb_t *ctrl_ring = 0;
static uint32_t ctrl_enq = 0;
static uint32_t ctrl_cycle = 1;

int usb_hid_ready(void) { return kbd_ready; }

static int ctrl_transfer(uint32_t slot, setup_pkt_t *setup, void *buf) {
    uint32_t data_len = setup->wLength;
    int dir_in = (setup->bmRequestType & 0x80) ? 1 : 0;

    trb_t t = {0};
    t.param_lo = *(uint32_t *)setup;
    t.param_hi = *(uint32_t *)((uint8_t *)setup + 4);
    t.status = (8u << 17);
    t.control = (TRB_SETUP << 10) | (1u << 6) | (1u << 5) |
                ((data_len ? (dir_in ? 3u : 2u) : 0u) << 16) | ctrl_cycle;
    ctrl_ring[ctrl_enq] = t;
    ctrl_enq = (ctrl_enq + 1) % 256;
    if (ctrl_enq == 0) ctrl_cycle ^= 1;

    if (data_len) {
        trb_t d = {0};
        d.param_lo = (uint32_t)(uintptr_t)buf;
        d.param_hi = 0;
        d.status = (data_len << 17) | (data_len << 0);
        d.control = (TRB_DATA << 10) | ((dir_in ? 1u : 0u) << 16) | (1u << 5) | ctrl_cycle;
        ctrl_ring[ctrl_enq] = d;
        ctrl_enq = (ctrl_enq + 1) % 256;
        if (ctrl_enq == 0) ctrl_cycle ^= 1;
    }

    trb_t s = {0};
    s.control = (TRB_STATUS << 10) | (1u << 5) |
                ((data_len ? (dir_in ? 0u : 1u) : 1u) << 16) | ctrl_cycle;
    ctrl_ring[ctrl_enq] = s;
    ctrl_enq = (ctrl_enq + 1) % 256;
    if (ctrl_enq == 0) ctrl_cycle ^= 1;

    xhci_ring_doorbell(slot, 1);

    uint32_t code = xhci_wait_transfer_event();
    return code == 1 ? 0 : -1;
}

static uint8_t *cfg_desc = 0;

static int find_kbd_ep(void) {
    uint8_t *p = cfg_desc;
    uint32_t total = p[2] | (p[3] << 8);
    uint32_t off = p[0];

    while (off < total) {
        uint8_t len = p[off];
        uint8_t type = p[off + 1];
        if (len == 0) break;

        if (type == 0x04) {
            uint8_t cls = p[off + 5];
            uint8_t sub = p[off + 6];
            uint8_t pro = p[off + 7];
            if (cls == 3 && sub == 1 && pro == 1) {
                uint32_t ep_off = off + len;
                while (ep_off < total) {
                    uint8_t elen = p[ep_off];
                    uint8_t etype = p[ep_off + 1];
                    if (elen == 0) break;
                    if (etype == 0x04) break;
                    if (etype == 0x05) {
                        uint8_t addr = p[ep_off + 2];
                        uint8_t attr = p[ep_off + 3];
                        if ((attr & 0x03) == 0x03 && (addr & 0x80)) {
                            uint8_t ep_num = addr & 0x0F;
                            kbd_dci = (ep_num * 2) + 1;
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

static int is_hid_keyboard(void) {
    uint8_t *p = cfg_desc;
    uint32_t total = p[2] | (p[3] << 8);
    uint32_t off = p[0];
    while (off < total) {
        uint8_t len = p[off];
        uint8_t type = p[off + 1];
        if (len == 0) break;
        if (type == 0x04) {
            if (p[off + 5] == 3 && p[off + 6] == 1 && p[off + 7] == 1) return 1;
        }
        off += len;
    }
    return 0;
}

int usb_hid_kbd_setup(int port) {
    if (!xhci_ready()) return -1;

    kbd_slot = 0;
    kbd_dci = 0;
    kbd_enq = 0;
    kbd_cycle = 1;
    ctrl_enq = 0;
    ctrl_cycle = 1;
    kbd_ready = 0;

    if (xhci_port_reset(port) < 0) return -1;

    // 获取真实的 Port Number 和 Speed
    uint32_t portsc = xhci_port_status(port);
    uint32_t port_num = port + 1; // xHCI 端口号从 1 开始
    uint32_t speed = (portsc >> 10) & 0xF; // bit 10-13
    if (speed == 0) speed = 1; // 默认 Full Speed

    uint32_t slot = 0;
    uint32_t code = xhci_cmd_enable_slot(&slot);
    if (code != 1) {
        printk("%[E] usb_hid: Enable Slot code=%d\n", code);
        return -1;
    }
    kbd_slot = slot;
    printk("%[I] usb_hid: 端口 %d (PortNum=%d, Speed=%d) -> Slot %d\n", port, port_num, speed, slot);

    kbd_dev_ctx = (uint8_t *)xhci_alloc(2048);
    for (int i = 0; i < 2048; i++) kbd_dev_ctx[i] = 0;

    ctrl_ring = (trb_t *)xhci_alloc(256 * 16);

    // Slot Context
    uint32_t *slot_ctx = (uint32_t *)kbd_dev_ctx;
    slot_ctx[0] = (1u << 27) | (speed << 20); // DCI=1, Speed 动态填入
    slot_ctx[1] = (port_num << 16);           // Port Number (1-based)

    // EP0 Context
    uint32_t *ep0 = (uint32_t *)(kbd_dev_ctx + 32);
    ep0[1] = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (3u << 1);
    ep0[2] = (8u << 16) | (8u << 0);
    ep0[3] = (uint32_t)(uintptr_t)ctrl_ring | 1u;

    code = xhci_cmd_address_device(slot, kbd_dev_ctx);
    if (code != 1) {
        printk("%[E] usb_hid: Address Device code=%d\n", code);
        xhci_cmd_disable_slot(slot);
        return -1;
    }
    printk("%[I] usb_hid: Address Device 成功\n");

    uint8_t dev_desc[64];
    setup_pkt_t setup = {0x80, 0x06, 0x0100, 0x0000, 18};
    if (ctrl_transfer(slot, &setup, dev_desc) < 0) {
        printk("%[E] usb_hid: Get Device Descriptor 失败\n");
        xhci_cmd_disable_slot(slot);
        return -1;
    }

    cfg_desc = (uint8_t *)xhci_alloc(512);
    setup.wValue = 0x0200; setup.wLength = 9;
    if (ctrl_transfer(slot, &setup, cfg_desc) < 0) {
        xhci_cmd_disable_slot(slot);
        return -1;
    }
    uint16_t total_len = cfg_desc[2] | (cfg_desc[3] << 8);
    if (total_len > 512) total_len = 512;
    setup.wLength = total_len;
    if (ctrl_transfer(slot, &setup, cfg_desc) < 0) {
        xhci_cmd_disable_slot(slot);
        return -1;
    }

    if (!is_hid_keyboard()) {
        printk("%[I] usb_hid: 端口 %d 不是键盘，跳过\n", port);
        xhci_cmd_disable_slot(slot);
        return -1;
    }

    if (!find_kbd_ep()) {
        printk("%[W] usb_hid: 未找到端点\n");
        xhci_cmd_disable_slot(slot);
        return -1;
    }
    printk("%[I] usb_hid: 键盘 EP IN, DCI=%d\n", kbd_dci);

    setup.bmRequestType = 0x00; setup.bRequest = 0x09;
    setup.wValue = 1; setup.wIndex = 0; setup.wLength = 0;
    if (ctrl_transfer(slot, &setup, 0) < 0) {
        xhci_cmd_disable_slot(slot);
        return -1;
    }

    setup.bmRequestType = 0x21; setup.bRequest = 0x0A;
    setup.wValue = 0; setup.wIndex = 0; setup.wLength = 0;
    ctrl_transfer(slot, &setup, 0);

    uint8_t *ep_ring_mem = (uint8_t *)xhci_alloc(256 * 16);
    kbd_ring = (trb_t *)ep_ring_mem;
    kbd_buf = (uint8_t *)xhci_alloc(8);

    uint32_t *ep_ctx = (uint32_t *)(kbd_dev_ctx + 32 * (kbd_dci + 1));
    ep_ctx[1] = (1u << 1) | (1u << 3) | (1u << 4) | (3u << 1);
    ep_ctx[2] = (8u << 16) | (8u << 0);
    ep_ctx[3] = (uint32_t)(uintptr_t)ep_ring_mem | 1u;

    uint8_t *in_ctx = (uint8_t *)xhci_alloc(2048);
    for (int i = 0; i < 2048; i++) in_ctx[i] = 0;

    uint32_t *in_slot = (uint32_t *)in_ctx;
    in_slot[0] = (1u << 27) | (speed << 20); // 再次填入正确的 Speed
    in_slot[1] = (port_num << 16) | ((kbd_dci + 1) << 27); // 再次填入 PortNum

    uint32_t *in_ep = (uint32_t *)(in_ctx + 32);
    in_ep[0] = (kbd_dci << 27);
    for (int i = 1; i < 5; i++) in_ep[i] = ep_ctx[i];

    code = xhci_cmd_configure_ep(slot, in_ctx, kbd_dci);
    if (code != 1) {
        printk("%[E] usb_hid: Configure Endpoint code=%d\n", code);
        xhci_cmd_disable_slot(slot);
        return -1;
    }

    trb_t n = {0};
    n.param_lo = (uint32_t)(uintptr_t)kbd_buf;
    n.param_hi = 0;
    n.status = (8u << 17) | (8u << 0);
    n.control = (TRB_NORMAL << 10) | (1u << 5) | kbd_cycle;
    kbd_ring[kbd_enq] = n;
    kbd_enq = (kbd_enq + 1) % 256;
    if (kbd_enq == 0) kbd_cycle ^= 1;
    xhci_ring_doorbell(slot, kbd_dci);

    kbd_ready = 1;
    printk("%[O] usb_hid: 键盘就绪！\n");
    return 0;
}

int usb_hid_poll(char *out) {
    if (!kbd_ready) return 0;
    int got = 0;
    for (;;) {
        trb_t *ev = xhci_event_get();
        if (!ev) break;
        uint32_t type = (ev->control >> 10) & 0x3F;

        if (type == TRB_TRANSFER_EVENT) {
            uint32_t slot = (ev->control >> 24) & 0xFF;
            uint32_t dci  = (ev->control >> 16) & 0x1F;
            if (slot == kbd_slot && dci == kbd_dci) {
                uint8_t key = kbd_buf[2];
                if (key != 0 && out) {
                    *out = (char)key;
                    got = 1;
                }
                trb_t n = {0};
                n.param_lo = (uint32_t)(uintptr_t)kbd_buf;
                n.param_hi = 0;
                n.status = (8u << 17) | (8u << 0);
                n.control = (TRB_NORMAL << 10) | (1u << 5) | kbd_cycle;
                kbd_ring[kbd_enq] = n;
                kbd_enq = (kbd_enq + 1) % 256;
                if (kbd_enq == 0) kbd_cycle ^= 1;
                xhci_ring_doorbell(kbd_slot, kbd_dci);
            }
        }
        xhci_event_advance();
        if (got) break;
    }
    return got;
}