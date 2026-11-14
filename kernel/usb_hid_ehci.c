// kernel/usb_hid_ehci.c
#include "usb_hid_ehci.h"
#include "ehci.h"
#include "printk.h"

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) setup_pkt_t;

static qh_t *kbd_qh = 0;
static qtd_t *kbd_td = 0;
static uint8_t *kbd_buf = 0;
static int kbd_ready = 0;

static qh_t *ctrl_qh = 0;
static qtd_t *ctrl_td = 0;

int usb_hid_ehci_ready(void) { return kbd_ready; }

static int control_transfer(uint8_t dev_addr, setup_pkt_t *setup, void *buf) {
    for (uint32_t i = 0; i < sizeof(qh_t); i++) ((uint8_t *)ctrl_qh)[i] = 0;
    ctrl_qh->horiz_link = PTR_TERMINATE;
    ctrl_qh->info1 = (dev_addr << 8) | QH_MAXPACKET(8) | QH_EPS_HIGH | QH_DTC | QH_RL(3);
    ctrl_qh->info2 = 0x40000000;
    ctrl_qh->cur_qtd = (uint32_t)(uintptr_t)ctrl_td;

    uint8_t *td_buf = (uint8_t *)ctrl_td;
    for (uint32_t i = 0; i < sizeof(qtd_t); i++) td_buf[i] = 0;

    ctrl_td[0].next_link = (uint32_t)(uintptr_t)&ctrl_td[1];
    ctrl_td[0].alt_next = PTR_TERMINATE;
    ctrl_td[0].token = QTD_TOKEN(SETUP, 3, 0, 8);
    ctrl_td[0].buf[0] = (uint32_t)(uintptr_t)setup;

    int has_data = (setup->wLength > 0);
    int pid = (setup->bmRequestType & 0x80) ? 1 : 0;

    if (has_data) {
        ctrl_td[1].next_link = (uint32_t)(uintptr_t)&ctrl_td[2];
        ctrl_td[1].alt_next = PTR_TERMINATE;
        if (pid) {
            ctrl_td[1].token = QTD_TOKEN(IN, 3, 1, setup->wLength);
            ctrl_td[2].token = QTD_TOKEN(OUT, 3, 1, 0);
        } else {
            ctrl_td[1].token = QTD_TOKEN(OUT, 3, 1, setup->wLength);
            ctrl_td[2].token = QTD_TOKEN(IN, 3, 1, 0);
        }
        ctrl_td[1].buf[0] = (uint32_t)(uintptr_t)buf;
        ctrl_td[2].next_link = PTR_TERMINATE;
        ctrl_td[2].alt_next = PTR_TERMINATE;
    } else {
        ctrl_td[1].next_link = PTR_TERMINATE;
        ctrl_td[1].alt_next = PTR_TERMINATE;
        ctrl_td[1].token = QTD_TOKEN(IN, 3, 1, 0);
    }

    __asm__ volatile("mfence" ::: "memory");
    ehci_insert_async(ctrl_qh);

    int success = 0;
    for (int t = 0; t < 1000000; t++) {
        if (!(ctrl_td[has_data ? 2 : 1].token & QTD_STS_ACTIVE)) {
            if (!(ctrl_td[has_data ? 2 : 1].token & QTD_STS_HALT)) success = 1;
            break;
        }
        __asm__ volatile("pause");
    }

    ehci_remove_async(ctrl_qh);
    for (int t = 0; t < 100000; t++) __asm__ volatile("pause"); // 等待控制器彻底放弃该 QH

    return success ? 0 : -1;
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
                            return addr & 0x0F;
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

int usb_hid_ehci_kbd_setup(int port) {
    if (!ehci_ready()) return -1;
    kbd_ready = 0;

    if (ehci_port_reset(port) < 0) return -1;

    uint8_t dev_desc[18];
    setup_pkt_t setup = {0x80, 0x06, 0x0100, 0x0000, 18};
    if (control_transfer(0, &setup, dev_desc) < 0) return -1;

    setup = (setup_pkt_t){0x00, 0x05, 1, 0, 0};
    if (control_transfer(0, &setup, 0) < 0) return -1;

    cfg_desc = (uint8_t *)ehci_alloc(512);
    if (!cfg_desc) return -1;

    setup = (setup_pkt_t){0x80, 0x06, 0x0200, 0, 9};
    if (control_transfer(1, &setup, cfg_desc) < 0) return -1;
    
    uint16_t total_len = cfg_desc[2] | (cfg_desc[3] << 8);
    if (total_len > 512) total_len = 512;
    setup.wLength = total_len;
    if (control_transfer(1, &setup, cfg_desc) < 0) return -1;

    int ep_num = find_kbd_ep();
    if (ep_num == 0) return -1;

    setup = (setup_pkt_t){0x00, 0x09, 1, 0, 0};
    if (control_transfer(1, &setup, 0) < 0) return -1;

    kbd_qh = (qh_t *)ehci_alloc(sizeof(qh_t));
    kbd_td = (qtd_t *)ehci_alloc(sizeof(qtd_t));
    kbd_buf = (uint8_t *)ehci_alloc(8);
    if (!kbd_qh || !kbd_td || !kbd_buf) return -1;

    for (uint32_t i = 0; i < sizeof(qh_t); i++) ((uint8_t *)kbd_qh)[i] = 0;
    for (uint32_t i = 0; i < sizeof(qtd_t); i++) ((uint8_t *)kbd_td)[i] = 0;

    kbd_qh->horiz_link = PTR_TERMINATE;
    kbd_qh->info1 = (1 << 8) | QH_MAXPACKET(8) | QH_EPS_HIGH | QH_DTC | QH_RL(3);
    kbd_qh->info2 = ((uint32_t)ep_num << 8) | 0x01;
    kbd_qh->cur_qtd = (uint32_t)(uintptr_t)kbd_td;
    kbd_qh->next_qtd = PTR_TERMINATE;
    kbd_qh->alt_qtd = PTR_TERMINATE;
    kbd_qh->token = 0;

    kbd_td->next_link = PTR_TERMINATE;
    kbd_td->alt_next = PTR_TERMINATE;
    kbd_td->token = QTD_TOKEN(IN, 3, 1, 8);
    kbd_td->buf[0] = (uint32_t)(uintptr_t)kbd_buf;

    ehci_insert_periodic(kbd_qh, 8);
    ehci_start_periodic();

    kbd_ready = 1;
    printk("%[O] ehci: 键盘已就绪，可以敲击！\n");
    return 0;
}

int usb_hid_ehci_poll(char *out) {
    if (!kbd_ready) return 0;

    if (!(kbd_td->token & QTD_STS_ACTIVE)) {
        uint8_t key = kbd_buf[2];
        if (key != 0 && out) {
            *out = (char)key;
        }

        kbd_td->token = QTD_TOKEN(IN, 3, 1, 8);
        kbd_td->buf[0] = (uint32_t)(uintptr_t)kbd_buf;
        kbd_td->next_link = PTR_TERMINATE;

        kbd_qh->cur_qtd = (uint32_t)(uintptr_t)kbd_td;
        kbd_qh->token &= ~QH_T_STS_HALT;

        __asm__ volatile("mfence" ::: "memory");
        return 1;
    }

    return 0;
}