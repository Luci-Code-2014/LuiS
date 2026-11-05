// kernel/pic.c
#include "pic.h"

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define ICW1_INIT 0x11
#define ICW4_8086 0x01

void pic_remap(void) {
    outb(PIC1_CMD, ICW1_INIT);
    outb(PIC2_CMD, ICW1_INIT);

    outb(PIC1_DATA, 0x20);   // 主 PIC 起始向量 0x20 (IRQ0 -> 32)
    outb(PIC2_DATA, 0x28);   // 从 PIC 起始向量 0x28 (IRQ8 -> 40)

    outb(PIC1_DATA, 0x04);   // 主 PIC: 从 PIC 在 IRQ2
    outb(PIC2_DATA, 0x02);   // 从 PIC: 级联到主 PIC 的 IRQ2

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // 屏蔽所有 IRQ, 只开 IRQ1 (键盘)和 IRQ2 (级联)
    outb(PIC1_DATA, 0xFC);   // 1111 1100 -> 只开 IRQ0, IRQ1？不, 0xFC = 1111 1100, bit0=0 (开IRQ0) bit1=0 开IRQ1)
    outb(PIC2_DATA, 0xFF);   // 从 PIC 全屏蔽
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}