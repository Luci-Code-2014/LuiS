#ifndef USB_H
#define USB_H
#include <stdint.h>

extern int usb_max_ports;
extern int usb_connected_count;
extern uint32_t usb_port_status[32];
extern char usb_last_key;
extern int usb_last_mouse_dx;
extern int usb_last_mouse_dy;

void usb_init(void);
void usb_poll(void);

#endif