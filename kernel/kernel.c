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

/* ===== printk 内部 ===== */

static void printk_putc(char c) {
    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
}

static void printk_puts(const char *s) {
    if (!s) s = "(null)";
    while (*s) printk_putc(*s++);
}

/* 无符号整数转字符串并输出
 * base: 10 或 16; upper: 十六进制是否用大写 A-F
 */

static void printk_uint_base(unsigned int v, unsigned int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[32];
    int i = 0;

    if (v == 0) {
        printk_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = digits[v % base];
        v /= base;
    }
    while (i > 0) {
        printk_putc(buf[--i]);
    }
}

/* 有符号十进制, 处理 INT_MIN */
static void printk_int(int v) {
    if (v < 0) {
        printk_putc('-');
        unsigned int u = (unsigned int)(-(v + 1)) + 1U; // 避免 INT_MIN 溢出
        printk_uint_base(u, 10, 0);
    } else {
        printk_uint_base((unsigned int)v, 10, 0);
    }
}

/* 十六进制, 带 0x 前缀 (%p 和 %x 可选不同风格, 这里 %x 不带前缀, %p 带) */
static void printk_hex(unsigned int v, int upper) {
    printk_uint_base(v, 16, upper);
}

static void printk_ptr(const void *p) {
    printk_putc('0');
    printk_putc('x');
    printk_uint_base((unsigned int)(unsigned long)p, 16, 0);
}

/* ---------- 对外接口 ---------- */

void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            printk_putc(*fmt++);
            continue;
        }
        fmt++;  // 跳过 '%'

        switch (*fmt) {
        case 'd':
        case 'i':
            printk_int(va_arg(ap, int));
            fmt++;
            break;
        case 'u':
            printk_uint_base(va_arg(ap, unsigned int), 10, 0);
            fmt++;
            break;
        case 'x':
            printk_hex(va_arg(ap, unsigned int), 0);
            fmt++;
            break;
        case 'X':
            printk_hex(va_arg(ap, unsigned int), 1);
            fmt++;
            break;
        case 's':
            printk_puts(va_arg(ap, const char *));
            fmt++;
            break;
        case 'c':
            printk_putc((char)va_arg(ap, int));
            fmt++;
            break;
        case 'p':
            printk_ptr(va_arg(ap, void *));
            fmt++;
            break;
        case '%':
            printk_putc('%');
            fmt++;
            break;
        case '\0':
            /* 格式串以 '%' 结尾, 原样输出 '%' 并结束 */
            printk_putc('%');
            break;
        default:
            /* 未知格式符: 原样输出 '%' 和该字符 */
            printk_putc('%');
            printk_putc(*fmt++);
            break;
        }
    }

    va_end(ap);
}

/* ===== Kernel main ===== */

void kernel_main(void) {
    serial_init();

    printk("\n");
    printk("Kernel Loaded!\n");
    printk("-----------------\n");

    /* 十进制有符号 */
    printk("[d] 42          = %d\n", 42);
    printk("[d] -123        = %d\n", -123);
    printk("[d] INT_MIN     = %d\n", -2147483647 - 1);
    printk("[d] two args    = %d and %d\n", 100, 200);

    /* 十进制无符号 */
    printk("[u] 0           = %u\n", 0u);
    printk("[u] 4294967295  = %u\n", 4294967295u);

    /* 十六进制 */
    printk("[x] 0xdeadbeef  = %x\n", 0xdeadbeefu);
    printk("[x] 0xCAFE      = %x\n", 0xCAFEu);
    printk("[X] 0xCAFE      = %X\n", 0xCAFEu);

    /* 字符串 */
    printk("[s] hello       = %s\n", "hello");
    printk("[s] null        = %s\n", (const char *)0);

    /* 字符 */
    printk("[c] 'A'         = %c\n", 'A');

    /* 指针 */
    int x = 0;
    printk("[p] &x          = %p\n", (void *)&x);
    printk("[p] null        = %p\n", (void *)0);

    /* 百分号 */
    printk("[%%] literal     = 100%%\n");

    /* 混合 */
    printk("[mix] %s=%d (%x) at %p\n", "val", 255, 255, (void *)&x);

    printk("-----------------\n");
    printk("- All format tests done.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}