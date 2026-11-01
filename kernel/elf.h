#ifndef BERIN_ELF_H
#define BERIN_ELF_H

#include <stdint.h>

#define ELF_MAGIC     0x464C457FUL
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define ET_EXEC       2
#define EM_X86_64     0x3E
#define PT_LOAD       1

struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

/* returns 0 on success, -1 on error */
int elf_load(const uint8_t *data, uint64_t size, uint64_t *pml4, uint64_t *entry_out);

#endif
