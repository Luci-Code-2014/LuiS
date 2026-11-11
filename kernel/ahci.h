#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

int ahci_init(void);
int ahci_read_sector(uint32_t lba, void *buf);
int ahci_write_sector(uint32_t lba, const void *buf);

#endif