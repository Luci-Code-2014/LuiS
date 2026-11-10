// kernel/kernel.c
#include <stdarg.h>
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "gfx.h"
#include "mouse.h"
#include "ext4.h"

extern char ext4_files[16][64];
extern int ext4_file_count;

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
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80); outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
static void serial_putc(char c) {
    for (volatile int i = 0; i < 100000; i++) {
        if (inb(0x3FD) & 0x20) break;
    }
    outb(0x3F8, (unsigned char)c);
}

/* ===== printk ===== */
static void printk_putc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}
static void printk_puts(const char *s) {
    if (!s) s = "(null)";
    while (*s) printk_putc(*s++);
}
static void printk_int(int v) {
    char buf[32]; int i = 0;
    if (v == 0) { printk_putc('0'); return; }
    if (v < 0) { printk_putc('-'); v = -v; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) printk_putc(buf[--i]);
}
static void printk_hex(unsigned int v) {
    char buf[16]; int i = 0;
    if (v == 0) { printk_putc('0'); return; }
    while (v > 0) {
        int d = v & 0xF;
        buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        v >>= 4;
    }
    while (i > 0) printk_putc(buf[--i]);
}

void printk(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's')      printk_puts(va_arg(ap, const char*));
            else if (*fmt == 'd') printk_int(va_arg(ap, int));
            else if (*fmt == 'x') printk_hex(va_arg(ap, unsigned int));
            else if (*fmt == 'c') printk_putc((char)va_arg(ap, int));
            fmt++;
        } else {
            printk_putc(*fmt++);
        }
    }
    va_end(ap);
}

/* ===== 图形辅助 ===== */
static void draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fill_rect(x, y, w, 2, color); fill_rect(x, y + h - 2, w, 2, color);
    fill_rect(x, y, 2, h, color); fill_rect(x + w - 2, y, 2, h, color);
}

void redraw_file_list(void) {
    draw_box(100, 100, 400, 320, 0x00556677);
    fill_rect(102, 102, 396, 18, 0x00445566);
    draw_string(110, 104, "File System (ext4)", 0x00FFFFFF);

    for (int i = 0; i < ext4_file_count; i++) {
        draw_string(120, 140 + i * 20, ext4_files[i], 0x00FFFFFF);
    }
}

/* ===== Kernel main ===== */
void kernel_main(void) {
    serial_init();
    printk("\nKernel Loaded!\n");

    pic_remap(); idt_init(); pit_init(100); mouse_init(); gfx_init();
    ext4_init();

    clear_screen(0x00101820);
    fill_rect(0, 0, 1920, 40, 0x00203040);
    draw_string(20, 12, "LuiS OS v2.4.0", 0x00FFFFFF);
    redraw_file_list();

    int old_x = mouse_get_x();
    int old_y = mouse_get_y();
    draw_mouse_cursor(old_x, old_y);

    __asm__ volatile ("sti");

    for (;;) {
        int mx = mouse_get_x();
        int my = mouse_get_y();

        if (mx != old_x || my != old_y) {
            int min_x = (old_x < mx) ? old_x : mx;
            int min_y = (old_y < my) ? old_y : my;
            int max_x = ((old_x > mx) ? old_x : mx) + 12;
            int max_y = ((old_y > my) ? old_y : my) + 19;

            fill_rect(min_x, min_y, max_x - min_x, max_y - min_y, 0x00101820);

            if (min_y < 40) {
                fill_rect(0, 0, 1920, 40, 0x00203040);
                draw_string(20, 12, "LuiS OS v2.4.0", 0x00FFFFFF);
            }

            if (min_x < 500 && max_x > 100 && min_y < 420 && max_y > 100) {
                redraw_file_list();
            }

            draw_mouse_cursor(mx, my);

            old_x = mx;
            old_y = my;
        }

        __asm__ volatile ("hlt");
    }
}