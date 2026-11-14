#ifndef USB_HID_EHCI_H
#define USB_HID_EHCI_H

int usb_hid_ehci_kbd_setup(int port);
int usb_hid_ehci_poll(char *out);
int usb_hid_ehci_ready(void);

#endif