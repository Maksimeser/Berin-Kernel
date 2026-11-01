#ifndef BERIN_ATA_H
#define BERIN_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

int      ata_init(void);                                    /* 0 = ok */
int      ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int ata_read_slave(uint32_t lba, uint8_t count, void *buf);
int      ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);
uint32_t ata_sector_count(void);                            /* from IDENTIFY */
const char *ata_model(void);                                /* from IDENTIFY */

#endif
