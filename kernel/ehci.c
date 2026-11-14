// kernel/ehci.c
#include "ehci.h"
#include "printk.h"

static uint8_t mem_pool[256 * 1024] __attribute__((aligned(4096)));
static uint32_t mem_ptr = 0;

void *ehci_alloc(uint32_t size) {
    mem_ptr = (mem_ptr + 31) & ~31; // 必须 32 字节对齐
    if (mem_ptr + size > sizeof(mem_pool)) return 0;
    void *ret = &mem_pool[mem_ptr];
    mem_ptr += size;
    return ret;
}

static volatile uint8_t *bar = 0;
static volatile uint32_t *op = 0;
static uint32_t n_ports = 0;
static int inited = 0;

static uint32_t *periodic_list = 0;
static qh_t *async_head = 0;

int ehci_ready(void) { return inited; }
int ehci_max_ports(void) { return (int)n_ports; }

uint32_t ehci_port_status(int port) {
    if (!op || port < 0 || port >= (int)n_ports) return 0;
    return op[(EHCI_PORTSC + port*4)/4];
}

static void portsc_write(int p, uint32_t v) {
    op[(EHCI_PORTSC + p*4)/4] = v;
    __asm__ volatile("mfence" ::: "memory");
}

int ehci_port_reset(int port) {
    uint32_t s = ehci_port_status(port);
    if (!(s & PORTSC_CCS)) return -1;

    if (!(s & PORTSC_PP)) {
        portsc_write(port, s | PORTSC_PP);
        for (int t = 0; t < 100000; t++) {
            s = ehci_port_status(port);
            if (s & PORTSC_PP) break;
        }
    }

    portsc_write(port, (s & PORTSC_PP) | PORTSC_PR);
    for (volatile int i = 0; i < 1000000; i++) __asm__ volatile("pause");
    
    s = ehci_port_status(port);
    portsc_write(port, (s & ~PORTSC_PR) | PORTSC_PP);
    for (volatile int i = 0; i < 1000000; i++) __asm__ volatile("pause");

    s = ehci_port_status(port);
    portsc_write(port, s | PORTSC_CSC | PORTSC_PEDC);
    return 0;
}

void ehci_insert_async(qh_t *qh) {
    __asm__ volatile("mfence" ::: "memory");
    qh->horiz_link = async_head->horiz_link;
    async_head->horiz_link = (uint32_t)(uintptr_t)qh | PTR_QH;
    __asm__ volatile("mfence" ::: "memory");
}

void ehci_remove_async(qh_t *qh) {
    qh->horiz_link = PTR_TERMINATE;
    __asm__ volatile("mfence" ::: "memory");
}

void ehci_insert_periodic(qh_t *qh, int interval) {
    uint32_t phys_qh = (uint32_t)(uintptr_t)qh | PTR_QH;
    for (int i = 0; i < 1024; i += interval) {
        qh->horiz_link = periodic_list[i];
        periodic_list[i] = phys_qh;
    }
    __asm__ volatile("mfence" ::: "memory");
}

void ehci_start_periodic(void) {
    op[EHCI_USBCMD/4] |= CMD_PSE;
    __asm__ volatile("mfence" ::: "memory");
}

int ehci_init(volatile uint8_t *b) {
    if (inited) return 0;
    bar = b;
    mem_ptr = 0;

    uint8_t caplen = bar[EHCI_CAPLENGTH];
    op = (volatile uint32_t *)(bar + caplen);

    uint32_t hcsparams = *(volatile uint32_t *)(bar + EHCI_HCSPARAMS);
    n_ports = hcsparams & 0x0F;

    op[EHCI_USBCMD/4] = 0;
    for (int t = 0; t < 1000000; t++)
        if (op[EHCI_USBSTS/4] & STS_HCHALTED) break;

    op[EHCI_USBCMD/4] |= CMD_HCRESET;
    for (int t = 0; t < 1000000; t++)
        if (!(op[EHCI_USBCMD/4] & CMD_HCRESET)) break;

    periodic_list = (uint32_t *)ehci_alloc(1024 * sizeof(uint32_t));
    for (int i = 0; i < 1024; i++) {
        periodic_list[i] = PTR_TERMINATE;
    }

    async_head = (qh_t *)ehci_alloc(sizeof(qh_t));
    qtd_t *dummy_td = (qtd_t *)ehci_alloc(sizeof(qtd_t));

    for (uint32_t i = 0; i < sizeof(qh_t); i++) ((uint8_t *)async_head)[i] = 0;
    for (uint32_t i = 0; i < sizeof(qtd_t); i++) ((uint8_t *)dummy_td)[i] = 0;

    async_head->horiz_link = (uint32_t)(uintptr_t)async_head | PTR_QH;
    async_head->info1 = QH_HEAD | QH_EPS_HIGH;
    async_head->cur_qtd = (uint32_t)(uintptr_t)dummy_td;
    async_head->next_qtd = PTR_TERMINATE;
    async_head->alt_qtd = PTR_TERMINATE;
    async_head->token = QH_T_STS_HALT;

    dummy_td->next_link = PTR_TERMINATE;
    dummy_td->alt_next = PTR_TERMINATE;
    dummy_td->token = QTD_STS_HALT;

    op[EHCI_CTRLDSSEGMENT/4] = 0;
    op[EHCI_PERIODICLISTBASE/4] = (uint32_t)(uintptr_t)periodic_list;
    op[EHCI_ASYNCLISTADDR/4] = (uint32_t)(uintptr_t)async_head;
    
    op[EHCI_USBCMD/4] = (8 << 16);
    op[EHCI_CONFIGFLAG/4] = 1;

    op[EHCI_USBCMD/4] = (8 << 16) | CMD_RUN | CMD_ASE;
    for (int t = 0; t < 1000000; t++) {
        if (!(op[EHCI_USBSTS/4] & STS_HCHALTED) && (op[EHCI_USBSTS/4] & STS_ASS)) break;
    }

    for (int i = 0; i < (int)n_ports; i++) {
        uint32_t s = ehci_port_status(i);
        if (!(s & PORTSC_PP)) portsc_write(i, s | PORTSC_PP);
    }

    inited = 1;
    return 0;
}