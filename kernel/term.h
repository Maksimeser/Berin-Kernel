#ifndef BERIN_TERM_H
#define BERIN_TERM_H

#include <stdint.h>

void term_putchar(char c);
void term_write(const char *s);
void term_set_fg(uint8_t fg);
void term_set_bg(uint8_t bg);
void term_reset_color(void);
void term_clear(void);

#endif
