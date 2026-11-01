/* Berin - x86_64 GDT + TSS */

#include "gdt.h"

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[7];
static struct gdt_ptr gp;
static struct tss64    tss;

static uint8_t kstack[65536] __attribute__((aligned(16)));
static uint8_t ist1_stack[4096] __attribute__((aligned(16)));
static uint8_t ist2_stack[4096] __attribute__((aligned(16)));


void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    /* null */
    gdt[0] = 0;

    /* kernel code (0x08) */
    gdt[1] = 0x00AF9A000000FFFFULL;
    /* kernel data (0x10) */
    gdt[2] = 0x00CF92000000FFFFULL;
    /* user code (0x18) */
    gdt[3] = 0x00AFFA000000FFFFULL;
    /* user data (0x20) */
    gdt[4] = 0x00CFF2000000FFFFULL;

    /* TSS (0x28, takes two slots) */
    for (unsigned i = 0; i < sizeof(tss); i++) ((uint8_t *)&tss)[i] = 0;
    tss.iomap_base = sizeof(tss);
    tss.rsp0 = (uint64_t)&kstack[sizeof(kstack)];

    tss.ist[0] = (uint64_t)&ist1_stack[sizeof(ist1_stack)]; // IST1 для #DF
    tss.ist[1] = (uint64_t)&ist2_stack[sizeof(ist2_stack)]; // IST2 для NMI

    uint64_t base  = (uint64_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    gdt[5] = (uint64_t)(limit & 0xFFFF)
    | ((base & 0xFFFF) << 16)
    | (((base >> 16) & 0xFF) << 32)
    | (0x89ULL << 40)
    | ((uint64_t)((limit >> 16) & 0xF) << 48)
    | (((base >> 24) & 0xFF) << 56);
    gdt[6] = (base >> 32) & 0xFFFFFFFFULL;

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)&gdt;

    __asm__ __volatile__(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m"(gp)
        : "rax", "memory"
    );
}
