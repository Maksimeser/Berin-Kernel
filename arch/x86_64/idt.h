#ifndef BERIN_X86_64_IDT_H
#define BERIN_X86_64_IDT_H

#include <stdint.h>

/* Layout must match isr_common push order in isr_stubs.asm */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

void idt_init(void);

#endif
