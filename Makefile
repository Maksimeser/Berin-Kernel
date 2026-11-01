# Berin Kernel - Makefile
CC      := gcc
AS      := nasm
LD      := ld

CFLAGS  := -m64 -std=gnu11 -ffreestanding -fno-stack-protector \
           -fno-pic -fno-pie -mno-red-zone \
           -mno-mmx -mno-avx -mno-80387 -mgeneral-regs-only \
           -Wall -Wextra -O2 \
           -nostdlib -nostartfiles -nodefaultlibs \
           -Iarch/x86_64 -Idrivers -Ikernel -Imm -Iinclude -Irecovery

ASFLAGS := -f elf64
LDFLAGS := -m elf_x86_64 -T linker.ld -nostdlib

BUILD   := build
KERNEL  := berin.bin

C_SRCS  := kernel/kernel.c kernel/panic.c kernel/elf.c kernel/kstring.c \
           kernel/blockdev.c kernel/kberfs.c kernel/vfs.c kernel/sched.c kernel/fat32.c \
           mm/pmm.c mm/paging.c mm/heap.c mm/vmm.c \
           arch/x86_64/gdt.c arch/x86_64/idt.c arch/x86_64/pic.c arch/x86_64/pit.c arch/x86_64/syscall.c \
           drivers/keyboard.c drivers/ata.c \
           recovery/kerbash.c

ASM_SRCS := boot/boot.asm arch/x86_64/isr_stubs.asm arch/x86_64/switch.asm

C_OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))
OBJS     := $(ASM_OBJS) $(C_OBJS)

.PHONY: all clean

all: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(BUILD) $(KERNEL)
