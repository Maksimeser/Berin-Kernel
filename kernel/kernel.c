// Berin - kernel x86_64
#include "heap.h"
#include "pmm.h"
#include "blockdev.h"
#include "kberfs.h"
#include "ata.h"
#include "vfs.h"
#include "gdt.h"
#include "fat32.h"
#include "paging.h"
#include "sched.h"
#include "idt.h"
#include "elf.h"
#include "vmm.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "kerbash.h"
#include <stdint.h>
#include <stddef.h>

#include "boot_info.h"
#include "fonts/cp866_8x16.h"

static uint8_t term_fg = 7;
static uint8_t term_bg = 0;

/* ============================================================
 *  Port I/O
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" :: "a"(val), "Nd"(port));
}

/* ============================================================
 *  Serial COM1
 * ============================================================ */
#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}
static int  serial_tx_empty(void) { return inb(COM1 + 5) & 0x20; }
int  serial_rx_ready(void) { return inb(COM1 + 5) & 0x01; }

void serial_write_char(char c) {
    while (!serial_tx_empty()) ;
    outb(COM1, (uint8_t)c);
}
static char serial_read_char(void) {
    while (!serial_rx_ready()) ;
    return (char)inb(COM1);
}

/* ============================================================
 *  Framebuffer (GOP)
 * ============================================================ */
static struct boot_info *g_binfo = 0;
static volatile uint32_t *g_fb = 0;
static uint32_t fb_col = 0;
static uint32_t fb_row = 0;

#define FB_BLACK   0x00000000

static uint32_t vga_to_rgb(uint8_t color) {
    switch(color & 0x0F) {
        case 0: return 0x000000; case 1: return 0x0000AA;
        case 2: return 0x00AA00; case 3: return 0x00AAAA;
        case 4: return 0xAA0000; case 5: return 0xAA00AA;
        case 6: return 0xAA5500; case 7: return 0xAAAAAA;
        case 8: return 0x555555; case 9: return 0x5555FF;
        case 10: return 0x55FF55; case 11: return 0x55FFFF;
        case 12: return 0xFF5555; case 13: return 0xFF55FF;
        case 14: return 0xFFFF55; case 15: return 0xFFFFFF;
        default: return 0xFFFFFF;
    }
}

static void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_fb || x >= g_binfo->fb_width || y >= g_binfo->fb_height) return;
    g_fb[(y * (g_binfo->fb_pitch / 4)) + x] = color;
}

static void fb_clear(uint32_t color) {
    if (!g_fb) return;
    for (uint32_t y = 0; y < g_binfo->fb_height; y++) {
        for (uint32_t x = 0; x < g_binfo->fb_width; x++) {
            g_fb[(y * (g_binfo->fb_pitch / 4)) + x] = color;
        }
    }
    fb_col = 0; fb_row = 0;
}

static void fb_scroll(void) {
    if (!g_fb) return;
    uint32_t line_height = 16;
    uint32_t pitch = g_binfo->fb_pitch / 4;
    uint32_t total_pixels = g_binfo->fb_height * pitch;
    uint32_t shift_pixels = line_height * pitch;
    for (uint32_t i = 0; i < total_pixels - shift_pixels; i++) {
        g_fb[i] = g_fb[i + shift_pixels];
    }
    for (uint32_t i = total_pixels - shift_pixels; i < total_pixels; i++) {
        g_fb[i] = FB_BLACK;
    }
    if (fb_row > 0) fb_row--;
}

static void fb_putchar(char c) {
    if (!g_fb) return;

    if (c == '\n') {
        fb_col = 0; fb_row++;
    } else if (c == '\b') {
        if (fb_col > 0) fb_col--;
    } else if (c == '\r') {
        fb_col = 0;
    } else {
        const uint8_t *glyph = cp866_8x16[(uint8_t)c];
        uint32_t fg = vga_to_rgb(term_fg);
        uint32_t bg = vga_to_rgb(term_bg);

        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 8; x++) {
                if (glyph[y] & (0x80 >> x)) fb_putpixel(fb_col * 8 + x, fb_row * 16 + y, fg);
                else fb_putpixel(fb_col * 8 + x, fb_row * 16 + y, bg);
            }
        }
        fb_col++;
    }

    if (fb_col >= g_binfo->fb_width / 8) { fb_col = 0; fb_row++; }
    if (fb_row >= g_binfo->fb_height / 16) fb_scroll();

    if (c == '\n') serial_write_char('\r');
    serial_write_char(c);
}

