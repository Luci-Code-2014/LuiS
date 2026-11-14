// kernel/xhci.c
#include "xhci.h"
#include "printk.h"

static uint8_t mem_pool[512 * 1024] __attribute__((aligned(4096)));
static uint32_t mem_ptr = 0;

void *xhci_alloc(uint32_t size) {
    mem_ptr = (mem_ptr + 63) & ~63;
    if (mem_ptr + size > sizeof(mem_pool)) return 0;
    void *ret = &mem_pool[mem_ptr];
    mem_ptr += size;
    return ret;
}

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
static uint32_t max_slots = 0;
static uint32_t n_ports = 0;
static int inited = 0;

int xhci_ready(void) { return inited; }
int xhci_max_ports(void) { return (int)n_ports; }

static inline void flush_cache(void *addr, uint32_t size) {
    uint64_t start = (uint64_t)(uintptr_t)addr & ~63ULL;
    uint64_t end = ((uint64_t)(uintptr_t)addr + size + 63) & ~63ULL;
    for (uint64_t p = start; p < end; p += 64) {
        __asm__ volatile("clflush (%0)" :: "r"(p) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

trb_t *xhci_event_get(void) {
    if (!evt_ring) return 0;
    trb_t *ev = &evt_ring[evt_deq];
    if ((ev->control & 1) != evt_cycle) return 0;
    return ev;
}

void xhci_event_advance(void) {
    evt_deq = (evt_deq + 1) % 256;
    if (evt_deq == 0) evt_cycle ^= 1;
    uint64_t erdp = (uint64_t)(uintptr_t)&evt_ring[evt_deq];
    __asm__ volatile("mfence" ::: "memory");
    rt[XHCI_ERDP/4]     = (uint32_t)erdp | (1u << 3);
    rt[XHCI_ERDP/4 + 1] = (uint32_t)(erdp >> 32);
}

uint32_t xhci_wait_cmd_complete(uint32_t *slot_out) {
    for (int t = 0; t < 5000000; t++) {
        trb_t *ev = xhci_event_get();
        if (!ev) {
            __asm__ volatile("pause");
            continue;
        }
        uint32_t type = (ev->control >> 10) & 0x3F;
        if (type == TRB_CMD_COMPLETE) {
            uint32_t code = (ev->status >> 24) & 0xFF;
            if (slot_out) *slot_out = (ev->control >> 24) & 0xFF;
            xhci_event_advance();
            return code;
        }
        xhci_event_advance();
    }
    return 0;
}

uint32_t xhci_wait_transfer_event(void) {
    for (int t = 0; t < 5000000; t++) {
        trb_t *ev = xhci_event_get();
        if (!ev) {
            __asm__ volatile("pause");
            continue;
        }
        uint32_t type = (ev->control >> 10) & 0x3F;
        if (type == TRB_TRANSFER_EVENT) {
            uint32_t code = (ev->status >> 24) & 0xFF;
            xhci_event_advance();
            return code;
        }
        xhci_event_advance();
    }
    return 0;
}

static void cmd_push(trb_t t) {
    t.control = (t.control & ~1u) | cmd_cycle;
    cmd_ring[cmd_enq] = t;
    cmd_enq = (cmd_enq + 1) % 256;
    if (cmd_enq == 0) cmd_cycle ^= 1;
    
    // 强制刷新命令环, 确保硬件看到命令
    flush_cache(cmd_ring, 256 * 16);
    doorbell[0] = 0;
}

uint32_t xhci_cmd_enable_slot(uint32_t *slot_out) {
    trb_t cmd = {0};
    cmd.control = (TRB_ENABLE_SLOT << 10) | cmd_cycle;
    cmd_push(cmd);
    return xhci_wait_cmd_complete(slot_out);
}

uint32_t xhci_cmd_disable_slot(uint32_t slot) {
    trb_t cmd = {0};
    cmd.control = (TRB_DISABLE_SLOT << 10) | (slot << 24) | cmd_cycle;
    cmd_push(cmd);
    return xhci_wait_cmd_complete(0);
}

uint32_t xhci_cmd_address_device(uint32_t slot, uint8_t *dev_ctx) {
    trb_t cmd = {0};
    cmd.param_lo = (uint32_t)(uintptr_t)dev_ctx;
    cmd.param_hi = 0;
    cmd.control = (TRB_ADDRESS_DEVICE << 10) | (slot << 24) | (1u << 9) | cmd_cycle;
    cmd_push(cmd);
    return xhci_wait_cmd_complete(0);
}

uint32_t xhci_cmd_configure_ep(uint32_t slot, uint8_t *in_ctx, uint32_t dci) {
    trb_t cmd = {0};
    cmd.param_lo = (uint32_t)(uintptr_t)in_ctx;
    cmd.param_hi = 0;
    cmd.control = (TRB_CONFIGURE_EP << 10) | (slot << 24) | (1u << 9) | (dci << 16) | cmd_cycle;
    cmd_push(cmd);
    return xhci_wait_cmd_complete(0);
}

void xhci_ring_doorbell(uint32_t slot, uint32_t dci) {
    __asm__ volatile("mfence" ::: "memory");
    if (doorbell) doorbell[slot] = dci;
}

uint32_t xhci_port_status(int port) {
    if (!op || port < 0 || port >= (int)n_ports) return 0;
    return op[(XHCI_PORTSC_BASE + port*0x10)/4];
}

static void portsc_write(int p, uint32_t v) {
    op[(XHCI_PORTSC_BASE + p*0x10)/4] = v;
    __asm__ volatile("mfence" ::: "memory");
}

int xhci_port_connected(int port) {
    return (xhci_port_status(port) & PORTSC_CCS) != 0;
}

int xhci_port_reset(int port) {
    uint32_t s = xhci_port_status(port);
    if (!(s & PORTSC_CCS)) return -1;

    if (!(s & PORTSC_PP)) {
        portsc_write(port, s | PORTSC_PP);
        for (int t = 0; t < 100000; t++) {
            s = xhci_port_status(port);
            if (s & PORTSC_PP) break;
        }
    }

    portsc_write(port, (s & PORTSC_PED) | PORTSC_PR);
    for (int t = 0; t < 2000000; t++) {
        s = xhci_port_status(port);
        if (s & PORTSC_PRC) break;
    }
    if (!(s & PORTSC_PRC)) return -1;

    portsc_write(port, (s & PORTSC_PED) | PORTSC_PRC);
    for (int t = 0; t < 2000000; t++) {
        if (xhci_port_status(port) & PORTSC_PED) break;
    }
    return 0;
}

int xhci_init(volatile uint8_t *b) {
    if (inited) return 0;
    bar = b;
    mem_ptr = 0;
    cmd_enq = 0;
    cmd_cycle = 1;
    evt_deq = 0;
    evt_cycle = 1;

    uint8_t caplen = bar[XHCI_CAPLENGTH];
    op = (volatile uint32_t *)(bar + caplen);
    rt = (volatile uint32_t *)(bar + (*(volatile uint32_t *)(bar + XHCI_RTSOFF) & ~0x1Fu));
    doorbell = (uint32_t *)(bar + (*(volatile uint32_t *)(bar + XHCI_DBOFF) & ~0x3u));

    uint32_t hcsparams1 = *(volatile uint32_t *)(bar + XHCI_HCSPARAMS1);
    uint32_t hcsparams2 = *(volatile uint32_t *)(bar + XHCI_HCSPARAMS2);
    n_ports   = hcsparams1 & 0xFF;
    max_slots = (hcsparams1 >> 24) & 0xFF;
    if (n_ports > 32) n_ports = 32;

    op[XHCI_USBCMD/4] &= ~1u;
    for (int t = 0; t < 1000000; t++) if (op[XHCI_USBSTS/4] & 1u) break;
    op[XHCI_USBCMD/4] |= (1u << 1);
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBCMD/4] & (1u << 1))) break;
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBSTS/4] & (1u << 11))) break;

    dcbaa    = (uint64_t *)xhci_alloc(256 * 8);
    cmd_ring = (trb_t *)xhci_alloc(256 * 16);
    evt_ring = (trb_t *)xhci_alloc(256 * 16);
    erst     = (uint64_t *)xhci_alloc(16);
    
    for (uint32_t i = 0; i < 256 * 8; i++) ((uint8_t *)dcbaa)[i] = 0;
    for (uint32_t i = 0; i < 256 * 16; i++) ((uint8_t *)cmd_ring)[i] = 0;
    for (uint32_t i = 0; i < 256 * 16; i++) ((uint8_t *)evt_ring)[i] = 0;
    for (uint32_t i = 0; i < 16; i++) ((uint8_t *)erst)[i] = 0;

    uint32_t max_scratchpad = (hcsparams2 >> 21) & 0x1F;
    if (max_scratchpad == 0) max_scratchpad = (hcsparams2 >> 27) & 0x1F;
    if (max_scratchpad > 0) {
        uint64_t *scratchpad_array = (uint64_t *)xhci_alloc(max_scratchpad * 8);
        for (uint32_t i = 0; i < max_scratchpad; i++) {
            scratchpad_array[i] = (uint64_t)(uintptr_t)xhci_alloc(4096);
        }
        dcbaa[0] = (uint64_t)(uintptr_t)scratchpad_array;
    } else {
        dcbaa[0] = 0;
    }

    erst[0] = (uint64_t)(uintptr_t)evt_ring;
    erst[1] = 256;

    // 刷新所有初始化结构
    flush_cache(dcbaa, 256 * 8);
    flush_cache(erst, 16);

    op[XHCI_DCBAAP/4]   = (uint32_t)(uintptr_t)dcbaa;
    op[XHCI_DCBAAP/4+1] = 0;
    op[XHCI_CRCR/4]     = (uint32_t)(uintptr_t)cmd_ring | 1u;
    op[XHCI_CRCR/4+1]   = 0;

    rt[XHCI_ERSTSZ/4]   = 1;
    rt[XHCI_ERSTBA/4]   = (uint32_t)(uintptr_t)erst;
    rt[XHCI_ERSTBA/4+1] = 0;
    uint64_t erdp = (uint64_t)(uintptr_t)evt_ring;
    rt[XHCI_ERDP/4]     = (uint32_t)erdp | (1u << 3);
    rt[XHCI_ERDP/4+1]   = (uint32_t)(erdp >> 32);
    rt[XHCI_IMAN/4]     = 0; // 关闭中断, 纯轮询

    op[XHCI_CONFIG/4] = max_slots;
    __asm__ volatile("mfence" ::: "memory");
    op[XHCI_USBCMD/4] = 1;
    for (int t = 0; t < 1000000; t++) if (!(op[XHCI_USBSTS/4] & 1u)) break;

    for (int i = 0; i < (int)n_ports; i++) {
        uint32_t s = xhci_port_status(i);
        if ((s & PORTSC_CCS) && !(s & PORTSC_PP)) portsc_write(i, s | PORTSC_PP);
    }

    inited = 1;
    return 0;
}