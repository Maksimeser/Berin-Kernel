#include "kdebug.h"
#include "heap.h"
#include "kstring.h"

extern uint8_t _kernel_end[];
extern void term_write(const char *s);
extern void term_putchar(char c);
uint8_t *heap_debug_start(void);

#define HEAP_SIZE (32ULL * 1024ULL * 1024ULL)
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
    struct block_header *prev;
};

static struct block_header *heap_start = 0;
static uint64_t used_bytes = 0;

static void put_hex64(uint64_t v) {
    for (int i = 60; i >= 0; i -= 4)
        term_putchar("0123456789ABCDEF"[(v >> i) & 0xF]);
}

void heap_init(void) {
    uint8_t *base = (uint8_t *)ALIGN_UP((uint64_t)_kernel_end, 8);
    heap_start = (struct block_header *)base;
    heap_start->size = HEAP_SIZE;
    heap_start->is_free = 1;
    heap_start->next = 0;
    heap_start->prev = 0;
    used_bytes = 0;
}

void *kmalloc(size_t size) {
    if (size == 0 || size > 1024 * 1024) return 0;
    size_t total_size = ALIGN_UP(size + sizeof(struct block_header), 8);

    struct block_header *curr = heap_start;
    int guard = 0;
    while (curr) {
        if (++guard > 100000) {
            KLOG("[heap] LOOP in free list!\n");
            return 0;
        }
        if (curr->size == 0 || curr->size > HEAP_SIZE) {
            KLOG("[heap] BAD BLOCK size=0x");
            put_hex64(curr->size);
            term_putchar('\n');
            return 0;
        }
        if (curr->is_free && curr->size >= total_size) {
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
            curr->is_free = 0;
            used_bytes += curr->size;
            return (void *)((uint8_t *)curr + sizeof(struct block_header));
        }
        curr = curr->next;
    }
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint64_t p = (uint64_t)ptr;
    uint64_t lo = (uint64_t)heap_start;
    uint64_t hi = lo + HEAP_SIZE;
    if (p < lo + sizeof(struct block_header) || p >= hi) {
        KLOG("[heap] kfree OUT OF RANGE\n");
        return;
    }
    struct block_header *b = (struct block_header *)((uint8_t *)ptr - sizeof(struct block_header));
    if (b->is_free) {
        KLOG("[heap] kfree DOUBLE FREE\n");
        return;
    }
    if (b->size > used_bytes) {
        KLOG("[heap] kfree SIZE>USED\n");
        return;
    }

    b->is_free = 1;
    used_bytes -= b->size;

    if (b->next && b->next->is_free) {
        b->size += b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->is_free) {
        b->prev->size += b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return 0; }
    struct block_header *b = (struct block_header *)((uint8_t *)ptr - sizeof(struct block_header));
    size_t data_size = b->size - sizeof(struct block_header);
    if (data_size >= new_size) return ptr;
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, data_size);
    kfree(ptr);
    return new_ptr;
}

uint64_t heap_total(void) { return HEAP_SIZE; }
uint64_t heap_used(void)  { return used_bytes; }
uint64_t heap_free(void)  { return HEAP_SIZE - used_bytes; }

uint8_t *heap_debug_start(void) {
    return (uint8_t *)heap_start;
}
