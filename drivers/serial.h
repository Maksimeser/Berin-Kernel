#ifndef BERIN_SERIAL_H
#define BERIN_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *s);
int  serial_rx_ready(void);
char serial_read_char(void);

#endif
