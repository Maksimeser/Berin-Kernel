#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "kstring.h"

extern void term_write(const char *s);
extern void term_putchar(char c);

int elf_load(const uint8_t *data, uint64_t size, uint64_t *pml4, uint64_t *entry_out) {
    term_write("[elf_load] start\n");
    if (size < sizeof(struct elf64_ehdr)) { term_write("[elf_load] size too small\n"); return -1; }
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)data;

    if (*(const uint32_t *)eh->e_ident != ELF_MAGIC) { term_write("[elf_load] bad magic\n"); return -1; }
    if (eh->e_ident[4] != ELFCLASS64) { term_write("[elf_load] not 64-bit\n"); return -1; }
    if (eh->e_ident[5] != ELFDATA2LSB) { term_write("[elf_load] not LSB\n"); return -1; }
    if (eh->e_type != ET_EXEC) { term_write("[elf_load] not EXEC\n"); return -1; }
    if (eh->e_machine != EM_X86_64) { term_write("[elf_load] not x86_64\n"); return -1; }

    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size) {
        term_write("[elf_load] phdr out of bounds\n");
        return -1;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(data + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        term_write("[elf_load] PT_LOAD found\n");

        if (ph->p_filesz > ph->p_memsz) { term_write("[elf_load] filesz > memsz\n"); return -1; }
        if (ph->p_offset + ph->p_filesz > size) { term_write("[elf_load] seg out of bounds\n"); return -1; }

        uint64_t vaddr_start = ph->p_vaddr & ~0xFFFULL;
        uint64_t vaddr_end = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += 4096) {
            void *phys_page = pmm_alloc_page();
            if (!phys_page) { term_write("[elf_load] pmm_alloc FAIL\n"); return -1; }
            memset(phys_page, 0, 4096);

            if (vmm_map_page(pml4, vaddr, (uint64_t)phys_page, 0x07) != 0) {
                term_write("[elf_load] vmm_map_page FAIL\n");
                return -1;
            }

            uint64_t page_start = vaddr;
            uint64_t page_end = vaddr + 4096;
            uint64_t copy_start = page_start > ph->p_vaddr ? page_start : ph->p_vaddr;
            uint64_t copy_end = page_end < (ph->p_vaddr + ph->p_filesz) ? page_end : (ph->p_vaddr + ph->p_filesz);

            if (copy_start < copy_end) {
                uint64_t len = copy_end - copy_start;
                uint64_t file_offset = ph->p_offset + (copy_start - ph->p_vaddr);
                uint64_t phys_dst = (uint64_t)phys_page + (copy_start - page_start);
                memcpy((void *)phys_dst, data + file_offset, len);
            }
        }
    }

    *entry_out = eh->e_entry;
    term_write("[elf_load] success\n");
    return 0;
}
