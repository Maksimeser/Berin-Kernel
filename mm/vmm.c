#include "kdebug.h"
#include "vmm.h"
#include "pmm.h"
#include "kstring.h"

extern void term_write(const char *s);

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFF;
}

void vmm_switch_pml4(uint64_t *pml4) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pml4));
}

uint64_t vmm_get_phys(uint64_t *pml4, uint64_t vaddr) {
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

    if (!(pml4[idx_pml4] & PTE_PRESENT)) {
        uint64_t *new_pdpt = (uint64_t *)pmm_alloc_page();
        if (!new_pdpt) return -1;
        memset(new_pdpt, 0, PAGE_SIZE);
        pml4[idx_pml4] = (uint64_t)new_pdpt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[idx_pml4] & ~0xFFF);

    if (!(pdpt[idx_pdpt] & PTE_PRESENT)) {
        uint64_t *new_pd = (uint64_t *)pmm_alloc_page();
        if (!new_pd) return -1;
        memset(new_pd, 0, PAGE_SIZE);
        pdpt[idx_pdpt] = (uint64_t)new_pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pd = (uint64_t *)(pdpt[idx_pdpt] & ~0xFFF);

    if (!(pd[idx_pd] & PTE_PRESENT)) {
        uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
        if (!new_pt) return -1;
        memset(new_pt, 0, PAGE_SIZE);
        pd[idx_pd] = (uint64_t)new_pt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else if (pd[idx_pd] & (1ULL << 7)) {
        uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
        if (!new_pt) return -1;
        uint64_t huge_phys = pd[idx_pd] & ~0x1FFFFFULL;
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

/* Public: identity-map RAM in current PML4 */
void vmm_identity_map_all(uint64_t max_addr) {
    uint64_t *cur = (uint64_t *)read_cr3();
    for (uint64_t addr = 0; addr < max_addr; addr += 0x1000) {
        if (!vmm_get_phys(cur, addr)) {
            vmm_map_page(cur, addr, addr, 0x03);
        }
    }
}

/* Static: identity-map RAM in given PML4 */
static void vmm_identity_map_into(uint64_t *pml4) {
    for (uint64_t addr = 0; addr < 0x8000000ULL; addr += 0x1000) {
        if (!vmm_get_phys(pml4, addr)) {
            vmm_map_page(pml4, addr, addr, 0x03);
        }
    }
}

uint64_t *vmm_create_pml4_clean(void) {
    uint64_t *old_pml4 = (uint64_t *)read_cr3();
    uint64_t *new_pml4 = (uint64_t *)pmm_alloc_page();
    if (!new_pml4) return 0;
    memset(new_pml4, 0, PAGE_SIZE);

    for (int i = 256; i < 512; i++) new_pml4[i] = old_pml4[i];

    for (int i = 0; i < 256; i++) {
        if (!(old_pml4[i] & PTE_PRESENT)) continue;

        uint64_t *old_pdpt = (uint64_t *)(old_pml4[i] & ~0xFFF);
        uint64_t *new_pdpt = (uint64_t *)pmm_alloc_page();
        if (!new_pdpt) return 0;
        memset(new_pdpt, 0, PAGE_SIZE);
        new_pml4[i] = (uint64_t)new_pdpt | (old_pml4[i] & 0xFFF);

        for (int j = 0; j < 512; j++) {
            if (!(old_pdpt[j] & PTE_PRESENT)) continue;

            uint64_t *old_pd = (uint64_t *)(old_pdpt[j] & ~0xFFF);
            uint64_t *new_pd = (uint64_t *)pmm_alloc_page();
            if (!new_pd) return 0;
            memset(new_pd, 0, PAGE_SIZE);
            new_pdpt[j] = (uint64_t)new_pd | (old_pdpt[j] & 0xFFF);

            for (int k = 0; k < 512; k++) {
                if (!(old_pd[k] & PTE_PRESENT)) continue;

                if (old_pd[k] & (1ULL << 7)) {
                    new_pd[k] = old_pd[k];
                    continue;
                }

                uint64_t *old_pt = (uint64_t *)(old_pd[k] & ~0xFFF);
                uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
                if (!new_pt) return 0;
                memset(new_pt, 0, PAGE_SIZE);
                new_pd[k] = (uint64_t)new_pt | (old_pd[k] & 0xFFF);

                for (int l = 0; l < 512; l++) {
                    if (!(old_pt[l] & PTE_PRESENT)) continue;
                    uint64_t vaddr = ((uint64_t)i << 39) | ((uint64_t)j << 30) | ((uint64_t)k << 21) | ((uint64_t)l << 12);
                    if (vaddr >= 0x800000ULL && vaddr < 0x80000000ULL && (old_pt[l] & PTE_USER)) {
                        continue;
                    }
                    new_pt[l] = old_pt[l];
                }
            }
        }
    }

    vmm_identity_map_into(new_pml4);
    return new_pml4;
}

uint64_t *vmm_fork_pml4(void) {
    KLOG("[vf] start\n");
    uint64_t *old_pml4 = (uint64_t *)read_cr3();
    vmm_identity_map_into(old_pml4);
    KLOG("[vf] imap done\n");

    uint64_t *new_pml4 = (uint64_t *)pmm_alloc_page();
    if (!new_pml4) { KLOG("[vf] FAIL pml4\n"); return 0; }
    memset(new_pml4, 0, PAGE_SIZE);
    KLOG("[vf] pml4 alloc ok\n");

    for (int i = 256; i < 512; i++) new_pml4[i] = old_pml4[i];

    for (int i = 0; i < 256; i++) {
        if (!(old_pml4[i] & PTE_PRESENT)) continue;

        uint64_t *old_pdpt = (uint64_t *)(old_pml4[i] & ~0xFFF);
        uint64_t *new_pdpt = (uint64_t *)pmm_alloc_page();
        if (!new_pdpt) { KLOG("[vf] FAIL pdpt\n"); return 0; }
        memset(new_pdpt, 0, PAGE_SIZE);
        new_pml4[i] = (uint64_t)new_pdpt | (old_pml4[i] & 0xFFF);

        for (int j = 0; j < 512; j++) {
            if (!(old_pdpt[j] & PTE_PRESENT)) continue;

            uint64_t *old_pd = (uint64_t *)(old_pdpt[j] & ~0xFFF);
            uint64_t *new_pd = (uint64_t *)pmm_alloc_page();
            if (!new_pd) { KLOG("[vf] FAIL pd\n"); return 0; }
            memset(new_pd, 0, PAGE_SIZE);
            new_pdpt[j] = (uint64_t)new_pd | (old_pdpt[j] & 0xFFF);

            for (int k = 0; k < 512; k++) {
                if (!(old_pd[k] & PTE_PRESENT)) continue;

                if (old_pd[k] & (1ULL << 7)) {
                    // ВРЕМЕННО: шарим huge page как есть (для отладки)
                    // Deep copy отключён, потому что kernel identity-map использует huge pages,
                    // и deep copy сжирает всю память.
                    new_pd[k] = old_pd[k];
                    continue;
                }

                uint64_t *old_pt = (uint64_t *)(old_pd[k] & ~0xFFF);
                uint64_t *new_pt = (uint64_t *)pmm_alloc_page();
                if (!new_pt) { KLOG("[vf] FAIL pt\n"); return 0; }
                memset(new_pt, 0, PAGE_SIZE);
                new_pd[k] = (uint64_t)new_pt | (old_pd[k] & 0xFFF);

                for (int l = 0; l < 512; l++) {
                    if (!(old_pt[l] & PTE_PRESENT)) continue;
                    uint64_t vaddr = ((uint64_t)i << 39) | ((uint64_t)j << 30) | ((uint64_t)k << 21) | ((uint64_t)l << 12);
                    uint64_t pte = old_pt[l];

                    if (vaddr >= 0x80000000ULL && vaddr < 0x80400000ULL) {
                        new_pt[l] = pte;
                        continue;
                    }
                    if (!(pte & PTE_USER)) {
                        new_pt[l] = pte;
                        continue;
                    }
                    void *new_phys = pmm_alloc_page();
                    if (!new_phys) { KLOG("[vf] FAIL user_phys\n"); return 0; }
                    void *old_phys = (void *)(pte & ~0xFFF);
                    memcpy(new_phys, old_phys, PAGE_SIZE);
                    new_pt[l] = (uint64_t)new_phys | (pte & 0xFFF);
                }
            }
        }
    }

    vmm_identity_map_into(new_pml4);
    KLOG("[vf] done\n");
    return new_pml4;
}

uint64_t *vmm_create_pml4(void) {
    return vmm_fork_pml4();
}
