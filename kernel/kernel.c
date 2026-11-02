// kernel/kernel.c
#include <stdarg.h>

/* ===== 端口 I/O ===== */

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

/* ===== Serial Output ===== */

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

static void serial_puts(const char *s) __attribute__((unused));
static void serial_puts(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
}

/* ===== printk 内部 ===== */

static void printk_putc(char c) {
    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
}

static void printk_puts(const char *s) __attribute__((unused));
static void printk_puts(const char *s) {
    while (*s) printk_putc(*s++);
}

static void printk_int(int v) {
    char buf[12];
    int i = 0;
    unsigned int u;

    if (v < 0) {
        printk_putc('-');
        u = (unsigned int)(-(v + 1)) + 1U;
    } else {
        u = (unsigned int)v;
    }

    if (u == 0) {
        printk_putc('0');
        return;
    }
    while (u > 0) {
        buf[i++] = '0' + (u % 10);
        u /= 10;
    }
    while (i > 0) {
        printk_putc(buf[--i]);
    }
}

/* ===== printk 外部接口 ===== */

void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1) == 'd') {
            printk_int(va_arg(ap, int));
            fmt += 2;
        } else if (*fmt == '%' && *(fmt + 1) == '%') {
            printk_putc('%');
            fmt += 2;
        } else {
            printk_putc(*fmt++);
        }
    }

    va_end(ap);
}

/* ===== Kernel main ===== */

void kernel_main(void) {
    serial_init();

    printk("\n");
    printk("Kernel Loaded!\n");
    printk("- Test number:\n\n");
    printk("[1] Raw number: 1+1\n");
    printk("[1] Answer: %d\n", 1 + 1);
    printk("- Test success!\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}