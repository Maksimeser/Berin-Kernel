#ifndef BERIN_X86_64_GDT_H
#define BERIN_X86_64_GDT_H

#include <stdint.h>

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#define SEG_KCODE 0x08
#define SEG_KDATA 0x10
#define SEG_UCODE 0x18
#define SEG_UDATA 0x20
#define SEG_TSS   0x28

#endif