/* ============================================================
 *  VGA Text Mode & ANSI
 * ============================================================ */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

#define C_BLACK   0
#define C_GRAY    7
#define C_LGREEN 10

static size_t  term_row;
static size_t  term_col;
static uint8_t term_color = 0x07;

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}
static inline void term_update_color(void) {
    term_color = (uint8_t)((term_bg << 4) | (term_fg & 0x0F));
}
void term_set_fg(uint8_t fg) { term_fg = fg & 0x0F; term_update_color(); }
void term_set_bg(uint8_t bg) { term_bg = bg & 0x0F; term_update_color(); }
void term_reset_color(void)  { term_fg = C_GRAY; term_bg = C_BLACK; term_update_color(); }

void term_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = 0; term_col = 0;

    if (g_fb) {
        fb_clear(FB_BLACK);
        __asm__ __volatile__("wbinvd");
    } else {
        outb(0x3D4, 0x0F); outb(0x3D5, 0x00);
        outb(0x3D4, 0x0E); outb(0x3D5, 0x00);
    }
}

static void term_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = VGA_MEMORY[(y + 1) * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = VGA_HEIGHT - 1;
}

static void term_emit(char c) {
    if (c == '\n') { term_col = 0; term_row++; }
    else if (c == '\b') { if (term_col > 0) term_col--; }
    else if (c == '\r') { term_col = 0; }
    else {
        if (term_row < VGA_HEIGHT && term_col < VGA_WIDTH)
            VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry((unsigned char)c, term_color);
        term_col++;
    }

    if (term_col >= VGA_WIDTH) { term_col = 0; term_row++; }
    if (term_row >= VGA_HEIGHT) term_scroll();

    if (c == '\n') serial_write_char('\r');
    serial_write_char(c);

    uint16_t pos = (uint16_t)(term_row * VGA_WIDTH + term_col);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static int ansi_state = 0;
static int ansi_param = 0;
static const uint8_t ansi_to_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
static const uint8_t ansi_to_vga_bright[8] = { 8, 12, 10, 14, 9, 13, 11, 15 };

static void ansi_sgr(int p) {
    if (p == 0)                    term_reset_color();
    else if (p == 1)               ;
    else if (p >= 30 && p <= 37)   term_set_fg(ansi_to_vga[p - 30]);
    else if (p >= 40 && p <= 47)   term_set_bg(ansi_to_vga[p - 40]);
    else if (p >= 90 && p <= 97)   term_set_fg(ansi_to_vga_bright[p - 90]);
    else if (p >= 100 && p <= 107) term_set_bg(ansi_to_vga_bright[p - 100]);
}

void term_putchar(char c) {
    if (ansi_state == 2) {
        if (c >= '0' && c <= '9') { ansi_param = ansi_param * 10 + (c - '0'); return; }
        if (c == ';')             { ansi_param = 0; return; }
        ansi_state = 0;
        if (c == 'm')                          ansi_sgr(ansi_param);
        else if (c == 'J' && ansi_param == 2)  term_clear();
        else if (c == 'H')                     { term_row = 0; term_col = 0; }
        return;
    }
    if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_param = 0; return; }
        ansi_state = 0; return;
    }
    if (c == '\033') { ansi_state = 1; return; }

    if (g_fb) fb_putchar(c);
    else term_emit(c);
}

void term_write(const char *s) {
    while (*s) term_putchar(*s++);
}

/* ============================================================
 *  Timer, Input & Helpers
 * ============================================================ */
static volatile uint64_t ticks = 0;
void timer_tick(void) { ticks++; }
uint64_t get_ticks(void) { return ticks; }

char input_getchar(void) { // <-- УБРАН STATIC
    for (;;) {
        if (kb_ready())        return kb_getchar();
        if (serial_rx_ready()) return serial_read_char();
        __asm__ __volatile__("pause");
    }
}
char berin_input_getchar(void) { return input_getchar(); }

