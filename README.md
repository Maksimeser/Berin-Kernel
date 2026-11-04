# 🦊 Berin Kernel

**Independent x86_64 Kernel from scratch.**

This repository contains only the kernel source code for Berin OS. It builds a standalone ELF64 binary (`berin.bin`) which is loaded by the `turload` bootloader.

> Mascot: Sorsha, green fox.

## Status

**Phase 6.3 — CLOSED.** 
**Phase 6.4 — Next (fork/execve debugging & kdebug integration).**

### Subsystems implemented
- **Boot**: Long mode entry, PVH, page tables setup.
- **Arch (x86_64)**: GDT, TSS, IDT (256 vectors), PIC, PIT (100 Hz), Syscalls (`int 0x80`), `switch_to` context switch.
- **Memory**: PMM (bitmap, 4K pages), VMM (4-level paging, PML4 clone for fork/spawn), Heap (linked-list allocator, 32MB).
- **Processes**: Ring 3, Task scheduling, `SYS_SPAWN` (syscall 200) launches userland ELF. *(fork/execve are WIP).*
- **Storage**: ATA PIO (master/slave), Block Device abstraction.
- **File Systems**: VFS (mount, resolve, read/write), BerFS v1 (superblock, inodes, indirect blocks), FAT32 (basic BPB read).
- **Console**: VGA Text mode, GOP Framebuffer (with scrolling), ANSI escape sequences, Serial COM1.
- **Drivers**: PS/2 Keyboard.
- **Debug**: `kdebug` (klog) system for toggling logs on the fly. PANIC screen with `(1panic1)` codes.
- **Recovery**: Kerbash (built-in Ring 0 debug shell).

## Build Requirements (Arch Linux)

```bash
sudo pacman -S nasm gcc make qemu-system-x86
Build
bash
# Сборка ядра (создаст berin.bin в корне проекта)
make clean
make

# Проверка формата бинарника
file berin.bin
readelf -h berin.bin
Integration
To run the built kernel, berin.bin must be copied to the ESP (EFI System Partition) used by turload. The main Berin OS Makefile handles this automatically via make kernel.

Language & Rules
Kernel Core, Arch, MM, Sched, System Software: Strictly C (gnu23) + NASM. No C++.

Rust modules (drivers only): Phase 10+ (no_std, C ABI).

AI Assistants: DeepSeek, Claude, GPT.

