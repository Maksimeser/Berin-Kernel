#ifndef BERIN_VFS_H
#define BERIN_VFS_H

#include <stdint.h>
#include "berfs.h"

struct berfs_mount;

#define VFS_FILE  1
#define VFS_DIR   2

#define O_RDONLY  0x01
#define O_WRONLY  0x02
#define O_RDWR    0x03
#define O_CREAT   0x04
#define O_TRUNC   0x08

#define VFS_MAX_FILES 32
#define VFS_MAX_MOUNTS 4
#define VFS_PATH_MAX 256

struct vfs_mount;

struct vfs_ops {
    int      (*read)(struct vfs_mount *m, uint32_t ino, uint32_t pos, void *buf, uint32_t len);
    int      (*lookup)(struct vfs_mount *m, uint32_t dir_ino, const char *name, uint32_t *out_ino);
    int      (*readdir)(struct vfs_mount *m, uint32_t dir_ino, uint32_t *pos,
                        char *name, uint32_t *out_ino, uint8_t *type);
    int      (*stat)(struct vfs_mount *m, uint32_t ino, uint32_t *size, uint8_t *type);
};

struct vfs_mount {
    char              path[VFS_PATH_MAX];
    uint32_t          root_ino;
    const struct vfs_ops *ops;
    void             *fs_data;
};

void vfs_init(void);
int  vfs_mount_add(const char *path, uint32_t root_ino, const struct vfs_ops *ops, void *fs_data);
int  vfs_resolve(const char *path, struct vfs_mount **out_mount, uint32_t *out_ino);
int  vfs_stat(const char *path, uint32_t *size, uint8_t *type);
int  vfs_open(const char *path, int flags);
int  vfs_read(int fd, void *buf, uint32_t len);
int  vfs_write(int fd, const void *buf, uint32_t len);
int  vfs_close(int fd);
int  vfs_listdir(const char *path, uint32_t *pos, char *name, uint32_t *ino, uint8_t *type);
int  vfs_mount_berfs(struct berfs_mount *bm);
int  vfs_create(const char *path);
int  vfs_mkdir(const char *path);
int  vfs_unlink(const char *path);

#endif
