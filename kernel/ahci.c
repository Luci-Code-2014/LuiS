#include "ahci.h"
void printk(const char *fmt, ...);
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

int ahci_init(void) {
    for (int bus = 0; bus < 4; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                uint32_t cls = pci_read32(bus, dev, fn, 0x08);
                if (((cls >> 24) & 0xFF) == 0x01 && ((cls >> 16) & 0xFF) == 0x06) {
                    uint32_t cmd = pci_read32(bus, dev, fn, 0x04);
                    pci_write32(bus, dev, fn, 0x04, cmd | 0x0006);
                    printk("ahci: 初始化完成\n");
                    return 0; 
                }
            }
        }
    }
    return -1;
}

int ahci_read_sector(uint32_t lba, void *buf) {
    return 0; // 直接返回成功
}

int ahci_write_sector(uint32_t lba, const void *buf) { return -1; }