#ifndef BERIN_WAIT_H
#define BERIN_WAIT_H

#include <stdint.h>
#include <stddef.h>

/* status line that overwrites itself */
void wait_start(const char *label);
void wait_step(void);                  /* call on each iteration */
void wait_done_ok(void);
void wait_done_fail(const char *reason);

/* progress bar with total */
void progress_start(const char *label, uint32_t total);
void progress_update(uint32_t current);
void progress_done(void);

#endif
