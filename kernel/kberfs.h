#ifndef BERIN_KERNEL_BERFS_H
#define BERIN_KERNEL_BERFS_H

#include <stdint.h>
#include "berfs.h"

struct berfs_mount {
    struct berfs_superblock sb;
    int mounted;
};

int  berfs_mount_fs(struct berfs_mount *m);
int  berfs_read_inode(struct berfs_mount *m, uint32_t ino, struct berfs_inode *out);
int  berfs_write_inode(struct berfs_mount *m, uint32_t ino, const struct berfs_inode *in);
int  berfs_read_block(struct berfs_mount *m, uint32_t block, void *buf);
uint32_t berfs_dir_lookup(struct berfs_mount *m, const struct berfs_inode *dir, const char *name);
int  berfs_dir_next(struct berfs_mount *m, const struct berfs_inode *dir,
                    uint32_t *pos, uint32_t *ino, char *name, uint8_t *type);
int  berfs_file_read(struct berfs_mount *m, const struct berfs_inode *in, uint32_t pos, void *dst, uint32_t len);
int  berfs_file_write(struct berfs_mount *m, struct berfs_inode *in, uint32_t pos, const void *src, uint32_t len);
uint32_t berfs_alloc_block(struct berfs_mount *m);
uint32_t berfs_alloc_inode(struct berfs_mount *m);
int  berfs_dir_add(struct berfs_mount *m, struct berfs_inode *dir_inode,
                   uint32_t dir_ino, const char *name, uint32_t ino, uint8_t type);
int berfs_free_block(struct berfs_mount *m, uint32_t blk);
int berfs_free_inode(struct berfs_mount *m, uint32_t ino);
int berfs_dir_remove(struct berfs_mount *m, struct berfs_inode *dir_inode,
                     uint32_t dir_ino, const char *name);

void berfs_truncate(struct berfs_mount *m, struct berfs_inode *in);

#endif
