#include "pit.h"
#include "io.h"

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182u

void pit_init(uint32_t hz) {
    uint32_t div = PIT_FREQ / hz;
    if (div > 0xFFFF) div = 0xFFFF;
    if (div < 1)      div = 1;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
}
