// kernel/kernel.c
#include <stdarg.h>
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "gfx.h"
#include "mouse.h"
#include "pmm.h"
#include "kheap.h"

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
static void printk_uint_base(unsigned int v, unsigned int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[32]; int i = 0;
    if (v == 0) { printk_putc('0'); return; }
    while (v > 0) { buf[i++] = digits[v % base]; v /= base; }
    while (i > 0) { printk_putc(buf[--i]); }
}
static void printk_int(int v) {
    if (v < 0) { printk_putc('-'); printk_uint_base((unsigned int)(-(v + 1)) + 1U, 10, 0); }
    else { printk_uint_base((unsigned int)v, 10, 0); }
}
static void printk_hex(unsigned int v, int upper) { printk_uint_base(v, 16, upper); }
static void printk_ptr(const void *p) {
    printk_putc('0'); printk_putc('x');
    printk_uint_base((unsigned int)(unsigned long)p, 16, 0);
}

void printk(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { printk_putc(*fmt++); continue; }
        fmt++;
        switch (*fmt) {
        case 'd': case 'i': printk_int(va_arg(ap, int)); fmt++; break;
        case 'u': printk_uint_base(va_arg(ap, unsigned int), 10, 0); fmt++; break;
        case 'x': printk_hex(va_arg(ap, unsigned int), 0); fmt++; break;
        case 'X': printk_hex(va_arg(ap, unsigned int), 1); fmt++; break;
        case 's': printk_puts(va_arg(ap, const char *)); fmt++; break;
        case 'c': printk_putc((char)va_arg(ap, int)); fmt++; break;
        case 'p': printk_ptr(va_arg(ap, void *)); fmt++; break;
        case '%': printk_putc('%'); fmt++; break;
        case '\0': printk_putc('%'); break;
        default: printk_putc('%'); printk_putc(*fmt++); break;
        }
    }
    va_end(ap);
}

/* ===== UI 绘制 ===== */
static void draw_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fill_rect(x, y, w, 2, color); fill_rect(x, y + h - 2, w, 2, color);
    fill_rect(x, y, 2, h, color); fill_rect(x + w - 2, y, 2, h, color);
}

// 只重绘指定的一片区域
void redraw_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, char *test_str) {
    // 1. 填充基础背景色
    fill_rect(x, y, w, h, 0x00101820);

    // 2. 如果这块区域碰到了顶栏
    if (y < 40) {
        fill_rect(0, 0, 1920, 40, 0x00203040); 
        draw_string(20, 12, "LuiS OS v2.4.0", 0x00FFFFFF);
        draw_string(1600, 12, "RAM: 16MB", 0x00AABBCC);
    }

    // 3. 如果碰到了窗口 1
    if (x < 500 && x + w > 100 && y < 300 && y + h > 100) {
        draw_box(100, 100, 400, 200, 0x00556677);
        fill_rect(102, 102, 396, 18, 0x00445566);
        draw_string(110, 104, "System Info", 0x00FFFFFF);
        draw_string(120, 150, "Kernel: 64-bit (Long Mode)", 0x00AAAAAA);
        draw_string(120, 180, "Memory Heap: Active", 0x0000FF00);
        draw_string(120, 210, "Test String:", 0x00AAAAAA);
        draw_string(220, 210, test_str, 0x0000FF00);
    }

    // 4. 如果碰到了窗口 2
    if (x < 900 && x + w > 600 && y < 300 && y + h > 100) {
        draw_box(600, 100, 300, 200, 0x00556677);
        fill_rect(602, 102, 296, 18, 0x00445566);
        draw_string(610, 104, "System Timer", 0x00FFFFFF);
        draw_string(620, 150, "PIT Frequency:", 0x00AAAAAA);
        draw_string(620, 170, "100 Hz", 0x0000FF00);
        draw_string(620, 200, "Ticks:", 0x00AAAAAA);
    }
}

// 初始化全屏 UI
void render_ui(char *test_str) {
    fill_rect(0, 0, 1920, 1080, 0x00101820);
    fill_rect(0, 0, 1920, 40, 0x00203040); 
    draw_string(20, 12, "LuiS OS v2.4.0", 0x00FFFFFF);
    draw_string(1600, 12, "RAM: 16MB", 0x00AABBCC);

    draw_box(100, 100, 400, 200, 0x00556677);
    fill_rect(102, 102, 396, 18, 0x00445566);
    draw_string(110, 104, "System Info", 0x00FFFFFF);
    draw_string(120, 150, "Kernel: 64-bit (Long Mode)", 0x00AAAAAA);
    draw_string(120, 180, "Memory Heap: Active", 0x0000FF00);
    draw_string(120, 210, "Test String:", 0x00AAAAAA);
    draw_string(220, 210, test_str, 0x0000FF00);

    draw_box(600, 100, 300, 200, 0x00556677);
    fill_rect(602, 102, 296, 18, 0x00445566);
    draw_string(610, 104, "System Timer", 0x00FFFFFF);
    draw_string(620, 150, "PIT Frequency:", 0x00AAAAAA);
    draw_string(620, 170, "100 Hz", 0x0000FF00);
    draw_string(620, 200, "Ticks:", 0x00AAAAAA);
}

/* ===== Kernel main ===== */
void kernel_main(void) {
    serial_init();
    printk("\nKernel Loaded!\n");

    pic_remap(); idt_init(); pit_init(100); mouse_init(); gfx_init();
    
    pmm_init(16 * 1024 * 1024);
    kheap_init();

    char *test_str = (char *)kmalloc(32);
    test_str[0] = 'S'; test_str[1] = 'y'; test_str[2] = 's'; test_str[3] = 't';
    test_str[4] = 'e'; test_str[5] = 'm'; test_str[6] = ' '; test_str[7] = 'O';
    test_str[8] = 'K'; test_str[9] = '\0';

    // 初始化整个界面
    render_ui(test_str);
    
    int old_x = mouse_get_x();
    int old_y = mouse_get_y();
    draw_mouse_cursor(old_x, old_y);

    __asm__ volatile ("sti"); 

    for (;;) {
        int mx = mouse_get_x();
        int my = mouse_get_y();
        
        if (mx != old_x || my != old_y) {
            // 算出旧鼠标的包围盒 (加上一点边距防残影)
            int old_min_x = old_x - 2;
            int old_min_y = old_y - 2;
            int old_w = 12 + 4;
            int old_h = 19 + 4;

            // 1. 把旧鼠标所在的区域重绘成原来的 UI 背景
            redraw_region(old_min_x, old_min_y, old_w, old_h, test_str);

            // 2. 在新位置画鼠标
            draw_mouse_cursor(mx, my);

            old_x = mx;
            old_y = my;
        }
        
        __asm__ volatile ("hlt");
    }
}