#include "blockdev.h"
#include "ata.h"

int bdev_init(void) {
    return ata_init();
}

int bdev_read(uint32_t block, void *buf) {
    /* BLOCK_SIZE = 4096 bytes = 8 sectors of 512 */
    uint32_t lba = block * 8;
    return ata_read_sectors(lba, 8, buf);
}

int bdev_write(uint32_t block, const void *buf) {
    uint32_t lba = block * 8;
    return ata_write_sectors(lba, 8, buf);
}
