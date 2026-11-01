#ifndef BERIN_FAT32_H
#define BERIN_FAT32_H

#include <stdint.h>
#include "vfs.h"

int  fat32_init(void);
void fat32_info(void);
void fat32_ls(void);
int  fat32_read(const char *filename, void *buffer, uint32_t max_size);

// Новые функции для VFS
struct vfs_ops *fat32_get_vfs_ops(void);
uint32_t fat32_get_root_cluster(void);

#endif
