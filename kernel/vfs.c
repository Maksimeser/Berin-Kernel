#include "vfs.h"
#include "kberfs.h"
#include "kstring.h"

/* Разбивает /a/b/c на parent="/a/b" и name="c" */
static int split_path(const char *path, char *parent, char *name) {
    const char *last = 0;
    for (const char *p = path; *p; p++) if (*p == '/') last = p;
    if (!last) return -1;

    int len = last - path;
    if (len == 0) { parent[0] = '/'; parent[1] = '\0'; }
    else { memcpy(parent, path, len); parent[len] = '\0'; }

    int nlen = 0;
    while (last[1+nlen]) { name[nlen] = last[1+nlen]; nlen++; }
    name[nlen] = '\0';

    if (nlen == 0) return -1;
    return 0;
}

static struct vfs_mount g_mounts[VFS_MAX_MOUNTS];
static int              g_mount_count;

struct vfs_open_file {
    int               used;
    struct vfs_mount *mount;
    uint32_t          ino;
    uint32_t          pos;
    uint32_t          size;
    int               flags;
};
static struct vfs_open_file g_files[VFS_MAX_FILES];

void vfs_init(void) {
    g_mount_count = 0;
    for (int i = 0; i < VFS_MAX_FILES; i++) g_files[i].used = 0;
}

int vfs_mount_add(const char *path, uint32_t root_ino,
                  const struct vfs_ops *ops, void *fs_data) {
    if (g_mount_count >= VFS_MAX_MOUNTS) return -1;
    struct vfs_mount *m = &g_mounts[g_mount_count];
    int i = 0;
    while (path[i] && i < VFS_PATH_MAX - 1) { m->path[i] = path[i]; i++; }
    m->path[i] = '\0';
    m->root_ino = root_ino;
    m->ops      = ops;
    m->fs_data  = fs_data;
    g_mount_count++;
    return 0;
                  }

                  static struct vfs_mount *find_mount(const char *path, const char **rest) {
                      int best_len = -1;
                      struct vfs_mount *best = 0;
                      for (int i = 0; i < g_mount_count; i++) {
                          const char *mp = g_mounts[i].path;
                          int j = 0;
                          while (mp[j] && path[j] && mp[j] == path[j]) j++;
                          if (mp[j] == '\0') {
                              if (j > best_len) { best_len = j; best = &g_mounts[i]; }
                          }
                      }
                      if (!best) return 0;
                      if (rest) *rest = path + best_len;
                      return best;
                  }

                  static uint32_t walk(struct vfs_mount *m, uint32_t start_ino, const char *rel) {
                      uint32_t cur = start_ino;
                      if (!rel || !*rel || (rel[0] == '/' && !rel[1])) return cur;
                      while (*rel == '/') rel++;

                      char name[BERFS_NAME_MAX + 1];
                      while (*rel) {
                          int n = 0;
                          while (*rel && *rel != '/' && n < BERFS_NAME_MAX) name[n++] = *rel++;
                          name[n] = '\0';
                          while (*rel == '/') rel++;
                          uint32_t next;
                          if (m->ops->lookup(m, cur, name, &next) != 0) return 0;
                          cur = next;
                      }
                      return cur;
                  }

                  int vfs_resolve(const char *path, struct vfs_mount **out_mount, uint32_t *out_ino) {
                      if (!path || path[0] != '/') return -1;
                      const char *rel;
                      struct vfs_mount *m = find_mount(path, &rel);
                      if (!m) return -1;
                      uint32_t ino = walk(m, m->root_ino, rel);
                      if (ino == 0) return -2;
                      if (out_mount) *out_mount = m;
                      if (out_ino)   *out_ino   = ino;
                      return 0;
                  }

                  int vfs_stat(const char *path, uint32_t *size, uint8_t *type) {
                      struct vfs_mount *m;
                      uint32_t ino;
                      if (vfs_resolve(path, &m, &ino) != 0) return -1;
                      return m->ops->stat(m, ino, size, type);
                  }

                  int vfs_open(const char *path, int flags) {
                      struct vfs_mount *m;
                      uint32_t ino;
                      if (vfs_resolve(path, &m, &ino) != 0) return -1;

                      uint32_t size;
                      uint8_t type;
                      if (m->ops->stat(m, ino, &size, &type) != 0) return -2;
                      if (type != VFS_FILE) return -3;

                      if (flags & O_TRUNC) {
                          struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                          struct berfs_inode in;
                          if (berfs_read_inode(bm, ino, &in) == 0) {
                              in.i_size = 0;
                              berfs_write_inode(bm, ino, &in);
                          }
                          size = 0;
                      }

                      for (int i = 3; i < VFS_MAX_FILES; i++) {
                          if (!g_files[i].used) {
                              g_files[i].used  = 1;
                              g_files[i].mount = m;
                              g_files[i].ino   = ino;
                              g_files[i].pos   = 0;
                              g_files[i].size  = size;
                              g_files[i].flags = flags;
                              return i;
                          }
                      }
                      return -4;
                  }

                  int vfs_read(int fd, void *buf, uint32_t len) {
                      if (fd < 0 || fd >= VFS_MAX_FILES) return -1;
                      struct vfs_open_file *f = &g_files[fd];
                      if (!f->used) return -2;
                      if (f->pos >= f->size) return 0;
                      uint32_t left = f->size - f->pos;
                      if (len > left) len = left;
                      int r = f->mount->ops->read(f->mount, f->ino, f->pos, buf, len);
                      if (r > 0) f->pos += r;
                      return r;
                  }

                  int vfs_write(int fd, const void *buf, uint32_t len) {
                      if (fd < 0 || fd >= VFS_MAX_FILES) return -1;
                      struct vfs_open_file *f = &g_files[fd];
                      if (!f->used) return -2;

                      struct berfs_mount *bm = (struct berfs_mount *)f->mount->fs_data;
                      struct berfs_inode in;
                      if (berfs_read_inode(bm, f->ino, &in) != 0) return -3;

                      int r = berfs_file_write(bm, &in, f->pos, buf, len);
                      if (r == 0) {
                          berfs_write_inode(bm, f->ino, &in);
                          f->pos += len;
                          return len;
                      }
                      return -1;
                  }

                  int vfs_close(int fd) {
                      if (fd < 0 || fd >= VFS_MAX_FILES) return -1;
                      if (!g_files[fd].used) return -2;
                      g_files[fd].used = 0;
                      return 0;
                  }

                  int vfs_listdir(const char *path, uint32_t *pos,
                                  char *name, uint32_t *ino, uint8_t *type) {
                      struct vfs_mount *m;
                      uint32_t dir_ino;
                      if (vfs_resolve(path, &m, &dir_ino) != 0) return -1;
                      uint32_t sz; uint8_t t;
                      if (m->ops->stat(m, dir_ino, &sz, &t) != 0) return -1;
                      if (t != VFS_DIR) return -2;
                      return m->ops->readdir(m, dir_ino, pos, name, ino, type);
                                  }

                                  int vfs_create(const char *path) {
                                      struct vfs_mount *m;
                                      uint32_t existing_ino;
                                      if (vfs_resolve(path, &m, &existing_ino) == 0) return -2;

                                      char parent_path[256], name[256];
                                      if (split_path(path, parent_path, name) != 0) return -1;

                                      uint32_t parent_ino;
                                      if (vfs_resolve(parent_path, &m, &parent_ino) != 0) return -3;

                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      uint32_t ino = berfs_alloc_inode(bm);
                                      if (ino == 0) return -4;

                                      struct berfs_inode in;
                                      memset(&in, 0, sizeof(in));
                                      in.i_mode = BERFS_TYPE_FILE | 0644;
                                      in.i_size = 0;
                                      in.i_links_count = 1;

                                      if (berfs_write_inode(bm, ino, &in) != 0) return -5;

                                      struct berfs_inode parent;
                                      if (berfs_read_inode(bm, parent_ino, &parent) != 0) return -1;
                                      if (berfs_dir_add(bm, &parent, parent_ino, name, ino, BERFS_DT_FILE) != 0) return -5;

                                      return 0;
                                  }

                                  int vfs_mkdir(const char *path) {
                                      struct vfs_mount *m;
                                      uint32_t existing_ino;
                                      if (vfs_resolve(path, &m, &existing_ino) == 0) return -2;

                                      char parent_path[256], name[256];
                                      if (split_path(path, parent_path, name) != 0) return -1;

                                      uint32_t parent_ino;
                                      if (vfs_resolve(parent_path, &m, &parent_ino) != 0) return -3;

                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      uint32_t ino = berfs_alloc_inode(bm);
                                      if (ino == 0) return -4;

                                      struct berfs_inode new_dir;
                                      memset(&new_dir, 0, sizeof(new_dir));
                                      new_dir.i_mode = BERFS_TYPE_DIR | 0755;
                                      new_dir.i_size = 0;
                                      new_dir.i_links_count = 2;

                                      if (berfs_dir_add(bm, &new_dir, ino, ".", ino, BERFS_DT_DIR) != 0) return -5;
                                      if (berfs_dir_add(bm, &new_dir, ino, "..", parent_ino, BERFS_DT_DIR) != 0) return -5;
                                      if (berfs_write_inode(bm, ino, &new_dir) != 0) return -5;

                                      struct berfs_inode parent;
                                      if (berfs_read_inode(bm, parent_ino, &parent) != 0) return -1;
                                      if (berfs_dir_add(bm, &parent, parent_ino, name, ino, BERFS_DT_DIR) != 0) return -5;

                                      return 0;
                                  }

                                  int vfs_unlink(const char *path) {
                                      char parent_path[256], name[256];
                                      if (split_path(path, parent_path, name) != 0) return -1;

                                      struct vfs_mount *m;
                                      uint32_t parent_ino;
                                      if (vfs_resolve(parent_path, &m, &parent_ino) != 0) return -2;

                                      uint32_t ino;
                                      if (vfs_resolve(path, &m, &ino) != 0) return -1;

                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      struct berfs_inode parent;
                                      if (berfs_read_inode(bm, parent_ino, &parent) != 0) return -1;

                                      if (berfs_dir_remove(bm, &parent, parent_ino, name) != 0) return -2;

                                      struct berfs_inode in;
                                      if (berfs_read_inode(bm, ino, &in) == 0) {

                                          extern void berfs_truncate(struct berfs_mount *m, struct berfs_inode *in);
                                          berfs_truncate(bm, &in);
                                          berfs_write_inode(bm, ino, &in); //
                                      }
                                      berfs_free_inode(bm, ino);
                                      return 0;
                                  }

                                  static int bf_read(struct vfs_mount *m, uint32_t ino, uint32_t pos, void *buf, uint32_t len) {
                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      struct berfs_inode in;
                                      if (berfs_read_inode(bm, ino, &in) != 0) return -1;
                                      if (berfs_file_read(bm, &in, pos, buf, len) != 0) return -1;
                                      return (int)len;
                                  }

                                  static int bf_lookup(struct vfs_mount *m, uint32_t dir_ino, const char *name, uint32_t *out_ino) {
                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      struct berfs_inode dir;
                                      if (berfs_read_inode(bm, dir_ino, &dir) != 0) return -1;
                                      uint32_t ino = berfs_dir_lookup(bm, &dir, name);
                                      if (ino == 0) return -2;
                                      *out_ino = ino;
                                      return 0;
                                  }

                                  static int bf_readdir(struct vfs_mount *m, uint32_t dir_ino, uint32_t *pos,
                                                        char *name, uint32_t *out_ino, uint8_t *type) {
                                      struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                      struct berfs_inode dir;
                                      if (berfs_read_inode(bm, dir_ino, &dir) != 0) return -1;
                                      int r = berfs_dir_next(bm, &dir, pos, out_ino, name, type);
                                      if (r == 1) {
                                          *type = (*type == BERFS_DT_DIR) ? VFS_DIR : VFS_FILE;
                                      }
                                      return r;
                                                        }

                                                        static int bf_stat(struct vfs_mount *m, uint32_t ino, uint32_t *size, uint8_t *type) {
                                                            struct berfs_mount *bm = (struct berfs_mount *)m->fs_data;
                                                            struct berfs_inode in;
                                                            if (berfs_read_inode(bm, ino, &in) != 0) return -1;
                                                            *size = in.i_size;
                                                            uint16_t t = in.i_mode & 0xF000u;
                                                            if      (t == BERFS_TYPE_DIR) *type = VFS_DIR;
                                                            else if (t == BERFS_TYPE_FILE) *type = VFS_FILE;
                                                            else *type = VFS_FILE;
                                                            return 0;
                                                        }

                                                        static const struct vfs_ops berfs_ops = {
                                                            .read    = bf_read,
                                                            .lookup  = bf_lookup,
                                                            .readdir = bf_readdir,
                                                            .stat    = bf_stat,
                                                        };

                                                        int vfs_mount_berfs(struct berfs_mount *bm) {
                                                            return vfs_mount_add("/", BERFS_ROOT_INODE, &berfs_ops, bm);
                                                        }
