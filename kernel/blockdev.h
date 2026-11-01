#ifndef BERIN_BLOCKDEV_H
#define BERIN_BLOCKDEV_H

#include <stdint.h>

#define BLOCK_SIZE 4096

/* init: 0 = ok */
int      bdev_init(void);

/* read/write one 4096-byte block at block index (block 0 = LBA 0) */
int      bdev_read (uint32_t block, void *buf);
int      bdev_write(uint32_t block, const void *buf);

#endif
