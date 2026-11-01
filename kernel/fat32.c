#include <stdint.h>
#include "ata.h"
#include "kstring.h"
#include "heap.h"
#include "fat32.h"

struct fat32_bpb {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed));

struct fat32_dirent {
    char     name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} __attribute__((packed));

static uint32_t fat_start_lba = 0;
static uint32_t data_start_lba = 0;
static uint8_t  sectors_per_cluster = 0;
static uint32_t root_cluster = 0;
static int      fat32_ready = 0;

extern void term_write(const char *s);
extern void term_putchar(char c);

int fat32_init(void) {
    uint8_t buf[512];
    int r = ata_read_slave(0, 1, buf);
    if (r != 0) return r;

    struct fat32_bpb *bpb = (struct fat32_bpb *)buf;

    if (bpb->bytes_per_sector != 512) return -10;
    if (bpb->fs_type[0] != 'F' || bpb->fs_type[3] != '3') return -11;

    sectors_per_cluster = bpb->sectors_per_cluster;
    root_cluster = bpb->root_cluster;

    fat_start_lba = bpb->reserved_sectors;
    data_start_lba = fat_start_lba + (bpb->num_fats * bpb->fat_size_32);

    fat32_ready = 1;
    return 0;
}

void fat32_info(void) {
    if (!fat32_ready) {
        term_write("[ FAIL ] FAT32 not mounted\n");
        return;
    }

    uint8_t buf[512];
    ata_read_slave(0, 1, buf);
    struct fat32_bpb *bpb = (struct fat32_bpb *)buf;

    term_write("[ FAT32 INFO ]\n");
    term_write("  OEM: ");
    for(int i=0; i<8; i++) term_putchar(bpb->oem[i]);
    term_putchar('\n');

    term_write("  VolLabel: ");
    for(int i=0; i<11; i++) term_putchar(bpb->volume_label[i]);
    term_putchar('\n');
}

static uint32_t fat32_get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t buf[512];
    if (ata_read_slave(fat_sector, 1, buf) != 0) return 0x0FFFFFFF;

    uint32_t next = *(uint32_t *)(buf + ent_offset);
    return next & 0x0FFFFFFF;
}

void fat32_ls(void) {
    if (!fat32_ready) {
        term_write("[ FAIL ] FAT32 not initialized\n");
        return;
    }

    term_write("FAT32 Root Directory:\n");

    uint32_t current_cluster = root_cluster;
    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) {
        term_write("[ FAIL ] kmalloc\n");
        return;
    }

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;

        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) {
            term_write("[ FAIL ] read cluster\n");
            break;
        }

        struct fat32_dirent *dir = (struct fat32_dirent *)cluster_buf;
        int entries = (sectors_per_cluster * 512) / sizeof(struct fat32_dirent);

        for (int i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return;
            }
            if ((uint8_t)dir[i].name[0] == 0xE5) {
                continue;
            }
            if (dir[i].attr == 0x0F) {
                continue;
            }

            if (dir[i].attr & 0x10) term_putchar('d'); else term_putchar('-');
            term_putchar(' ');

            for (int j = 0; j < 11; j++) {
                if (j == 8) term_putchar('.');
                if (dir[i].name[j] == ' ') continue;
                term_putchar(dir[i].name[j]);
            }
            term_write("\n");
        }

        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    kfree(cluster_buf);
}

