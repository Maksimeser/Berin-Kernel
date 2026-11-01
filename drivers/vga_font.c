/* Berin OS - load 8x16 font into VGA plane 2 (canonical sequence) */

#include <stdint.h>
#include "io.h"
#include "fonts/cp866_8x16.h"

static inline uint8_t sr(uint8_t idx) {
    outb(0x3C4, idx); return inb(0x3C5);
}
static inline void sr_set(uint8_t idx, uint8_t val) {
    outb(0x3C4, idx); outb(0x3C5, val);
}
static inline uint8_t gc(uint8_t idx) {
    outb(0x3CE, idx); return inb(0x3CF);
}
static inline void gc_set(uint8_t idx, uint8_t val) {
    outb(0x3CE, idx); outb(0x3CF, val);
}

void vga_install_cp866_font(void) {
    const uint8_t *font = (const uint8_t *)cp866_8x16;

    /* Save state */
    uint8_t sr0 = sr(0x00);
    uint8_t sr2 = sr(0x02);
    uint8_t sr3 = sr(0x03);
    uint8_t sr4 = sr(0x04);
    uint8_t gc4 = gc(0x04);
    uint8_t gc5 = gc(0x05);
    uint8_t gc6 = gc(0x06);

    /* Enter sync reset */
    sr_set(0x00, 0x01);

    /* Point at plane 2 */
    sr_set(0x02, 0x04);   /* map mask  = plane 2 */
    sr_set(0x04, 0x06);   /* mem mode  = extended + sequential */
    sr_set(0x05, 0x00);   /* mode       = normal */

    gc_set(0x04, 0x02);   /* read map  = plane 2 */
    gc_set(0x05, 0x00);   /* gc mode   = 0 */
    gc_set(0x06, 0x00);   /* misc       = text mode */

    /* Copy 4096 bytes */
    volatile uint8_t *p = (volatile uint8_t *)0xA0000;
    for (int i = 0; i < 4096; i++) p[i] = font[i];

    /* Restore state */
    sr_set(0x00, sr0);
    sr_set(0x02, sr2);
    sr_set(0x03, sr3);
    sr_set(0x04, sr4);
    gc_set(0x04, gc4);
    gc_set(0x05, gc5);
    gc_set(0x06, gc6);
}
