#include "kberfs.h"
#include "blockdev.h"
#include "kstring.h"
#include "heap.h"

static void berfs_write_sb(struct berfs_mount *m) {
    uint8_t *buf = kmalloc(BLOCK_SIZE);
    if (!buf) return;
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &m->sb, sizeof(m->sb));
    bdev_write(1, buf);
    kfree(buf);
}

int berfs_read_block(struct berfs_mount *m, uint32_t block, void *buf) {
    (void)m;
    return bdev_read(block, buf);
}

int berfs_mount_fs(struct berfs_mount *m) {
    uint8_t *buf = kmalloc(BLOCK_SIZE);
    if (!buf) return -1;
    int rc = berfs_read_block(m, 1, buf);
    if (rc == 0) {
        memcpy(&m->sb, buf, sizeof(m->sb));
        if (m->sb.sb_magic != BERFS_MAGIC) rc = -2;
        else if (m->sb.sb_version != BERFS_VERSION) rc = -3;
        else m->mounted = 1;
    }
    kfree(buf);
    return rc;
}

int berfs_read_inode(struct berfs_mount *m, uint32_t ino, struct berfs_inode *out) {
    if (ino >= m->sb.sb_total_inodes) return -1;
    uint32_t ipb = BLOCK_SIZE / sizeof(struct berfs_inode);
    uint32_t block = m->sb.sb_inode_table_block + (ino / ipb);
    uint32_t index = ino % ipb;

    uint8_t *buf = kmalloc(BLOCK_SIZE);
    if (!buf) return -2;
    int rc = berfs_read_block(m, block, buf);
    if (rc == 0) memcpy(out, buf + index * sizeof(struct berfs_inode), sizeof(struct berfs_inode));
    kfree(buf);
    return rc;
}

int berfs_write_inode(struct berfs_mount *m, uint32_t ino, const struct berfs_inode *in) {
    if (ino >= m->sb.sb_total_inodes) return -1;
    uint32_t ipb = BLOCK_SIZE / sizeof(struct berfs_inode);
    uint32_t block = m->sb.sb_inode_table_block + (ino / ipb);
    uint32_t index = ino % ipb;

    uint8_t *buf = kmalloc(BLOCK_SIZE);
    if (!buf) return -2;
    if (berfs_read_block(m, block, buf) != 0) { kfree(buf); return -2; }
    memcpy(buf + index * sizeof(struct berfs_inode), in, sizeof(struct berfs_inode));
    int rc = bdev_write(block, buf);
    kfree(buf);
    return rc;
}

/* =========================================================
 * INDIRECT BLOCKS (BerFS 2.0 - до 4 ТБ на файл)
 * ========================================================= */

