#ifndef BERIN_VMM_H
#define BERIN_VMM_H

#include <stdint.h>

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)

/* Создать новую PML4 с маппингом ядра */
uint64_t *vmm_create_pml4(void);

/* Замапить виртуальный адрес на физический в заданном PML4 */
int vmm_map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags);

/* Получить физический адрес по виртуальному (walk) */
uint64_t vmm_get_phys(uint64_t *pml4, uint64_t vaddr);

/* Загрузить PML4 в CR3 */
void vmm_switch_pml4(uint64_t *pml4);

#endif
