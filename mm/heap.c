/* Berin - kernel heap: linked-list allocator with coalescing (free) */
#include "heap.h"
#include "kstring.h"

extern uint8_t _kernel_end[];

#define HEAP_SIZE (8ULL * 1024ULL * 1024ULL)
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

// Заголовок каждого блока памяти
struct block_header {
    size_t size;            // Размер блока (включая заголовок)
    int is_free;            // 1 = свободен, 0 = занят
    struct block_header *next;
    struct block_header *prev;
};

static struct block_header *heap_start = 0;
static uint64_t used_bytes = 0;

void heap_init(void) {
    uint8_t *base = (uint8_t *)ALIGN_UP((uint64_t)_kernel_end, 8);

    // Инициализируем самый первый, огромный блок
    heap_start = (struct block_header *)base;
    heap_start->size = HEAP_SIZE;
    heap_start->is_free = 1;
    heap_start->next = 0;
    heap_start->prev = 0;

    used_bytes = 0;
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;

    size_t total_size = ALIGN_UP(size + sizeof(struct block_header), 8);

    struct block_header *curr = heap_start;
    while (curr) {
        if (curr->is_free && curr->size >= total_size) {
            // Нашли подходящий блок!
            // Если он сильно больше, чем нужно, разрезаем его пополам
            if (curr->size >= total_size + sizeof(struct block_header) + 8) {
                struct block_header *new_block = (struct block_header *)((uint8_t *)curr + total_size);
                new_block->size = curr->size - total_size;
                new_block->is_free = 1;
                new_block->next = curr->next;
                new_block->prev = curr;
                if (curr->next) curr->next->prev = new_block;

                curr->size = total_size;
                curr->next = new_block;
            }

            curr->is_free = 0; // Помечаем как занятый
            used_bytes += curr->size;

            // Возвращаем указатель на данные (сразу после заголовка)
            return (void *)((uint8_t *)curr + sizeof(struct block_header));
        }
        curr = curr->next;
    }
    return 0; // Нет памяти
}

void kfree(void *ptr) {
    if (!ptr) return;

    // Находим заголовок нашего блока
    struct block_header *block = (struct block_header *)((uint8_t *)ptr - sizeof(struct block_header));
    if (block->is_free) return; // Двойное освобождение (защита)

    block->is_free = 1;
    used_bytes -= block->size;

    // Склеиваем со следующим блоком, если он свободен
    if (block->next && block->next->is_free) {
        block->size += block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    // Склеиваем с предыдущим блоком, если он свободен
    if (block->prev && block->prev->is_free) {
        block->prev->size += block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return 0; }

    struct block_header *block = (struct block_header *)((uint8_t *)ptr - sizeof(struct block_header));
    size_t data_size = block->size - sizeof(struct block_header);

    if (data_size >= new_size) return ptr; // Текущего размера хватает

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;

    memcpy(new_ptr, ptr, data_size);
    kfree(ptr);
    return new_ptr;
}

uint64_t heap_total(void) { return HEAP_SIZE; }
uint64_t heap_used(void)  { return used_bytes; }
uint64_t heap_free(void)  { return HEAP_SIZE - used_bytes; }
