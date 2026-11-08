// kernel/panic.c
#include "panic.h"
#include "gfx.h"
#include "mouse.h"

void printk(const char *fmt, ...);

void panic(const char *msg, unsigned long err, unsigned long rip, unsigned long rsp) {
    // 1. 终端串口输出
    printk("\n\n!!! KERNEL PANIC !!!\n");
    printk("Exception: %s\n", msg);
    printk("Err Code : %p\n", (void *)err);
    printk("RIP      : %p\n", (void *)rip);
    printk("RSP      : %p\n", (void *)rsp);

    // 2. 屏幕变红
    clear_screen(0x00550000);
    draw_string(100, 100, "================ KERNEL PANIC ================", 0x00FFFFFF);
    draw_string(100, 150, "A fatal exception has occurred.", 0x00FFFFFF);
    
    draw_string(100, 200, "Reason:", 0x00FFFF00);
    draw_string(200, 200, msg, 0x00FFFFFF);

    draw_string(100, 250, "RIP (Code):", 0x00FFFF00);
    char buf[32];
    const char *hex = "0123456789ABCDEF";
    unsigned long r = rip;
    buf[18] = '\0';
    for (int i = 17; i >= 0; i--) {
        buf[i] = hex[r & 0xF];
        r >>= 4;
    }
    draw_string(250, 250, "0x", 0x00FFFFFF);
    draw_string(270, 250, buf + 2, 0x00FFFFFF);

    draw_string(100, 300, "The system has been halted. You can still move the mouse.", 0x00FFFFFF);

    // 3. 开启中断, 让鼠标 IRQ 还能进
    __asm__ volatile ("sti");

    int old_x = mouse_get_x();
    int old_y = mouse_get_y();
    draw_mouse_cursor(old_x, old_y);

    // 4. 在红屏里维持鼠标存活的重绘循环
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

            // 擦除旧鼠标, 恢复深红色背景
            fill_rect(min_x, min_y, max_x - min_x, max_y - min_y, 0x00550000);
            
            // 防擦除: 如果鼠标划过了文字区域, 把字补回来
            if (min_y < 120 && max_y > 100) draw_string(100, 100, "================ KERNEL PANIC ================", 0x00FFFFFF);
            if (min_y < 170 && max_y > 150) draw_string(100, 150, "A fatal exception has occurred.", 0x00FFFFFF);
            if (min_y < 220 && max_y > 200) {
                draw_string(100, 200, "Reason:", 0x00FFFF00);
                draw_string(200, 200, msg, 0x00FFFFFF);
            }
            if (min_y < 270 && max_y > 250) {
                draw_string(100, 250, "RIP (Code):", 0x00FFFF00);
                draw_string(250, 250, "0x", 0x00FFFFFF);
                draw_string(270, 250, buf + 2, 0x00FFFFFF);
            }
            if (min_y < 320 && max_y > 300) draw_string(100, 300, "The system has been halted. You can still move the mouse.", 0x00FFFFFF);
            
            // 画新鼠标
            draw_mouse_cursor(mx, my);
            old_x = mx;
            old_y = my;
        }
        
        __asm__ volatile ("hlt");
    }
}