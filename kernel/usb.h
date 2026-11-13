#ifndef USB_H
#define USB_H
#include <stdint.h>

extern int usb_max_ports;
extern int usb_connected_count;
extern uint32_t usb_port_status[32];

void usb_init(void);
#endif