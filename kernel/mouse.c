// kernel/mouse.c
#include "mouse.h"

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

static inline void wait_write(void) {
    for (volatile int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 2) == 0) break;
    }
}

static inline void wait_read(void) {
    for (volatile int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 1) == 1) break;
    }
}

static void mouse_write(unsigned char val) {
    wait_write();
    outb(0x64, 0xD4); // 告诉控制器: 接下来发给鼠标
    wait_write();
    outb(0x60, val);  // 发送数据
}

static unsigned char mouse_read(void) {
    wait_read();
    return inb(0x60);
}

// 鼠标坐标和状态
static int mouse_x = 960;
static int mouse_y = 540;
static int mouse_clicked = 0;

// 三字节数据包缓存
static unsigned char cycle = 0;
static unsigned char packet[3];

void mouse_init(void) {
    unsigned char status;

    // 1. 启用辅助设备 (鼠标)
    wait_write();
    outb(0x64, 0xA8);

    // 2. 启用 IRQ12
    wait_write();
    outb(0x64, 0x20);
    wait_read();
    status = inb(0x60);
    status |= 2; // 设置 bit 1
    wait_write();
    outb(0x64, 0x60);
    wait_write();
    outb(0x60, status);

    // 3. 发送初始化命令
    mouse_write(0xF6); // 设置默认参数
    mouse_read();      // 读 ACK (0xFA)
    
    mouse_write(0xF4); // 启用鼠标数据上报
    mouse_read();      // 读 ACK
}

void mouse_handle_irq(void) {
    unsigned char data = inb(0x60);

    // 如果第一个字节的 bit 3 不是 1, 说明数据错位, 直接抛弃
    if (cycle == 0 && !(data & 0x08)) return;

    packet[cycle] = data;
    cycle++;

    if (cycle == 3) {
        cycle = 0;

        // 1. 点击状态
        mouse_clicked = (packet[0] & 0x01) ? 1 : 0;

        // 2. 强制符号扩展 (将 8 位无符号转为 32 位有符号)
        // 先强转成 signed char, 再转成 int, 这样 0xFF 就会变成 -1
        int dx = (int)(signed char)packet[1];
        int dy = (int)(signed char)packet[2];

        // 3. 处理溢出标志
        // 如果溢出 (bit 6 或 bit 7), 说明鼠标滑得太快了, 硬件截断, 我们直接忽略本次移动
        if (packet[0] & 0x40) dx = 0; // X 溢出
        if (packet[0] & 0x80) dy = 0; // Y 溢出

        // 4. 更新坐标
        mouse_x += dx;
        mouse_y -= dy; // PS/2 鼠标上移是正, 但屏幕下移是正, 所以减去

        // 5. 屏幕边界限制
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x > 1016) mouse_x = 1016;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y > 752) mouse_y = 752;
    }
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_clicked(void) { return mouse_clicked; }