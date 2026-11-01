# Berin Kernel

**Independent x86_64 Kernel from scratch.**

This repository contains only the kernel source code for Berin OS. It builds a standalone ELF64 binary (`berin.bin`) which is loaded by the `turload` bootloader.

> Mascot: green fox.

## Status

Phase 6.3 — VFS + userland shell (in progress).

### Subsystems
- **Boot**: Long mode entry, page tables setup.
- **Arch (x86_64)**: GDT, TSS, IDT (256 vectors), PIC, PIT (100 Hz), Syscalls (`int 0x80`).
- **Memory**: PMM (bitmap, 4K pages), VMM (page tables, 2MB huge pages support), Heap (linked-list allocator with coalescing).
- **Processes**: Ring 3, Task scheduling (idle task, preemption), ELF64 loader, `sys_spawn`.
- **Storage**: ATA PIO, Block Device abstraction.
- **File Systems**: VFS, BerFS v1 (superblock, inodes, indirect blocks), FAT32 (WIP).
- **Console**: VGA Text mode, GOP Framebuffer (with scrolling), ANSI escape sequences, Serial COM1.
- **Drivers**: PS/2 Keyboard.
- **Recovery**: Kerbash (built-in Ring 0 debug shell).

## Build

Requirements: `nasm`, `gcc`, `make`.

This project builds completely independently of the userland or bootloader. 

```bash
# Сборка ядра (создаст berin.bin в корне проекта)
make clean
make

# Проверка формата бинарника
file berin.bin
readelf -h berin.bin

Integration
To run the built kernel, berin.bin must be copied to the ESP (EFI System Partition) used by turload. The main Berin OS Makefile handles this automatically via make kernel.
