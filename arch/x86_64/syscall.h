#ifndef BERIN_X86_64_SYSCALL_H
#define BERIN_X86_64_SYSCALL_H

#include "idt.h"

/* Syscall numbers (Linux-compatible where possible) */
#define SYS_READ     0
#define SYS_WRITE    1
#define SYS_OPEN     2
#define SYS_CLOSE    3
#define SYS_STAT     4
#define SYS_FSTAT    5
#define SYS_LSEEK    8
#define SYS_LISTDIR  100    /* our own: listdir(path, pos, name, *ino, *type) */
#define SYS_EXIT     60
#define SYS_GETPID   39
#define SYS_YIELD    24
#define SYS_UPTIME   201

void syscall_dispatch(struct regs *r);

#endif
