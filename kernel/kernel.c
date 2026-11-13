// kernel/kernel.c
#include "printk.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "gfx.h"
#include "mouse.h"
#include "ext4.h"
#include "usb.h"

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

// 绘制 USB 状态面板
void draw_usb_status(void) {
    draw_box(550, 100, 450, 320, 0x00556677);
    fill_rect(552, 102, 446, 18, 0x00445566);
    draw_string(560, 104, "USB xHCI / EHCI Status", 0x00FFFFFF);

    if (usb_max_ports == 0) {
        draw_string(560, 140, "USB Controller not initialized.", 0x00FF0000);
        return;
    }

    char buf[64];
    int cx = 560;
    int cy = 140;
    
    draw_string(cx, cy, "Controller: USB Host", 0x00FFFF00); cy += 20;
    
    buf[0] = 'C'; buf[1] = 'o'; buf[2] = 'n'; buf[3] = 'n'; buf[4] = 'e'; buf[5] = 'c'; buf[6] = 't'; buf[7] = 'e'; buf[8] = 'd'; buf[9] = ':'; buf[10] = ' '; 
    buf[11] = '0' + usb_connected_count;
    buf[12] = 0;
    draw_string(cx, cy, buf, 0x00FFFFFF); cy += 30;

    for (int i = 0; i < usb_max_ports; i++) {
        uint32_t status = usb_port_status[i];

        int is_connected = (status & 1) || (status & (1 << 9));
        
        buf[0] = 'P'; buf[1] = 'o'; buf[2] = 'r'; buf[3] = 't'; buf[4] = ' '; 
        buf[5] = '0' + (i + 1); buf[6] = ':'; buf[7] = ' '; buf[8] = 0;
        draw_string(cx, cy, buf, 0x00FFFFFF);

        if (is_connected) {
            draw_string(cx + 80, cy, "[ CONNECTED ]", 0x0000FF00);  // 绿色
        } else {
            draw_string(cx + 80, cy, "[ EMPTY ]", 0x00888888); // 灰色
        }
        cy += 20;
    }
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
    
    ext4_init();
    usb_init(); 

    clear_screen(0x00101820);
    fill_rect(0, 0, 1920, 40, 0x00203040);
    draw_string(20, 12, "LuiS OS v2.4.0", 0x00FFFFFF);
    
    redraw_file_list();
    draw_usb_status();

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
            if (min_x < 1000 && max_x > 550 && min_y < 420 && max_y > 100) {
                draw_usb_status();
            }

            draw_mouse_cursor(mx, my);

            old_x = mx;
            old_y = my;
        }

        __asm__ volatile ("hlt");
    }
}