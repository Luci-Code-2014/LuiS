// kernel/ata.c
#include "ata.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "d"(port));
}

static inline void ata_delay_400ns(uint16_t port) {
    for (int i = 0; i < 4; i++) inb(port + 7);
}

static int ata_wait_not_busy(uint16_t port) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(port + 7);
        if (status == 0xFF) return 0; // 没设备
        if (status & 0x01) return 0;  // 错误
        if ((status & 0x80) == 0) return 1; // 空闲
    }
    return 0;
}

static int ata_wait_drq(uint16_t port) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(port + 7);
        if (status == 0xFF) return 0;
        if (status & 0x01) return 0; // 错误
        if ((status & 0x80) == 0 && (status & 0x08)) return 1; // 准备就绪
    }
    return 0;
}

// 必须先选盘, 再等待空闲
static void ata_select(uint16_t port, uint8_t drive, uint32_t lba) {
    outb(port + 6, drive | ((lba >> 24) & 0x0F));
    ata_delay_400ns(port);
}

void ata_read_sector(uint16_t port, uint8_t drive, uint32_t lba, uint8_t *buffer) {
    ata_select(port, drive, lba);
    if (!ata_wait_not_busy(port)) return;

    outb(port + 2, 1);
    outb(port + 3, (uint8_t)lba);
    outb(port + 4, (uint8_t)(lba >> 8));
    outb(port + 5, (uint8_t)(lba >> 16));
    outb(port + 7, 0x20); // READ

    if (!ata_wait_drq(port)) return;

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        __asm__ volatile ("inw %1, %0" : "=a"(buf[i]) : "d"(port));
    }

    ata_wait_not_busy(port);
    ata_delay_400ns(port);
}

void ata_write_sector(uint16_t port, uint8_t drive, uint32_t lba, uint8_t *buffer) {
    ata_select(port, drive, lba);
    if (!ata_wait_not_busy(port)) return;

    outb(port + 2, 1);
    outb(port + 3, (uint8_t)lba);
    outb(port + 4, (uint8_t)(lba >> 8));
    outb(port + 5, (uint8_t)(lba >> 16));
    outb(port + 7, 0x30); // WRITE

    if (!ata_wait_drq(port)) return;

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(port, buf[i]);
    }

    ata_wait_not_busy(port);
    ata_delay_400ns(port);

    outb(port + 7, 0xE7); // FLUSH
    ata_wait_not_busy(port);
    ata_delay_400ns(port);
}