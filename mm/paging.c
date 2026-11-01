/* Berin OS - paging (x86_64)
 * Page tables are set up in boot.asm (identity map, 2 MB huge pages).
 * Here we only expose CR3 / CR2 / status.
 */

#include "paging.h"

int paging_enabled(void) {
    uint64_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000ULL) ? 1 : 0;
}

uint64_t paging_get_cr3(void) {
    uint64_t v;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(v));
    return v;
}

uint64_t paging_get_cr2(void) {
    uint64_t v;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(v));
    return v;
}