int fat32_read(const char *filename, void *buffer, uint32_t max_size) {
    if (!fat32_ready) return -1;

    uint32_t current_cluster = root_cluster;
    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) return -1;

    uint32_t file_size = 0;
    uint32_t start_cluster = 0;
    int found = 0;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        struct fat32_dirent *dir = (struct fat32_dirent *)cluster_buf;
        int entries = (sectors_per_cluster * 512) / sizeof(struct fat32_dirent);

        for (int i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) { kfree(cluster_buf); return -1; }
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;
            if (dir[i].attr == 0x0F) continue;

            char name8[9]; char ext3[4];
            int n8 = 0, e3 = 0;
            for(int j=0; j<8; j++) if(dir[i].name[j] != ' ') name8[n8++] = dir[i].name[j];
            name8[n8] = '\0';
            for(int j=8; j<11; j++) if(dir[i].name[j] != ' ') ext3[e3++] = dir[i].name[j];
            ext3[e3] = '\0';

            char full_name[16];
            if (e3 > 0) {
                int f=0;
                for(int j=0; j<n8; j++) full_name[f++]=name8[j];
                full_name[f++]='.';
                for(int j=0; j<e3; j++) full_name[f++]=ext3[j];
                full_name[f]='\0';
            } else {
                for(int j=0; j<n8; j++) full_name[j]=name8[j];
                full_name[n8]='\0';
            }

            if (strcmp(full_name, filename) == 0 || strcmp(name8, filename) == 0) {
                file_size = dir[i].file_size;
                start_cluster = dir[i].fst_clus_lo | (dir[i].fst_clus_hi << 16);
                found = 1;
                break;
            }
        }
        if (found) break;
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    if (!found || start_cluster < 2) {
        kfree(cluster_buf);
        return -1;
    }

    uint32_t bytes_read = 0;
    uint32_t to_read = (file_size > max_size) ? max_size : file_size;
    current_cluster = start_cluster;
    uint8_t *dst = (uint8_t *)buffer;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2 && bytes_read < to_read) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        uint32_t chunk = sectors_per_cluster * 512;
        if (bytes_read + chunk > to_read) chunk = to_read - bytes_read;

        memcpy(dst + bytes_read, cluster_buf, chunk);
        bytes_read += chunk;
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    kfree(cluster_buf);
    return bytes_read;
}

// Читаем файл, начиная с определенного кластера
static int f32_read(struct vfs_mount *m, uint32_t ino, uint32_t pos, void *buf, uint32_t len) {
    (void)m;
    if (ino < 2) return -1;

    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) return -1;

    uint32_t current_cluster = ino;
    uint32_t bytes_read = 0;
    uint32_t skip = pos;
    uint8_t *dst = (uint8_t *)buf;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2 && bytes_read < len) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        uint32_t chunk = sectors_per_cluster * 512;

        if (skip >= chunk) {
            skip -= chunk;
        } else {
            uint32_t offset_in_cluster = skip;
            uint32_t to_copy = chunk - offset_in_cluster;
            if (to_copy > len - bytes_read) to_copy = len - bytes_read;

            memcpy(dst + bytes_read, cluster_buf + offset_in_cluster, to_copy);
            bytes_read += to_copy;
            skip = 0;
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    kfree(cluster_buf);
    return bytes_read;
}

// Ищем файл в директории (dir_ino = кластер директории)
static int f32_lookup(struct vfs_mount *m, uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    (void)m;
    uint32_t current_cluster = dir_ino;
    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) return -1;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        struct fat32_dirent *dir = (struct fat32_dirent *)cluster_buf;
        int entries = (sectors_per_cluster * 512) / sizeof(struct fat32_dirent);

        for (int i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) { kfree(cluster_buf); return -1; }
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;
            if (dir[i].attr == 0x0F) continue;

            char name8[9]; char ext3[4];
            int n8 = 0, e3 = 0;
            for(int j=0; j<8; j++) if(dir[i].name[j] != ' ') name8[n8++] = dir[i].name[j];
            name8[n8] = '\0';
            for(int j=8; j<11; j++) if(dir[i].name[j] != ' ') ext3[e3++] = dir[i].name[j];
            ext3[e3] = '\0';

            char full_name[16];
            if (e3 > 0) {
                int f=0;
                for(int j=0; j<n8; j++) full_name[f++]=name8[j];
                full_name[f++]='.';
                for(int j=0; j<e3; j++) full_name[f++]=ext3[j];
                full_name[f]='\0';
            } else {
                for(int j=0; j<n8; j++) full_name[j]=name8[j];
                full_name[n8]='\0';
            }

            if (strcmp(full_name, name) == 0 || strcmp(name8, name) == 0) {
                *out_ino = dir[i].fst_clus_lo | (dir[i].fst_clus_hi << 16);
                kfree(cluster_buf);
                return 0;
            }
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    kfree(cluster_buf);
    return -2;
}

