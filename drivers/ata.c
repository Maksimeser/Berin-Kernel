/* Berin OS - ATA PIO driver (x86_64, primary master, LBA28) */

#include "ata.h"
#include "io.h"

#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6

#define ATA_REG_DATA      0
#define ATA_REG_FEAT      1
#define ATA_REG_SECCOUNT  2
#define ATA_REG_LBA_LO    3
#define ATA_REG_LBA_MID   4
#define ATA_REG_LBA_HI    5
#define ATA_REG_DRIVE     6
#define ATA_REG_CMD       7
#define ATA_REG_STATUS    7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static uint32_t g_sector_count = 0;
static char     g_model[41];

static void ata_io_wait(void) {
    for (int i = 0; i < 4; i++) (void)inb(ATA_PRIMARY_CTRL);
}

static int ata_wait_ready(int check_drq) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DF)  return -2;
        if ((st & ATA_SR_BSY) == 0) {
            if (!check_drq) return 0;
            if (st & ATA_SR_DRQ) return 0;
        }
        __asm__ __volatile__("pause");
    }
    return -3;
}

int ata_init(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    ata_io_wait();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   0);
    outb(ATA_PRIMARY_IO + ATA_REG_CMD, 0xEC);
    ata_io_wait();

    uint8_t st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (st == 0) return -1;

    if (ata_wait_ready(0) != 0) return -2;

    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);

    g_sector_count = ((uint32_t)id[61] << 16) | id[60];

    for (int i = 0; i < 20; i++) {
        g_model[i * 2]     = (char)(id[27 + i] >> 8);
        g_model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    g_model[40] = 0;
    for (int i = 39; i >= 0 && g_model[i] == ' '; i--) g_model[i] = 0;

    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (ata_wait_ready(0) != 0) return -1;

    // 0xE0 = Primary Master
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE,    (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_PRIMARY_IO + ATA_REG_FEAT,     0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_CMD, 0x20);
    ata_io_wait();

    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_ready(1) != 0) return -2;
        for (int i = 0; i < 256; i++)
            *p++ = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (ata_wait_ready(0) != 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE,    (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_PRIMARY_IO + ATA_REG_FEAT,     0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_CMD, 0x30);
    ata_io_wait();

    const uint16_t *p = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_ready(1) != 0) return -2;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, *p++);
    }
    return 0;
}

int ata_read_slave(uint32_t lba, uint8_t count, void *buf) {
    // 1. Выбираем Primary Slave (0xB0)
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, (uint8_t)(0xF0 | ((lba >> 24) & 0x0F)));
    ata_io_wait(); // Ждем 400 нс, чтобы контроллер переключился

    if (ata_wait_ready(0) != 0) return -1;

    // 2. Отправляем команду READ SECTORS
    outb(ATA_PRIMARY_IO + ATA_REG_FEAT,     0);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_CMD, 0x20);
    ata_io_wait();

    // 3. Читаем данные
    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_ready(1) != 0) return -2;
        for (int i = 0; i < 256; i++)
            *p++ = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    return 0;
}

uint32_t ata_sector_count(void) { return g_sector_count; }
const char *ata_model(void)     { return g_model; }
