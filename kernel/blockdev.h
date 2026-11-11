#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stdint.h>

typedef enum {
    BLOCKDEV_NONE = 0,
    BLOCKDEV_ATA,
    BLOCKDEV_AHCI,
} blockdev_type_t;

blockdev_type_t blockdev_init(void);
blockdev_type_t blockdev_get_type(void);
int blockdev_read_sector(uint64_t lba, void *buf);
int blockdev_write_sector(uint64_t lba, const void *buf);
int blockdev_can_write(void);

#endif