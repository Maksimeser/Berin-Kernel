/* Berin - physical memory manager (x86_64)
 * Bitmap, 4K pages. RAM size hardcoded for now (128 MB from qemu -m 128M).
 * Free region starts right after kernel end (_kernel_end).
 */

#include "pmm.h"

extern uint8_t _kernel_end[];

static uint8_t  bitmap[PMM_BITMAP_BYTES];
static uint64_t total_pages;
static uint64_t free_pages;

static inline void bit_set(uint64_t i)   { bitmap[i >> 3] |=  (uint8_t)(1u << (i & 7)); }
static inline void bit_clear(uint64_t i) { bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static inline int  bit_get(uint64_t i)   { return (bitmap[i >> 3] >> (i & 7)) & 1u; }

#define HEAP_SIZE_BYTES (8ULL * 1024ULL * 1024ULL)
void pmm_init(void) {
    for (uint64_t i = 0; i < PMM_BITMAP_BYTES; i++) bitmap[i] = 0xFF;

    total_pages = PMM_MAX_PAGES;
    free_pages  = 0;

    // Защищаем всё до конца heap (ядро + .bss + .data + heap 4MB)
    uint64_t heap_end = (uint64_t)_kernel_end + HEAP_SIZE_BYTES;
    uint64_t start_page = (heap_end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t p = start_page; p < total_pages; p++) {
        bit_clear(p);
        free_pages++;
    }
}

void *pmm_alloc_page(void) {
    for (uint64_t p = 0; p < total_pages; p++) {
        if (!bit_get(p)) {
            bit_set(p);
            free_pages--;
            return (void *)(p * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *page) {
    uint64_t p = (uint64_t)page / PAGE_SIZE;
    if (p >= total_pages) return;
    if (bit_get(p)) {
        bit_clear(p);
        free_pages++;
    }
}

uint64_t pmm_total_pages(void) { return total_pages; }
uint64_t pmm_free_pages(void)  { return free_pages;  }
uint64_t pmm_used_pages(void)  { return total_pages - free_pages; }
