#include "timer.h"

static volatile uint64_t ticks;
static uint32_t          hz = 100;

void timer_set_hz(uint32_t new_hz) { hz = new_hz; }
uint32_t timer_hz(void)            { return hz; }

void timer_tick(void) { ticks++; }
uint64_t timer_ticks(void) { return ticks; }
