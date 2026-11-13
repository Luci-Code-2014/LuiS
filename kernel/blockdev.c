// kernel/blockdev.c
#include "blockdev.h"
#include "ata.h"
#include "ahci.h"
#include "rootfs_data.h"
#include "printk.h"

extern const unsigned char _binary_fs_rootfs_img_end[];
#define ROOTFS_SIZE   ((uint64_t)((uintptr_t)_binary_fs_rootfs_img_end - (uintptr_t)_data_qemu_luis_rootfs_img))

#define ATA_PRIMARY_IO     0x1F0
#define ATA_TARGET_DRIVE   0xF0

static blockdev_type_t current = BLOCKDEV_NONE;

static int mem_read_sector(uint64_t lba, void *buf) {
    uint64_t off = lba * 512;
    uint8_t *dst = (uint8_t *)buf;
    if (off + 512 > ROOTFS_SIZE) {
        for (int i = 0; i < 512; i++) dst[i] = 0;
        return -1;
    }
    const uint8_t *src = (const uint8_t *)_data_qemu_luis_rootfs_img + off;
    for (int i = 0; i < 512; i++) dst[i] = src[i];
    return 0;
}

blockdev_type_t blockdev_init(void) {
    if (ahci_init() == 0) {
        current = BLOCKDEV_AHCI;
        printk("%[O] blockdev: 使用 AHCI 物理磁盘\n");
        return current;
    }

    printk("%[W] blockdev: AHCI 不可用，尝试 ATA PIO\n");
    current = BLOCKDEV_ATA;

    uint8_t test_buf[512];
    ata_read_sector(ATA_PRIMARY_IO, ATA_TARGET_DRIVE, 2, test_buf);
    uint16_t magic = *(uint16_t *)(test_buf + 0x38);
    if (magic == 0xEF53) {
        printk("%[O] blockdev: 使用 ATA PIO 物理磁盘\n");
        return current;
    }

    printk("%[W] blockdev: 硬件磁盘不可用，回退到内嵌内存盘\n");
    current = BLOCKDEV_MEM;
    return current;
}

blockdev_type_t blockdev_get_type(void) { return current; }

int blockdev_can_write(void) { return (current == BLOCKDEV_ATA || current == BLOCKDEV_AHCI); }

int blockdev_read_sector(uint64_t lba, void *buf) {
    if (buf == 0) return -1;
    switch (current) {
        case BLOCKDEV_ATA:
            ata_read_sector(ATA_PRIMARY_IO, ATA_TARGET_DRIVE, (uint32_t)lba, (uint8_t *)buf);
            return 0;
        case BLOCKDEV_AHCI:
            return ahci_read_sector((uint32_t)lba, buf);
        case BLOCKDEV_MEM:
            return mem_read_sector(lba, buf);
        default:
            return -1;
    }
}

int blockdev_write_sector(uint64_t lba, const void *buf) {
    if (buf == 0) return -1;
    switch (current) {
        case BLOCKDEV_ATA:
            ata_write_sector(ATA_PRIMARY_IO, ATA_TARGET_DRIVE, (uint32_t)lba, (uint8_t *)buf);
            return 0;
        case BLOCKDEV_AHCI:
            return ahci_write_sector((uint32_t)lba, buf);
        case BLOCKDEV_MEM:
            return -1;
        default:
            return -1;
    }
}
