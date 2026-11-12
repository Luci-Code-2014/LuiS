#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stdint.h>

typedef enum {
    BLOCKDEV_NONE = 0,
    BLOCKDEV_ATA,      /* 传统 ATA PIO (0x1F0) */
    BLOCKDEV_AHCI,     /* AHCI (PCI BAR5) */
    BLOCKDEV_MEM,      /* 内嵌 rootfs.img 内存盘（只读回退） */
} blockdev_type_t;

blockdev_type_t blockdev_init(void);
blockdev_type_t blockdev_get_type(void);
int blockdev_read_sector(uint64_t lba, void *buf);
int blockdev_write_sector(uint64_t lba, const void *buf);
int blockdev_can_write(void);

#endif
