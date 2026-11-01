#ifndef BERIN_PMM_H
#define BERIN_PMM_H

#include <stdint.h>

#define PAGE_SIZE     4096UL
#define PMM_MEM_BYTES (128UL * 1024UL * 1024UL)
#define PMM_MAX_PAGES (PMM_MEM_BYTES / PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_PAGES / 8)

void     pmm_init(void);
void    *pmm_alloc_page(void);
void     pmm_free_page(void *page);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_used_pages(void);

#endif
