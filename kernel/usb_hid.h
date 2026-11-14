#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include "xhci.h"

int usb_hid_kbd_setup(int port);
int usb_hid_poll(char *out);
int usb_hid_ready(void);

#endif