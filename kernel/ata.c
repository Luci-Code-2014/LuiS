#include "ata.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

void ata_read_sector(uint16_t port, uint8_t drive, uint32_t lba, uint8_t *buffer) {
    for (int i = 0; i < 1000000; i++) {
        if ((inb(port + 7) & 0x80) == 0) break;
    }
    outb(port + 6, drive | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 4; i++) inb(port + 7);
    outb(port + 2, 1);
    outb(port + 3, (uint8_t)lba);
    outb(port + 4, (uint8_t)(lba >> 8));
    outb(port + 5, (uint8_t)(lba >> 16));
    outb(port + 7, 0x20);
    for (int i = 0; i < 1000000; i++) {
        if ((inb(port + 7) & 0x08) != 0) break;
    }
    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        __asm__ volatile ("inw %1, %0" : "=a"(buf[i]) : "d"(port));
    }
}