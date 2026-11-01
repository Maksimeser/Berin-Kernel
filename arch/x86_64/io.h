#ifndef BERIN_X86_64_IO_H
#define BERIN_X86_64_IO_H

#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ __volatile__("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline uint16_t inw(uint16_t p) {
    uint16_t r;
    __asm__ __volatile__("inw %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ __volatile__("outw %0, %1" :: "a"(v), "Nd"(p));
}
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif
