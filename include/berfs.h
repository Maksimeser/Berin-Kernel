#ifndef BERFS_H
#define BERFS_H

/* BerFS v1 - on-disk format
 * Shared between kernel and host tools (bformat, bfsck).
 *
 * Layout:
 *   block 0        : boot block (reserved, 4096 bytes, zeroed)
 *   block 1        : superblock
 *   block 2..N     : inode bitmap, block bitmap, inode table, data
 *
 * Sizes:
 *   BLOCK_SIZE  = 4096
 *   SECTOR_SIZE = 512
 *   SECTORS_PER_BLOCK = 8
 *
 * All integers on-disk are little-endian.
 */

#include <stdint.h>
#include <stddef.h>

#define BERFS_MAGIC          0x53465242u   /* "BRFS" little-endian */
#define BERFS_VERSION        1
#define BERFS_BLOCK_SIZE     4096u
#define BERFS_SECTOR_SIZE    512u
#define BERFS_SECTORS_PER_BLOCK (BERFS_BLOCK_SIZE / BERFS_SECTOR_SIZE)

#define BERFS_ROOT_INODE     1
#define BERFS_NAME_MAX       255
#define BERFS_DIRECT_BLOCKS  12            /* direct block pointers per inode */
#define BERFS_MAX_INODES     4096

/* feature flags in superblock */
#define BERFS_FEATURE_JOURNAL  (1u << 0)   /* v2 */
#define BERFS_FEATURE_COW      (1u << 1)   /* v2 */

/* inode types (i_mode high bits) */
#define BERFS_TYPE_FILE       0x8000u
#define BERFS_TYPE_DIR        0x4000u
#define BERFS_TYPE_SYMLINK    0xA000u

/* ---------------------------------------------------------------- */

/* Superblock - lives at block 1 */
struct berfs_superblock {
    uint32_t sb_magic;           /* BERFS_MAGIC */
    uint32_t sb_version;         /* BERFS_VERSION */
    uint32_t sb_feature_flags;   /* bitmask of BERFS_FEATURE_* */
    uint32_t sb_block_size;      /* 4096 */
    uint32_t sb_total_blocks;    /* total blocks in the FS */
    uint32_t sb_total_inodes;    /* total inodes in the table */
    uint32_t sb_free_blocks;     /* cached count of free blocks */
    uint32_t sb_free_inodes;     /* cached count of free inodes */
    uint32_t sb_root_inode;      /* always BERFS_ROOT_INODE */
    uint32_t sb_inode_bitmap_block; /* first block of inode bitmap */
    uint32_t sb_block_bitmap_block; /* first block of block bitmap */
    uint32_t sb_inode_table_block;  /* first block of inode table */
    uint32_t sb_data_block;         /* first data block */
    uint64_t sb_mount_time;         /* UNIX time, 0 = not mounted */
    uint64_t sb_write_time;         /* last write */
    uint32_t sb_mount_count;        /* how many times mounted */
    uint32_t sb_max_mount;          /* max mount count before fsck */
    uint8_t  sb_uuid[16];           /* filesystem UUID */
    uint8_t  sb_reserved[932];      /* pad to 1024 */
} __attribute__((packed));

/* 1024 - 36 = 988 bytes reserved -> total 1024 bytes superblock,
 * so we can copy it to offset 1024 (classic ext2 layout),
 * but we'll keep it at block 1 (offset 4096) in BerFS v1 */

_Static_assert(sizeof(struct berfs_superblock) == 1024,
               "berfs_superblock must be 1024 bytes");
_Static_assert(offsetof(struct berfs_superblock, sb_magic) == 0,
               "sb_magic must be at offset 0");
_Static_assert(offsetof(struct berfs_superblock, sb_mount_time) == 52,
               "sb_mount_time must be at offset 52");
_Static_assert(offsetof(struct berfs_superblock, sb_reserved) == 92,
               "sb_reserved must be at offset 92");

/* ---------------------------------------------------------------- */

/* Inode - 128 bytes each, matches ext2 for familiarity */
struct berfs_inode {
    uint16_t i_mode;             /* type + permissions */
    uint16_t i_uid;              /* owner user id */
    uint32_t i_size;             /* size in bytes (file) or block count (dir) */
    uint32_t i_atime;            /* access time */
    uint32_t i_mtime;            /* modification time */
    uint32_t i_ctime;            /* creation time */
    uint32_t i_links_count;      /* hard links */
    uint32_t i_blocks;           /* number of 512-byte sectors allocated */
    uint32_t i_flags;            /* reserved */
    uint32_t i_reserved1;        /* reserved */
    uint32_t i_block[BERFS_DIRECT_BLOCKS]; /* direct block pointers */
    uint32_t i_indirect;         /* single indirect block (v2) */
    uint32_t i_double_indirect;  /* double indirect (v2) */
    uint32_t i_triple_indirect;  /* triple indirect (v2) */
    uint32_t i_generation;       /* for NFS (unused) */
    uint32_t i_reserved2[7];     /* pad to 128 */
} __attribute__((packed));

_Static_assert(sizeof(struct berfs_inode) == 128,
               "berfs_inode must be 128 bytes");

/* ---------------------------------------------------------------- */

/* Directory entry — variable-length, name follows */
struct berfs_dirent {
    uint32_t d_inode;            /* 0 = empty slot */
    uint16_t d_reclen;           /* total size of this entry, multiple of 4 */
    uint8_t  d_name_len;         /* length of name (without NUL) */
    uint8_t  d_type;             /* 0=unknown, 1=file, 2=dir */
    /* char d_name[d_name_len]; followed by padding to d_reclen */
} __attribute__((packed));

_Static_assert(sizeof(struct berfs_dirent) == 8,
               "berfs_dirent must be 8 bytes");

/* d_type values */
#define BERFS_DT_UNKNOWN  0
#define BERFS_DT_FILE     1
#define BERFS_DT_DIR      2

/* ---------------------------------------------------------------- */

/* Helpers for on-disk layout calculation (used by bformat and kernel) */

struct berfs_layout {
    uint32_t total_blocks;
    uint32_t inode_bitmap_blocks;
    uint32_t block_bitmap_blocks;
    uint32_t inode_table_blocks;
    uint32_t data_start_block;
};

/* Calculate layout from desired total block count and inode count.
 * Writes result into *out. Returns 0 on success. */
static inline int berfs_calc_layout(uint32_t total_blocks,
                                    uint32_t total_inodes,
                                    struct berfs_layout *out)
{
    if (total_blocks < 16)     return -1;
    if (total_inodes == 0)     return -1;

    uint32_t inode_bitmap_blocks =
    (total_inodes + 8 * BERFS_BLOCK_SIZE - 1) / (8 * BERFS_BLOCK_SIZE);
    uint32_t block_bitmap_blocks =
    (total_blocks + 8 * BERFS_BLOCK_SIZE - 1) / (8 * BERFS_BLOCK_SIZE);
    uint32_t inodes_per_block = BERFS_BLOCK_SIZE / sizeof(struct berfs_inode);
    uint32_t inode_table_blocks =
    (total_inodes + inodes_per_block - 1) / inodes_per_block;

    uint32_t data_start =
    2 + inode_bitmap_blocks + block_bitmap_blocks + inode_table_blocks;
    if (data_start >= total_blocks) return -1;

    out->total_blocks        = total_blocks;
    out->inode_bitmap_blocks = inode_bitmap_blocks;
    out->block_bitmap_blocks = block_bitmap_blocks;
    out->inode_table_blocks  = inode_table_blocks;
    out->data_start_block    = data_start;
    return 0;
}

#endif /* BERFS_H */
