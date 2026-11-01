#ifndef BERIN_X86_64_PIC_H
#define BERIN_X86_64_PIC_H

#include <stdint.h>

void pic_remap(void);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);

#endif
