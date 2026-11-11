// kernel/ata.h
#ifndef ATA_H
#define ATA_H
#include <stdint.h>

int  ata_probe(void);
void ata_read_sector(uint16_t io_base, uint8_t drive, uint32_t lba, uint8_t *buf);
void ata_write_sector(uint16_t io_base, uint8_t drive, uint32_t lba, uint8_t *buf);

#endif