#ifndef BERIN_PANIC_H
#define BERIN_PANIC_H

#include <stdint.h>
#include "idt.h"

void berin_panic(uint16_t code, const char *name, struct regs *r);

#endif
