// kernel/kernel.c

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void serial_putc(char c) {
    for (volatile int i = 0; i < 100000; i++) {
        if (inb(0x3FD) & 0x20) break;
    }
    outb(0x3F8, (unsigned char)c);
}

static void serial_puts(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
}

void kernel_main(void) {
    serial_init();
    serial_puts("\r\n");
    serial_puts("Kernel Loaded!\r\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}