int str_eq(const char *a, const char *b) { // <-- УБРАН STATIC
    while (*a && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

void put_u64(uint64_t v) { // <-- УБРАН STATIC
    char r[24]; int ri = 0;
    if (v == 0) { term_putchar('0'); return; }
    while (v) { r[ri++] = '0' + (v % 10); v /= 10; }
    while (ri) term_putchar(r[--ri]);
}

void put_hex64(uint64_t v) { // <-- УБРАН STATIC
    for (int i = 60; i >= 0; i -= 4)
        term_putchar("0123456789ABCDEF"[(v >> i) & 0xF]);
}

int tokenize(char *s, char **argv, int max) { // <-- УБРАН STATIC
    int argc = 0;
    while (*s && argc < max) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        argv[argc++] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        if (*s) *s++ = '\0';
    }
    return argc;
}

/* ============================================================
 *  Reboot/Logoff
 * ============================================================ */
void reboot_now(void) {
    while (inb(0x64) & 0x02) ;
    outb(0x64, 0xFE);
    outw(0x604, 0x2000);
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ __volatile__("lidt %0" :: "m"(null_idt));
    __asm__ __volatile__("int $0x03");
    for (;;) __asm__ __volatile__("hlt");
}

void logoff_now(void) {
    outw(0x604, 0x2000);
    outw(0x4004, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* ============================================================
 *  Entry
 * ============================================================ */
struct berfs_mount g_root; // <-- УБРАН STATIC

void kernel_main(struct boot_info *binfo) {
    if (binfo && binfo->fb_base) {
        g_binfo = binfo;
        g_fb = (volatile uint32_t *)(uintptr_t)binfo->fb_base;
    } else {
        g_fb = 0;
    }

    serial_init();

    {
        uint64_t cr0, cr4;
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ULL << 2); cr0 |=  (1ULL << 1);
        __asm__ __volatile__("mov %0, %%cr0" :: "r"(cr0));
        __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 9) | (1ULL << 10);
        __asm__ __volatile__("mov %0, %%cr4" :: "r"(cr4));
    }

    gdt_init();
    term_set_fg(C_LGREEN);
    term_set_bg(C_BLACK);
    term_clear();
    term_write("\n");

    heap_init();
    vfs_init();
    bdev_init();

    int mrc = berfs_mount_fs(&g_root);
    if (mrc != 0) {
        term_write("[ WARN ] berfs mount: "); put_u64((uint64_t)mrc); term_putchar('\n');
    }
    if (mrc == 0) {
        vfs_mount_berfs(&g_root);
    }

    pmm_init();
    idt_init();
    pic_remap();
    pit_init(100);
    kb_init();
    pic_unmask(0);
    pic_unmask(1);

    extern int kb_ready(void);
    extern char kb_getchar(void);
    while (kb_ready()) {
        kb_getchar();
    }

    while (serial_rx_ready()) {
        serial_read_char();
    }

    __asm__ __volatile__("sti");

    term_write("Berin 0.0.4 loaded!\n\n");

    __asm__ __volatile__("wbinvd");
    sched_init();

    const char *init_paths[] = {
        "/sbin/init", "/bin/init", "/bin/login.elf", "/bin/berbash.elf", 0
    };

    const char *init_to_run = 0;
    uint32_t init_size = 0;

    for (int i = 0; init_paths[i]; i++) {
        uint32_t size; uint8_t type;
        if (vfs_stat(init_paths[i], &size, &type) == 0 && type == VFS_FILE) {
            init_to_run = init_paths[i];
            init_size = size;
            break;
        }
    }

    if (!init_to_run) {
        term_write("[ FAIL ] No init found. Dropping to kerbash.\n");
        kerbash_run();
    } else {
        term_write("[ init ] found: ");
        term_write(init_to_run);
        term_write("\n");

        void *code = kmalloc(init_size);
        int fd = vfs_open(init_to_run, O_RDONLY);
        if (fd >= 0 && code) {
            vfs_read(fd, code, init_size);
            vfs_close(fd);

            uint64_t *new_pml4 = vmm_create_pml4();
            uint64_t entry = 0;
            if (new_pml4 && elf_load(code, init_size, new_pml4, &entry) == 0) {
                kfree(code);

                void *stack_phys = pmm_alloc_page();
                uint64_t user_stack_vaddr = 0x7FFFFFFFE000;
                vmm_map_page(new_pml4, user_stack_vaddr, (uint64_t)stack_phys, 0x07);
                uint64_t user_rsp = user_stack_vaddr + 4096 - 16;

                struct task *t = task_create(entry, new_pml4, user_rsp);
                if (t) {
                    term_write("[ init ] started as PID 1\n");
                    extern void idle_loop(void);
                    idle_loop();
                } else {
                    term_write("[ FAIL ] task_create failed\n");
                }
            } else {
                term_write("[ FAIL ] elf_load failed\n");
            }
        } else {
            term_write("[ FAIL ] cannot open init\n");
        }

        term_write("[ FAIL ] init crashed. Dropping to kerbash.\n");
        kerbash_run();
    }

    for (;;) __asm__ __volatile__("cli; hlt");
}