// Читаем директорию (для ls)
static int f32_readdir(struct vfs_mount *m, uint32_t dir_ino, uint32_t *pos, char *name, uint32_t *out_ino, uint8_t *type) {
    (void)m;
    uint32_t current_cluster = dir_ino;
    uint32_t skip = *pos; // Количество записей для пропуска
    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) return -1;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        struct fat32_dirent *dir = (struct fat32_dirent *)cluster_buf;
        int entries = (sectors_per_cluster * 512) / sizeof(struct fat32_dirent);

        for (int i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) { kfree(cluster_buf); return 0; } // Конец
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;
            if (dir[i].attr == 0x0F || dir[i].attr == 0x08) continue; // LFN & VolID

            // Пропускаем . и ..
            if (dir[i].name[0] == '.' && (dir[i].name[1] == ' ' || dir[i].name[1] == '.')) continue;

            if (skip > 0) { skip--; continue; }

            int f = 0;
            for(int j=0; j<8; j++) if(dir[i].name[j] != ' ') name[f++] = dir[i].name[j];
            if (dir[i].name[8] != ' ') {
                name[f++] = '.';
                for(int j=8; j<11; j++) if(dir[i].name[j] != ' ') name[f++] = dir[i].name[j];
            }
            name[f] = '\0';

            *out_ino = dir[i].fst_clus_lo | (dir[i].fst_clus_hi << 16);
            *type = (dir[i].attr & 0x10) ? VFS_DIR : VFS_FILE;
            *pos = *pos + 1;
            kfree(cluster_buf);
            return 1;
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    kfree(cluster_buf);
    return 0;
}

// Получаем размер и тип файла
static int f32_stat(struct vfs_mount *m, uint32_t ino, uint32_t *size, uint8_t *type) {
    (void)m;
    if (ino == root_cluster) {
        *size = 0;
        *type = VFS_DIR;
        return 0;
    }

    uint32_t current_cluster = root_cluster;
    uint8_t *cluster_buf = kmalloc(sectors_per_cluster * 512);
    if (!cluster_buf) return -1;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t cluster_lba = data_start_lba + (current_cluster - 2) * sectors_per_cluster;
        if (ata_read_slave(cluster_lba, sectors_per_cluster, cluster_buf) != 0) break;

        struct fat32_dirent *dir = (struct fat32_dirent *)cluster_buf;
        int entries = (sectors_per_cluster * 512) / sizeof(struct fat32_dirent);

        for (int i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) { kfree(cluster_buf); return -1; }
            if ((uint8_t)dir[i].name[0] == 0xE5) continue;
            if (dir[i].attr == 0x0F || dir[i].attr == 0x08) continue; // Skip LFN & Volume ID

            uint32_t cluster = dir[i].fst_clus_lo | (dir[i].fst_clus_hi << 16);
            if (cluster == ino) {
                *size = dir[i].file_size;
                *type = (dir[i].attr & 0x10) ? VFS_DIR : VFS_FILE;
                kfree(cluster_buf);
                return 0;
            }
        }
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    kfree(cluster_buf);
    return -1;
}

static struct vfs_ops fat32_ops = {
    .read    = f32_read,
    .lookup  = f32_lookup,
    .readdir = f32_readdir,
    .stat    = f32_stat,
};

struct vfs_ops *fat32_get_vfs_ops(void) { return &fat32_ops; }
uint32_t fat32_get_root_cluster(void) { return root_cluster; }
