#include "vmm.h"
#include "pmm.h"
#include "kstring.h"

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFF;
}

void vmm_switch_pml4(uint64_t *pml4) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pml4));
}

uint64_t *vmm_create_pml4(void) {
    uint64_t *new_pml4 = (uint64_t *)pmm_alloc_page();
    if (!new_pml4) return 0;
    memset(new_pml4, 0, PAGE_SIZE);

    uint64_t *old_pml4 = (uint64_t *)read_cr3();

    // 1. Копируем верхнюю половину (ядро) как есть
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = old_pml4[i];
    }

    // 2. Глубоко копируем нижнюю половину (0..255) до уровня PT
    for (int i = 0; i < 256; i++) {
        if (old_pml4[i] & PTE_PRESENT) {
            uint64_t *old_pdpt = (uint64_t *)(old_pml4[i] & ~0xFFF);
            uint64_t *new_pdpt = (uint64_t *)pmm_alloc_page();
            if (!new_pdpt) return 0;
            memset(new_pdpt, 0, PAGE_SIZE);
            new_pml4[i] = (uint64_t)new_pdpt | (old_pml4[i] & 0xFFF);

            for (int j = 0; j < 512; j++) {
                if (old_pdpt[j] & PTE_PRESENT) {
                    uint64_t *old_pd = (uint64_t *)(old_pdpt[j] & ~0xFFF);
                    uint64_t *new_pd = (uint64_t *)pmm_alloc_page();
                    if (!new_pd) return 0;
                    memset(new_pd, 0, PAGE_SIZE);
                    new_pdpt[j] = (uint64_t)new_pd | (old_pdpt[j] & 0xFFF);

                    for (int k = 0; k < 512; k++) {
                        if (old_pd[k] & PTE_PRESENT) {
                            if (old_pd[k] & (1ULL << 7)) {
                                // Huge page (2MB). Копируем как есть,
                                // (если vmm_map_page потом захочет разбить - он это сделает локально)
                                new_pd[k] = old_pd[k];
                            } else {
                                // Page Table (4KB). Копируем её целиком!
                                uint64_t *old_pt = (uint64_t *)(old_pd[k] & ~0xFFF);
                                uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
                                if (!new_pt) return 0;
                                memcpy(new_pt, old_pt, PAGE_SIZE);
                                new_pd[k] = (uint64_t)new_pt | (old_pd[k] & 0xFFF);
                            }
                        }
                    }
                }
            }
        }
    }
    return new_pml4;
}

uint64_t vmm_get_phys(uint64_t *pml4, uint64_t vaddr) {
    // ... (оставь свою текущую реализацию, она рабочая) ...
    uint64_t idx_pml4 = (vaddr >> 39) & 0x1FF;
    uint64_t idx_pdpt = (vaddr >> 30) & 0x1FF;
    uint64_t idx_pd   = (vaddr >> 21) & 0x1FF;
    uint64_t idx_pt   = (vaddr >> 12) & 0x1FF;

    if (!(pml4[idx_pml4] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[idx_pml4] & ~0xFFF);
    if (!(pdpt[idx_pdpt] & PTE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[idx_pdpt] & ~0xFFF);
    if (!(pd[idx_pd] & PTE_PRESENT)) return 0;
    if (pd[idx_pd] & (1ULL << 7)) return (pd[idx_pd] & ~0x1FFFFF) + (vaddr & 0x1FFFFF);
    uint64_t *pt = (uint64_t *)(pd[idx_pd] & ~0xFFF);
    if (!(pt[idx_pt] & PTE_PRESENT)) return 0;
    return (pt[idx_pt] & ~0xFFF) + (vaddr & 0xFFF);
}

int vmm_map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t idx_pml4 = (vaddr >> 39) & 0x1FF;
    uint64_t idx_pdpt = (vaddr >> 30) & 0x1FF;
    uint64_t idx_pd   = (vaddr >> 21) & 0x1FF;
    uint64_t idx_pt   = (vaddr >> 12) & 0x1FF;

    // PML4 -> PDPT
    if (!(pml4[idx_pml4] & PTE_PRESENT)) {
        uint64_t *new_pdpt = (uint64_t *)pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset(new_pdpt, 0, PAGE_SIZE);
        pml4[idx_pml4] = (uint64_t)new_pdpt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[idx_pml4] & ~0xFFF);

    // PDPT -> PD
    if (!(pdpt[idx_pdpt] & PTE_PRESENT)) {
        uint64_t *new_pd = (uint64_t *)pmm_alloc_page();
        if (!new_pd) return -1;
        memset(new_pd, 0, PAGE_SIZE);
        pdpt[idx_pdpt] = (uint64_t)new_pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pd = (uint64_t *)(pdpt[idx_pdpt] & ~0xFFF);

    // PD -> PT
    if (!(pd[idx_pd] & PTE_PRESENT)) {
        uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
        if (!new_pt) return -1;
        memset(new_pt, 0, PAGE_SIZE);
        pd[idx_pd] = (uint64_t)new_pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else if (pd[idx_pd] & (1ULL << 7)) {
        // Разбиваем 2MB huge page
        uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
        if (!new_pt) return -1;
        uint64_t huge_phys = pd[idx_pd] & ~0x1FFFFFULL;
        // ВАЖНО: Берем флаги из pd, но ГАРАНТИРУЕМ User бит
        uint64_t old_flags = (pd[idx_pd] & 0xFFF) | (flags & PTE_USER);

        for (int i = 0; i < 512; i++) {
            new_pt[i] = (huge_phys + i * PAGE_SIZE) | old_flags;
        }
        pd[idx_pd] = (uint64_t)new_pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pt = (uint64_t *)(pd[idx_pd] & ~0xFFF);
    pt[idx_pt] = paddr | flags;
    __asm__ __volatile__("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}
