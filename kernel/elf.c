#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "kstring.h"

int elf_load(const uint8_t *data, uint64_t size, uint64_t *pml4, uint64_t *entry_out) {
    if (size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)data;

    if (*(const uint32_t *)eh->e_ident != ELF_MAGIC) return -1;
    if (eh->e_ident[4] != ELFCLASS64) return -1;
    if (eh->e_ident[5] != ELFDATA2LSB) return -1;
    if (eh->e_type != ET_EXEC) return -1;
    if (eh->e_machine != EM_X86_64) return -1;

    // Проверка, что таблица заголовков не выходит за пределы файла
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size) return -1;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(data + eh->e_phoff + (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz > ph->p_memsz) return -1;

        // КРИТИЧЕСКАЯ ПРОВЕРКА: данные сегмента не должны выходить за пределы файла
        if (ph->p_offset + ph->p_filesz > size) return -1;

        uint64_t vaddr_start = ph->p_vaddr & ~0xFFFULL;
        uint64_t vaddr_end = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += 4096) {
            void *phys_page = pmm_alloc_page();
            if (!phys_page) return -1;

            memset(phys_page, 0, 4096);

            if (vmm_map_page(pml4, vaddr, (uint64_t)phys_page, 0x07) != 0) return -1;

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
    return 0;
}
