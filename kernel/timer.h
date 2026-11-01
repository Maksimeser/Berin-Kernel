#ifndef BERIN_TIMER_H
#define BERIN_TIMER_H

#include <stdint.h>

void     timer_tick(void);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);

#endif
