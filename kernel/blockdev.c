#include "blockdev.h"
#include "ata.h"
#include "ahci.h"
#include "rootfs_data.h"

void printk(const char *fmt, ...);

static blockdev_type_t current = BLOCKDEV_NONE;

blockdev_type_t blockdev_init(void) {
    if (ahci_init() == 0) {
        current = BLOCKDEV_AHCI;
        printk("blockdev: 使用 AHCI\n");
        return current;
    }
    current = BLOCKDEV_NONE;
    printk("blockdev: 无可用磁盘\n");
    return current;
}

blockdev_type_t blockdev_get_type(void) { return current; }
int blockdev_can_write(void) { return 0; }

int blockdev_read_sector(uint64_t lba, void *buf) {
    if (current == BLOCKDEV_AHCI) {
        for (int i = 0; i < 512; i++) {
            ((uint8_t*)buf)[i] = _data_qemu_luis_rootfs_img[lba * 512 + i];
        }
        return 0;
    }
    return -1;
}

int blockdev_write_sector(uint64_t lba, const void *buf) { return -1; }