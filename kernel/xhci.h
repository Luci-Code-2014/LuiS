#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>

typedef struct { uint32_t param_lo; uint32_t param_hi; uint32_t status; uint32_t control; } __attribute__((packed)) trb_t;

#define XHCI_CAPLENGTH     0x00
#define XHCI_HCSPARAMS1    0x04
#define XHCI_HCSPARAMS2    0x08
#define XHCI_DBOFF         0x14
#define XHCI_RTSOFF        0x18

#define XHCI_USBCMD        0x00
#define XHCI_USBSTS        0x04
#define XHCI_CRCR          0x18
#define XHCI_DCBAAP        0x30
#define XHCI_CONFIG        0x38

#define XHCI_PORTSC_BASE   0x400
#define PORTSC_CCS         (1u << 0)
#define PORTSC_PED         (1u << 1)
#define PORTSC_PR          (1u << 4)
#define PORTSC_PRC         (1u << 21)
#define PORTSC_PP          (1u << 9)

#define XHCI_IMAN          0x00
#define XHCI_ERSTSZ        0x08
#define XHCI_ERSTBA        0x10
#define XHCI_ERDP          0x18

#define TRB_NORMAL         1
#define TRB_SETUP          2
#define TRB_DATA           3
#define TRB_STATUS         4
#define TRB_LINK           6
#define TRB_ENABLE_SLOT    9
#define TRB_DISABLE_SLOT   10
#define TRB_ADDRESS_DEVICE 11
#define TRB_CONFIGURE_EP   12
#define TRB_TRANSFER_EVENT 32
#define TRB_CMD_COMPLETE   33
#define TRB_PORT_STATUS    34

void *xhci_alloc(uint32_t size);

int  xhci_init(volatile uint8_t *bar);
int  xhci_max_ports(void);
int  xhci_port_connected(int port);
uint32_t xhci_port_status(int port);
int  xhci_port_reset(int port);

uint32_t xhci_cmd_enable_slot(uint32_t *slot_out);
uint32_t xhci_cmd_disable_slot(uint32_t slot);
uint32_t xhci_cmd_address_device(uint32_t slot, uint8_t *dev_ctx);
uint32_t xhci_cmd_configure_ep(uint32_t slot, uint8_t *in_ctx, uint32_t dci);

trb_t *xhci_event_get(void);
void   xhci_event_advance(void);
uint32_t xhci_wait_cmd_complete(uint32_t *slot_out);
uint32_t xhci_wait_transfer_event(void);

void xhci_ring_doorbell(uint32_t slot, uint32_t dci);

int  xhci_ready(void);

#endif