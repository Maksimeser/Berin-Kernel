#ifndef BOOT_INFO_H
#define BOOT_INFO_H
#include <stdint.h>

#define BOOT_INFO_MAGIC 0x4245524E // "BERN"

struct boot_info {
    uint32_t magic;              // BOOT_INFO_MAGIC
    uint64_t fb_base;            // Адрес видеопамяти
    uint32_t fb_width;           // Ширина экрана
    uint32_t fb_height;          // Высота экрана
    uint32_t fb_pitch;           // Пикселей в строке
    uint32_t fb_bpp;             // Бит на пиксель
} __attribute__((packed));

typedef struct boot_info boot_info_t;

#endif
