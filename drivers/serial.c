/* Berin OS - COM1 serial driver (115200 8N1) */

#include "serial.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* enable DLAB */
    outb(COM1 + 0, 0x01);   /* divisor low: 115200 baud */
    outb(COM1 + 1, 0x00);   /* divisor high */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop */
    outb(COM1 + 2, 0xC7);   /* FIFO on, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR set */
}

static int tx_empty(void) { return inb(COM1 + 5) & 0x20; }
static int rx_ready(void) { return inb(COM1 + 5) & 0x01; }

void serial_write_char(char c) {
    while (!tx_empty()) ;
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) {
        if (*s == '\n') serial_write_char('\r');
        serial_write_char(*s++);
    }
}

int serial_rx_ready(void) { return rx_ready(); }

char serial_read_char(void) {
    while (!rx_ready()) ;
    return (char)inb(COM1);
}
