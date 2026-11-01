#include "panic.h"
#include "vmm.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "version.h"
#include "io.h"

extern void term_write(const char *s);
extern void term_putchar(char c);
extern void term_set_fg(uint8_t fg);
extern void term_set_bg(uint8_t bg);
extern void term_clear(void);

static void put_hex64(uint64_t v) {
    for (int i = 60; i >= 0; i -= 4) term_putchar("0123456789ABCDEF"[(v >> i) & 0xF]);
}
static void put_u64(uint64_t v) {
    char r[24]; int ri = 0;
    if (v == 0) { term_putchar('0'); return; }
    while (v) { r[ri++] = '0' + (v % 10); v /= 10; }
    while (ri) term_putchar(r[--ri]);
}
static void put_u16_3(uint16_t v) {
    term_putchar('0' + (v / 100) % 10);
    term_putchar('0' + (v /  10) % 10);
    term_putchar('0' +  v        % 10);
}

void berin_panic(uint16_t code, const char *name, struct regs *r) {
    __asm__ __volatile__("cli");
    term_set_fg(12); term_set_bg(0); term_clear();

    // 1
    term_write("*** BERIN PANIC ***\n");

    // 2
    term_set_fg(14); term_write("(1panic1)-"); put_u16_3(code);
    term_putchar('_'); term_write(name); term_set_fg(12); term_putchar('\n');

    // 3
    term_write("Berin OS "); term_write(BERIN_VERSION); term_write(" (x86_64)\n");

    // 4
    term_write("Reason: Exception #"); put_u64(r->int_no);
    term_write(", err=0x"); put_hex64(r->err_code);
    term_write(" at RIP=0x"); put_hex64(r->rip); term_putchar('\n');

    // 5
    if (code == 1) {
        term_write("Faulting addr: 0x"); put_hex64(paging_get_cr2()); term_putchar('\n');
    }

    // 6
    term_write("\nRegisters:\n");

    // 7
    term_write("  RIP=0x"); put_hex64(r->rip);
    term_write("  RSP=0x"); put_hex64(r->rsp); term_putchar('\n');

    // 8
    term_write("  RBP=0x"); put_hex64(r->rbp);
    term_write("  RAX=0x"); put_hex64(r->rax); term_putchar('\n');

    // 9
    term_write("  RBX=0x"); put_hex64(r->rbx);
    term_write("  RCX=0x"); put_hex64(r->rcx); term_putchar('\n');

    // 10
    term_write("  CR3=0x"); put_hex64(paging_get_cr3()); term_putchar('\n');

    // 11
    term_write("\nSystem:\n");

    // 12
    extern uint64_t get_ticks(void);
    uint64_t ticks = get_ticks();
    term_write("  Uptime: "); put_u64(ticks / 100); term_putchar('.');
    term_putchar('0' + (ticks / 10) % 10); term_putchar('0' + ticks % 10); term_write("s\n");

    // 13
    term_write("  Memory: "); put_u64(pmm_free_pages() * 4); term_write(" KB free / ");
    put_u64(pmm_total_pages() * 4); term_write(" KB total\n");

    // 14
    term_write("  Heap:   "); put_u64(heap_used()); term_write(" B used\n");

    // 15
    term_write("\nCopy: (1panic1)-"); put_u16_3(code);
    term_putchar('_'); term_write(name); term_putchar('\n');

    // 16
    term_write("Search: turkergames.com/lab/berin/1panic1\n");

    // 17
    term_write("Reboot: [Ctrl+Alt+Del] or [system halted]\n");

    int have_ctrl = 0, have_alt = 0;
    for (;;) {
        if (inb(0x64) & 0x01) {
            uint8_t sc = inb(0x60);
            if (sc == 0x1D) have_ctrl = 1;
            else if (sc == 0x38) have_alt = 1;
            else if (sc == 0x53 && have_ctrl && have_alt) {
                while (inb(0x64) & 0x02);
                outb(0x64, 0xFE);
                for (;;) __asm__ __volatile__("cli; hlt");
            }
            else if (sc == 0x9D) have_ctrl = 0;
            else if (sc == 0xB8) have_alt = 0;
        }
    }
}