// Возвращает физический блок по индексу. Если create=1, выделяет память под таблицы.
static uint32_t berfs_get_block(struct berfs_mount *m, struct berfs_inode *in, uint32_t block_idx, int create) {
    uint32_t ptrs_per_block = BLOCK_SIZE / sizeof(uint32_t); // 1024

    // 1. Прямые блоки (0 - 11)
    if (block_idx < BERFS_DIRECT_BLOCKS) {
        if (in->i_block[block_idx] == 0 && create) {
            in->i_block[block_idx] = berfs_alloc_block(m);
        }
        return in->i_block[block_idx];
    }
    block_idx -= BERFS_DIRECT_BLOCKS;

    // 2. Одинарный indirect (12 - 1035)
    if (block_idx < ptrs_per_block) {
        if (in->i_indirect == 0) {
            if (!create) return 0;
            in->i_indirect = berfs_alloc_block(m);
            if (in->i_indirect == 0) return 0;
            uint8_t *zero = kmalloc(BLOCK_SIZE);
            if (zero) { memset(zero, 0, BLOCK_SIZE); bdev_write(in->i_indirect, zero); kfree(zero); }
        }
        uint8_t *buf = kmalloc(BLOCK_SIZE);
        if (!buf) return 0;
        bdev_read(in->i_indirect, buf);
        uint32_t *ptrs = (uint32_t *)buf;
        if (ptrs[block_idx] == 0 && create) {
            ptrs[block_idx] = berfs_alloc_block(m);
            bdev_write(in->i_indirect, buf);
        }
        uint32_t phys = ptrs[block_idx];
        kfree(buf);
        return phys;
    }
    block_idx -= ptrs_per_block;

    // 3. Двойной indirect (1036 - 1049611)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (in->i_double_indirect == 0) {
            if (!create) return 0;
            in->i_double_indirect = berfs_alloc_block(m);
            if (in->i_double_indirect == 0) return 0;
            uint8_t *zero = kmalloc(BLOCK_SIZE);
            if (zero) { memset(zero, 0, BLOCK_SIZE); bdev_write(in->i_double_indirect, zero); kfree(zero); }
        }
        uint32_t l1_idx = block_idx / ptrs_per_block;
        uint32_t l2_idx = block_idx % ptrs_per_block;

        uint8_t *buf1 = kmalloc(BLOCK_SIZE);
        if (!buf1) return 0;
        bdev_read(in->i_double_indirect, buf1);
        uint32_t *ptrs1 = (uint32_t *)buf1;

        if (ptrs1[l1_idx] == 0) {
            if (!create) { kfree(buf1); return 0; }
            ptrs1[l1_idx] = berfs_alloc_block(m);
            bdev_write(in->i_double_indirect, buf1);
            uint8_t *zero = kmalloc(BLOCK_SIZE);
            if (zero) { memset(zero, 0, BLOCK_SIZE); bdev_write(ptrs1[l1_idx], zero); kfree(zero); }
        }
        uint32_t second_table = ptrs1[l1_idx];
        kfree(buf1);

        uint8_t *buf2 = kmalloc(BLOCK_SIZE);
        if (!buf2) return 0;
        bdev_read(second_table, buf2);
        uint32_t *ptrs2 = (uint32_t *)buf2;
        if (ptrs2[l2_idx] == 0 && create) {
            ptrs2[l2_idx] = berfs_alloc_block(m);
            bdev_write(second_table, buf2);
        }
        uint32_t phys = ptrs2[l2_idx];
        kfree(buf2);
        return phys;
    }
    block_idx -= ptrs_per_block * ptrs_per_block;

    // 4. Тройной indirect (1049612 - 1073742859) -> до 4 ТБ
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (in->i_triple_indirect == 0) {
            if (!create) return 0;
            in->i_triple_indirect = berfs_alloc_block(m);
            if (in->i_triple_indirect == 0) return 0;
            uint8_t *zero = kmalloc(BLOCK_SIZE);
            if (zero) { memset(zero, 0, BLOCK_SIZE); bdev_write(in->i_triple_indirect, zero); kfree(zero); }
        }
        uint32_t l1 = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t l2 = (block_idx / ptrs_per_block) % ptrs_per_block;
        uint32_t l3 = block_idx % ptrs_per_block;

        // Уровень 1
        uint8_t *b1 = kmalloc(BLOCK_SIZE); bdev_read(in->i_triple_indirect, b1);
        uint32_t *p1 = (uint32_t *)b1;
        if (p1[l1] == 0) {
            if (!create) { kfree(b1); return 0; }
            p1[l1] = berfs_alloc_block(m); bdev_write(in->i_triple_indirect, b1);
            uint8_t *zero = kmalloc(BLOCK_SIZE); memset(zero, 0, BLOCK_SIZE);
            bdev_write(p1[l1], zero); kfree(zero);
        }
        uint32_t t2 = p1[l1]; kfree(b1);

        // Уровень 2
        uint8_t *b2 = kmalloc(BLOCK_SIZE); bdev_read(t2, b2);
        uint32_t *p2 = (uint32_t *)b2;
        if (p2[l2] == 0) {
            if (!create) { kfree(b2); return 0; }
            p2[l2] = berfs_alloc_block(m); bdev_write(t2, b2);
            uint8_t *zero = kmalloc(BLOCK_SIZE); memset(zero, 0, BLOCK_SIZE);
            bdev_write(p2[l2], zero); kfree(zero);
        }
        uint32_t t3 = p2[l2]; kfree(b2);

        // Уровень 3
        uint8_t *b3 = kmalloc(BLOCK_SIZE); bdev_read(t3, b3);
        uint32_t *p3 = (uint32_t *)b3;
        if (p3[l3] == 0 && create) {
            p3[l3] = berfs_alloc_block(m); bdev_write(t3, b3);
        }
        uint32_t phys = p3[l3]; kfree(b3);
        return phys;
    }

    return 0; // Файл больше 4 ТБ
}

