// kernel/kernel.c
#include "printk.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "gfx.h"
#include "mouse.h"
#include "ext4.h"

extern char ext4_files[16][64];
extern int ext4_file_count;

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
    serial_init();  // 串口初始化保留在 printk.c 中, 外部只需调用
    printk("\nKernel Loaded!\n");

    pic_remap();
    idt_init();
    pit_init(100);
    mouse_init();
    gfx_init();
    ext4_init();

    /* ---- 调试输出 ---- */
    printk("%[I] ext4: found %d file(s)\n", ext4_file_count);
    for (int i = 0; i < ext4_file_count; i++) {
        printk("%[I]   [%2d] %-20s\n", i, ext4_files[i]);
    }
    printk("%[I] cursor: %p, files: %p\n", (void*)&ext4_file_count, (void*)ext4_files);
    printk("%[I] flags: %#x %#010X %b %o\n", 0xdead, 0xbeef, 5, 8);
    printk("%[I] signed: %+d % d %05d %-6d| %ld %lld\n",
        7, 7, 42, 42, 123456789L, 1234567890123LL);
    printk("%[I] unsigned: %u %lu %llu %zu\n",
        4000000000u, 123456789UL, 18446744073709551615ULL, (size_t)255);
    printk("%[I] str: [%s] [%10s] [%-10s] [%.3s]\n", "hi", "hi", "hi", "hello");
    printk("%[I] pct: 100%%\n\n");

    /* ---- 绘制界面 ---- */
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