#include "pit.h"

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

volatile unsigned long pit_ticks = 0;

/* freq_hz: 期望的中断频率 (Hz), 如 100 表示每 10ms 一次 */
void pit_init(unsigned int freq_hz) {
    unsigned int divisor = 1193182 / freq_hz;   /* PIT 基频 1.193182 MHz */

    outb(0x43, 0x36);                            /* 通道0, 先低后高, 模式3方波, 二进制 */
    outb(0x40, (unsigned char)(divisor & 0xFF));
    outb(0x40, (unsigned char)((divisor >> 8) & 0xFF));
}