/* =========================================================
 * ЧТЕНИЕ И ЗАПИСЬ
 * ========================================================= */

static int file_read_bytes(struct berfs_mount *m, const struct berfs_inode *in,
                           uint32_t pos, void *dst, uint32_t len) {
    uint8_t *p = (uint8_t *)dst;
    uint32_t done = 0;
    uint8_t *buf = kmalloc(BLOCK_SIZE);
    if (!buf) return -1;

    while (done < len) {
        uint32_t bp = pos + done;
        if (bp >= in->i_size) { kfree(buf); return -1; }
        uint32_t bi = bp / BLOCK_SIZE;
        uint32_t off = bp % BLOCK_SIZE;

        // Читаем физический блок через таблицу (create = 0)
        uint32_t block = berfs_get_block(m, (struct berfs_inode *)in, bi, 0);
        if (block == 0) { kfree(buf); return -1; }

        uint32_t chunk = BLOCK_SIZE - off;
        if (chunk > len - done) chunk = len - done;
        if (bdev_read(block, buf) != 0) { kfree(buf); return -1; }
        memcpy(p + done, buf + off, chunk);
        done += chunk;
    }
    kfree(buf);
    return 0;
                           }

                           int berfs_file_read(struct berfs_mount *m, const struct berfs_inode *in,
                                               uint32_t pos, void *dst, uint32_t len) {
                               return file_read_bytes(m, in, pos, dst, len);
                                               }

                                               int berfs_file_write(struct berfs_mount *m, struct berfs_inode *in,
                                                                    uint32_t pos, const void *src, uint32_t len) {
                                                   const uint8_t *p = (const uint8_t *)src;
                                                   uint32_t done = 0;
                                                   uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                   uint8_t *zero = kmalloc(BLOCK_SIZE);
                                                   if (!buf || !zero) { kfree(buf); kfree(zero); return -1; }
                                                   memset(zero, 0, BLOCK_SIZE);

                                                   while (done < len) {
                                                       uint32_t bp = pos + done;
                                                       uint32_t blk_idx = bp / BLOCK_SIZE;
                                                       uint32_t off = bp % BLOCK_SIZE;

                                                       // Выделяем блок через таблицу (create = 1)
                                                       uint32_t block = berfs_get_block(m, in, blk_idx, 1);
                                                       if (block == 0) { kfree(buf); kfree(zero); return -1; }

                                                       uint32_t chunk = BLOCK_SIZE - off;
                                                       if (chunk > len - done) chunk = len - done;

                                                       // Читаем блок, чтобы не затереть старое (если пишем в середину)
                                                       if (bdev_read(block, buf) != 0) memset(buf, 0, BLOCK_SIZE);

                                                       memcpy(buf + off, p + done, chunk);
                                                       if (bdev_write(block, buf) != 0) { kfree(buf); kfree(zero); return -1; }
                                                       done += chunk;
                                                   }
                                                   kfree(buf);
                                                   kfree(zero);
                                                   if (pos + len > in->i_size) {
                                                       in->i_size = pos + len;
                                                       in->i_blocks = (in->i_size + 511) / 512;
                                                   }
                                                   return 0;
                                                                    }

                                                                    /* =========================================================
                                                                     * АЛЛОКАЦИЯ И ОСВОБОЖДЕНИЕ
                                                                     * ========================================================= */

                                                                    uint32_t berfs_alloc_block(struct berfs_mount *m) {
                                                                        uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                                        if (!buf) return 0;
                                                                        uint32_t bitmap_start = m->sb.sb_block_bitmap_block;
                                                                        uint32_t bitmap_blocks = (m->sb.sb_total_blocks + 8*BLOCK_SIZE - 1) / (8*BLOCK_SIZE);

                                                                        for (uint32_t b = 0; b < bitmap_blocks; b++) {
                                                                            if (berfs_read_block(m, bitmap_start + b, buf) != 0) break;
                                                                            for (int i = 0; i < BLOCK_SIZE; i++) {
                                                                                if (buf[i] != 0xFF) {
                                                                                    for (int bit = 0; bit < 8; bit++) {
                                                                                        if (!(buf[i] & (1 << bit))) {
                                                                                            uint32_t blk = b * BLOCK_SIZE * 8 + i * 8 + bit;
                                                                                            if (blk < m->sb.sb_data_block) continue;
                                                                                            if (blk >= m->sb.sb_total_blocks) { kfree(buf); return 0; }

                                                                                            buf[i] |= (1 << bit);
                                                                                            bdev_write(bitmap_start + b, buf);

                                                                                            m->sb.sb_free_blocks--;
                                                                                            berfs_write_sb(m);
                                                                                            kfree(buf);
                                                                                            return blk;
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        kfree(buf);
                                                                        return 0;
                                                                    }

                                                                    uint32_t berfs_alloc_inode(struct berfs_mount *m) {
                                                                        uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                                        if (!buf) return 0;
                                                                        uint32_t bitmap_start = m->sb.sb_inode_bitmap_block;

                                                                        berfs_read_block(m, bitmap_start, buf);
                                                                        for (int i = 0; i < BLOCK_SIZE; i++) {
                                                                            if (buf[i] != 0xFF) {
                                                                                for (int bit = 0; bit < 8; bit++) {
                                                                                    if (!(buf[i] & (1 << bit))) {
                                                                                        uint32_t ino = i * 8 + bit;
                                                                                        if (ino < 2 || ino >= m->sb.sb_total_inodes) continue;

                                                                                        buf[i] |= (1 << bit);
                                                                                        bdev_write(bitmap_start, buf);

                                                                                        m->sb.sb_free_inodes--;
                                                                                        berfs_write_sb(m);
                                                                                        kfree(buf);
                                                                                        return ino;
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                        kfree(buf);
                                                                        return 0;
                                                                    }

                                                                    /* =========================================================
                                                                     * ДИРЕКТОРИИ
                                                                     * ========================================================= */

                                                                    int berfs_dir_next(struct berfs_mount *m, const struct berfs_inode *dir,
                                                                                       uint32_t *pos, uint32_t *ino, char *name, uint8_t *type) {
                                                                        while (*pos < dir->i_size) {
                                                                            struct berfs_dirent de;
                                                                            if (file_read_bytes(m, dir, *pos, &de, sizeof(de)) != 0) return -1;
                                                                            if (de.d_inode == 0 || de.d_reclen == 0) { *pos += de.d_reclen; continue; }
                                                                            if (file_read_bytes(m, dir, *pos + sizeof(de), name, de.d_name_len) != 0) return -1;
                                                                            name[de.d_name_len] = '\0';
                                                                            *ino = de.d_inode;
                                                                            *type = de.d_type;
                                                                            *pos += de.d_reclen;
                                                                            return 1;
                                                                        }
                                                                        return 0;
                                                                                       }

                                                                                       uint32_t berfs_dir_lookup(struct berfs_mount *m, const struct berfs_inode *dir, const char *name) {
                                                                                           uint32_t pos = 0, ino;
                                                                                           char buf[BERFS_NAME_MAX + 1];
                                                                                           uint8_t type;
                                                                                           while (berfs_dir_next(m, dir, &pos, &ino, buf, &type) == 1) {
                                                                                               if (strcmp(buf, name) == 0) return ino;
                                                                                           }
                                                                                           return 0;
                                                                                       }

                                                                                       int berfs_dir_add(struct berfs_mount *m, struct berfs_inode *dir_inode,
                                                                                                         uint32_t dir_ino, const char *name, uint32_t ino, uint8_t type) {
                                                                                           size_t name_len = strlen(name);
                                                                                           if (name_len == 0 || name_len > BERFS_NAME_MAX) return -1;

                                                                                           uint16_t reclen = (uint16_t)(sizeof(struct berfs_dirent) + name_len);
                                                                                           reclen = (uint16_t)((reclen + 3u) & ~3u);

                                                                                           struct berfs_dirent de;
                                                                                           de.d_inode    = ino;
                                                                                           de.d_reclen   = reclen;
                                                                                           de.d_name_len = (uint8_t)name_len;
                                                                                           de.d_type     = type;

                                                                                           uint32_t pos = dir_inode->i_size;
                                                                                           uint8_t *tmp = kmalloc(reclen);
                                                                                           if (!tmp) return -1;
                                                                                           memset(tmp, 0, reclen);
                                                                                           memcpy(tmp, &de, sizeof(de));
                                                                                           memcpy(tmp + sizeof(de), name, name_len);

                                                                                           int rc = berfs_file_write(m, dir_inode, pos, tmp, reclen);
                                                                                           kfree(tmp);
                                                                                           if (rc != 0) return -1;
                                                                                           if (berfs_write_inode(m, dir_ino, dir_inode) != 0) return -1;
                                                                                           return 0;
                                                                                                         }

                                                                                                         int berfs_dir_remove(struct berfs_mount *m, struct berfs_inode *dir_inode,
                                                                                                                              uint32_t dir_ino, const char *name) {
                                                                                                             uint32_t pos = 0;
                                                                                                             while (pos < dir_inode->i_size) {
                                                                                                                 uint32_t start_pos = pos;
                                                                                                                 struct berfs_dirent de;
                                                                                                                 if (file_read_bytes(m, dir_inode, pos, &de, sizeof(de)) != 0) return -1;
                                                                                                                 if (de.d_inode == 0) { pos += de.d_reclen; continue; }

                                                                                                                 char entry_name[BERFS_NAME_MAX + 1];
                                                                                                                 if (file_read_bytes(m, dir_inode, pos + sizeof(de), entry_name, de.d_name_len) != 0) return -1;
                                                                                                                 entry_name[de.d_name_len] = '\0';

                                                                                                                 if (strcmp(entry_name, name) == 0) {
                                                                                                                     uint32_t zero = 0;
                                                                                                                     if (berfs_file_write(m, dir_inode, start_pos, &zero, 4) != 0) return -1;
                                                                                                                     if (berfs_write_inode(m, dir_ino, dir_inode) != 0) return -1;
                                                                                                                     return 0;
                                                                                                                 }
                                                                                                                 pos += de.d_reclen;
                                                                                                             }
                                                                                                             return -2;
                                                                                                                              }

                                                                                                                              int berfs_free_block(struct berfs_mount *m, uint32_t blk) {
                                                                                                                                  if (blk < m->sb.sb_data_block || blk >= m->sb.sb_total_blocks) return -1;
                                                                                                                                  uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                                                                                                  if (!buf) return -1;
                                                                                                                                  uint32_t bitmap_start = m->sb.sb_block_bitmap_block;
                                                                                                                                  uint32_t byte_idx = blk / 8;
                                                                                                                                  uint32_t bit_idx = blk % 8;

                                                                                                                                  berfs_read_block(m, bitmap_start + (byte_idx / BLOCK_SIZE), buf);
                                                                                                                                  uint32_t off = byte_idx % BLOCK_SIZE;
                                                                                                                                  buf[off] &= ~(1 << bit_idx);
                                                                                                                                  bdev_write(bitmap_start + (byte_idx / BLOCK_SIZE), buf);

                                                                                                                                  m->sb.sb_free_blocks++;
                                                                                                                                  berfs_write_sb(m);
                                                                                                                                  kfree(buf);
                                                                                                                                  return 0;
                                                                                                                              }

                                                                                                                              int berfs_free_inode(struct berfs_mount *m, uint32_t ino) {
                                                                                                                                  if (ino < 2 || ino >= m->sb.sb_total_inodes) return -1;
                                                                                                                                  uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                                                                                                  if (!buf) return -1;
                                                                                                                                  uint32_t bitmap_start = m->sb.sb_inode_bitmap_block;
                                                                                                                                  uint32_t byte_idx = ino / 8;
                                                                                                                                  uint32_t bit_idx = ino % 8;

                                                                                                                                  berfs_read_block(m, bitmap_start, buf);
                                                                                                                                  buf[byte_idx] &= ~(1 << bit_idx);
                                                                                                                                  bdev_write(bitmap_start, buf);

                                                                                                                                  m->sb.sb_free_inodes++;
                                                                                                                                  berfs_write_sb(m);
                                                                                                                                  kfree(buf);
                                                                                                                                  return 0;
                                                                                                                              }

                                                                                                                              int berfs_sync(struct berfs_mount *m) {
                                                                                                                                  (void)m;
                                                                                                                                  return 0;
                                                                                                                              }

                                                                                                                              void berfs_truncate(struct berfs_mount *m, struct berfs_inode *in) {
                                                                                                                                  // 1. Освобождаем прямые блоки
                                                                                                                                  for (int i = 0; i < BERFS_DIRECT_BLOCKS; i++) {
                                                                                                                                      if (in->i_block[i] != 0) {
                                                                                                                                          berfs_free_block(m, in->i_block[i]);
                                                                                                                                          in->i_block[i] = 0;
                                                                                                                                      }
                                                                                                                                  }

                                                                                                                                  uint32_t ptrs_per_block = BLOCK_SIZE / sizeof(uint32_t);
                                                                                                                                  uint8_t *buf = kmalloc(BLOCK_SIZE);
                                                                                                                                  if (!buf) return;

                                                                                                                                  // 2. Освобождаем одинарный indirect
                                                                                                                                  if (in->i_indirect != 0) {
                                                                                                                                      bdev_read(in->i_indirect, buf);
                                                                                                                                      uint32_t *ptrs = (uint32_t *)buf;
                                                                                                                                      for (uint32_t i = 0; i < ptrs_per_block; i++) {
                                                                                                                                          if (ptrs[i] != 0) berfs_free_block(m, ptrs[i]);
                                                                                                                                      }
                                                                                                                                      berfs_free_block(m, in->i_indirect);
                                                                                                                                      in->i_indirect = 0;
                                                                                                                                  }

                                                                                                                                  // 3. Двойной indirect
                                                                                                                                  if (in->i_double_indirect != 0) {
                                                                                                                                      bdev_read(in->i_double_indirect, buf);
                                                                                                                                      uint32_t *ptrs1 = (uint32_t *)buf;
                                                                                                                                      for (uint32_t i = 0; i < ptrs_per_block; i++) {
                                                                                                                                          if (ptrs1[i] != 0) {
                                                                                                                                              uint8_t *buf2 = kmalloc(BLOCK_SIZE);
                                                                                                                                              if (buf2) {
                                                                                                                                                  bdev_read(ptrs1[i], buf2);
                                                                                                                                                  uint32_t *ptrs2 = (uint32_t *)buf2;
                                                                                                                                                  for (uint32_t j = 0; j < ptrs_per_block; j++) {
                                                                                                                                                      if (ptrs2[j] != 0) berfs_free_block(m, ptrs2[j]);
                                                                                                                                                  }
                                                                                                                                                  kfree(buf2);
                                                                                                                                              }
                                                                                                                                              berfs_free_block(m, ptrs1[i]);
                                                                                                                                          }
                                                                                                                                      }
                                                                                                                                      berfs_free_block(m, in->i_double_indirect);
                                                                                                                                      in->i_double_indirect = 0;
                                                                                                                                  }

                                                                                                                                  // 4. Тройной indirect (пока просто освобождаем, если есть)
                                                                                                                                  if (in->i_triple_indirect != 0) {
                                                                                                                                      berfs_free_block(m, in->i_triple_indirect);
                                                                                                                                      in->i_triple_indirect = 0;
                                                                                                                                  }

                                                                                                                                  kfree(buf);
                                                                                                                                  in->i_size = 0;
                                                                                                                                  in->i_blocks = 0;
                                                                                                                              }
