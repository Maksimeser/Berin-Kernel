#ifndef BERIN_PAGING_H
#define BERIN_PAGING_H

#include <stdint.h>

int      paging_enabled(void);
uint64_t paging_get_cr3(void);
uint64_t paging_get_cr2(void);

#endif
