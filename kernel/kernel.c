// kernel/kernel.c
#include <stdarg.h>
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "gfx.h"
#include "mouse.h"

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

/* 非阻塞读一个字符, 没有数据返回 0 */
__attribute__((unused)) static char serial_getc_nonblock(void) {
    if (inb(0x3FD) & 0x01) {      /* LSR bit0: Data Ready */
        return (char)inb(0x3F8);
    }
    return 0;
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
    printk("\nKernel Loaded!\n");

    pic_remap();
    idt_init();
    pit_init(100);
    mouse_init();
    gfx_init();
    
    // 画基础背景和文字
    clear_screen(0x00102030);
    draw_string(50, 50, "Hello, LuiS OS!", 0x00FFFFFF);
    draw_string(50, 100, "Mouse is working perfectly!", 0x0000FF00);
    draw_string(50, 150, "Try moving the mouse around.", 0x00FF0000);

    // 如果没触发 panic 才会走到下面
    __asm__ volatile ("sti"); 

    int old_x = mouse_get_x();
    int old_y = mouse_get_y();
    draw_mouse_cursor(old_x, old_y);

    for (;;) {
        int mx = mouse_get_x();
        int my = mouse_get_y();

        if (mx != old_x || my != old_y) {
            int min_x = (old_x < mx) ? old_x : mx;
            int min_y = (old_y < my) ? old_y : my;
            int max_x = ((old_x > mx) ? old_x : mx) + 12;
            int max_y = ((old_y > my) ? old_y : my) + 19;
            
            if (min_x < 0) min_x = 0;
            if (min_y < 0) min_y = 0;
            if (max_x > 1920) max_x = 1920;
            if (max_y > 1080) max_y = 1080;

            fill_rect(min_x, min_y, max_x - min_x, max_y - min_y, 0x00102030);
            
            if (min_y < 70 && max_y > 50) draw_string(50, 50, "Hello, LuiS OS!", 0x00FFFFFF);
            if (min_y < 120 && max_y > 100) draw_string(50, 100, "Mouse is working perfectly!", 0x0000FF00);
            if (min_y < 170 && max_y > 150) draw_string(50, 150, "Try moving the mouse around.", 0x00FF0000);
            
            draw_mouse_cursor(mx, my);
            old_x = mx;
            old_y = my;
        }
        
        __asm__ volatile ("hlt");
    }
}