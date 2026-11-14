#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>

#define IN  1
#define OUT 0

#define EHCI_CAPLENGTH      0x00
#define EHCI_HCSPARAMS      0x04
#define EHCI_HCCPARAMS      0x08
#define EHCI_USBCMD         0x00
#define EHCI_USBSTS         0x04
#define EHCI_USBINTR        0x08
#define EHCI_FRINDEX        0x0C
#define EHCI_CTRLDSSEGMENT  0x10
#define EHCI_PERIODICLISTBASE 0x14
#define EHCI_ASYNCLISTADDR  0x18
#define EHCI_CONFIGFLAG     0x40
#define EHCI_PORTSC         0x44

#define CMD_RUN             (1u << 0)
#define CMD_HCRESET         (1u << 1)
#define CMD_PSE             (1u << 4)
#define CMD_ASE             (1u << 5)
#define CMD_ITC(x)          ((x) << 16)

#define STS_HCHALTED        (1u << 12)
#define STS_ASS             (1u << 15)
#define STS_PSS             (1u << 14)

#define PORTSC_CCS          (1u << 0)
#define PORTSC_CSC          (1u << 1)
#define PORTSC_PED          (1u << 2)
#define PORTSC_PEDC         (1u << 3)
#define PORTSC_PR           (1u << 8)
#define PORTSC_LS           (3u << 10)
#define PORTSC_PP           (1u << 12)
#define PORTSC_OWNER        (1u << 13)
#define PORTSC_RWC_BITS     (PORTSC_CSC | PORTSC_PEDC)

#define QH_HEAD             (1u << 15)
#define QH_MAXPACKET(x)     ((x) & 0x7FF)
#define QH_EPS_HIGH         (2u << 12)
#define QH_EPS_FULL         (0u << 12)
#define QH_EPS_LOW          (1u << 12)
#define QH_DTC              (1u << 14)
#define QH_RL(x)            (((x) & 0xF) << 28)

#define QH_T_STS_HALT       (1u << 6)
#define QH_T_STS_ACTIVE     (1u << 7)

#define QTD_STS_ACTIVE      (1u << 7)
#define QTD_STS_HALT        (1u << 6)
#define QTD_STS_DBE         (1u << 5)
#define QTD_STS_BABBLE      (1u << 4)
#define QTD_STS_XACTERR     (1u << 3)
#define QTD_STS_MMF         (1u << 2)
#define QTD_STS_PING        (1u << 0)
#define QTD_PID_OUT         (0u << 8)
#define QTD_PID_IN          (1u << 8)
#define QTD_PID_SETUP       (2u << 8)
#define QTD_CERR(x)         (((x) & 0x3) << 10)
#define QTD_IOC             (1u << 15)
#define QTD_LENGTH(x)       (((x) & 0x7FFF) << 16)

#define QTD_TOKEN(pid, cerr, ioc, len) \
    (QTD_PID_##pid | QTD_CERR(cerr) | (ioc ? QTD_IOC : 0) | QTD_LENGTH(len))

#define PTR_TERMINATE       (1u << 0)
#define PTR_QH              (1u << 1)

typedef struct {
    uint32_t next_link;
    uint32_t alt_next;
    uint32_t token;
    uint32_t buf[5];
    uint32_t buf_hi[5];
    uint32_t padding[4];
} __attribute__((packed, aligned(32))) qtd_t;

typedef struct {
    uint32_t horiz_link;
    uint32_t info1;
    uint32_t info2;
    uint32_t cur_qtd;
    uint32_t next_qtd;
    uint32_t alt_qtd;
    uint32_t token;
    uint32_t buf[5];
    uint32_t buf_hi[5];
} __attribute__((packed, aligned(32))) qh_t;

void *ehci_alloc(uint32_t size);
int ehci_init(volatile uint8_t *bar);
int ehci_max_ports(void);
uint32_t ehci_port_status(int port);
int ehci_port_reset(int port);
int ehci_ready(void);

void ehci_insert_async(qh_t *qh);
void ehci_remove_async(qh_t *qh);
void ehci_insert_periodic(qh_t *qh, int interval);
void ehci_start_periodic(void);

#endif