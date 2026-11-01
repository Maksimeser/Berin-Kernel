/* Berin OS - x86_64 IDT + dispatch */

#include "idt.h"
#include "pic.h"
#include "syscall.h"
#include "sched.h"

extern void berin_panic(uint16_t code, const char *name, struct regs *r);
extern void timer_tick(void);
extern void kb_handler(void);

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

extern uint64_t isr_stub_table[256];

static void idt_set(int n, uint64_t base, uint16_t sel, uint8_t flags, uint8_t ist) {
    idt[n].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[n].offset_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[n].selector    = sel;
    idt[n].ist         = ist;
    idt[n].flags       = flags;
    idt[n].reserved    = 0;
}

static void map_int_to_panic(uint64_t n, uint16_t *code, const char **name) {
    switch (n) {
        case  0: *code = 3;  *name = "divide_error";         return;
        case  1: *code = 0;  *name = "debug";                return;
        case  2: *code = 0;  *name = "nmi_interrupt";        return;
        case  3: *code = 0;  *name = "breakpoint";           return;
        case  4: *code = 0;  *name = "overflow";             return;
        case  5: *code = 0;  *name = "bound_range_exceeded"; return;
        case  6: *code = 6;  *name = "invalid_opcode";       return;
        case  7: *code = 0;  *name = "device_not_available"; return;
        case  8: *code = 4;  *name = "double_fault";         return;
        case  9: *code = 0;  *name = "coprocessor_segment";  return;
        case 10: *code = 0;  *name = "invalid_tss";          return;
        case 11: *code = 8;  *name = "segment_not_present";  return;
        case 12: *code = 7;  *name = "stack_segment_fault";  return;
        case 13: *code = 2;  *name = "general_protection";   return;
        case 14: *code = 1;  *name = "page_fault";           return;
        case 15: *code = 0;  *name = "reserved_exception";   return;
        case 16: *code = 0;  *name = "x87_floating_point";   return;
        case 17: *code = 9;  *name = "alignment_check";      return;
        case 18: *code = 10; *name = "machine_check";        return;
        case 19: *code = 0;  *name = "simd_floating_point";  return;
        case 20: *code = 0;  *name = "virtualization_exception"; return;
        case 21: *code = 0;  *name = "control_protection";   return;
        default: *code = 0;  *name = "unknown_error";        return;
    }
}

void isr_handler(struct regs *r) {
    if (r->int_no == 0x80) {
        syscall_dispatch(r);
        return;
    }

    if (r->int_no >= 32 && r->int_no < 48) {
        uint8_t irq = (uint8_t)(r->int_no - 32);

        if (irq == 0) timer_tick();
        if (irq == 1) kb_handler();
        pic_eoi(irq);
        if (irq == 0 && (r->cs & 3) == 3) {
            schedule();
        }
        return;
    }

    uint16_t code;
    const char *name;
    map_int_to_panic(r->int_no, &code, &name);
    berin_panic(code, name, r);
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++) {
        uint8_t flags = (i == 0x80) ? 0xEE : 0x8E;

        uint8_t ist = 0;
        if (i == 8)  ist = 1;
        if (i == 2)  ist = 2;

        idt_set(i, isr_stub_table[i], 0x08, flags, ist);
    }

    __asm__ __volatile__("lidt %0" : : "m"(idtp));
}
