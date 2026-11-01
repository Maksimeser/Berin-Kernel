// Berin - Recovery Shell (kerbash)
#include "kerbash.h"
#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "vfs.h"
#include "kberfs.h"
#include "ata.h"
#include "fat32.h"
#include "elf.h"
#include "vmm.h"
#include "sched.h"
#include "blockdev.h"
#include <stdint.h>

#define MAX_LINE 256
#define MAX_ARGS 16

// Внешние функции и переменные из kernel.c
extern void term_write(const char *s);
extern void term_putchar(char c);
extern void term_clear(void);
extern char input_getchar(void);
extern void put_u64(uint64_t v);
extern void put_hex64(uint64_t v);
extern int tokenize(char *s, char **argv, int max);
extern int str_eq(const char *a, const char *b);
extern void reboot_now(void);
extern void logoff_now(void);
extern uint64_t get_ticks(void);
extern uint64_t enter_user_mode(uint64_t entry, uint64_t user_rsp);
extern struct berfs_mount g_root;

static char line_buf[MAX_LINE];
static int  line_len;

/* ============================================================
 *  kerbash Builtins
 * ============================================================ */
static int builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) term_putchar(' ');
        term_write(argv[i]);
    }
    term_putchar('\n');
    return 0;
}

static int builtin_help(int argc, char **argv) {
    if (argc >= 2 && str_eq(argv[1], "d-")) {
        term_write("kerbash 0.3 - debug/destructive:\n");
        term_write("  crash         trigger divide error\n");
        term_write("  pf            trigger page fault\n");
        term_write("  halt          halt CPU\n");
        term_write("  vmtest        test VMM (create PML4, map page)\n");
        term_write("  kmtest        kmalloc/kfree round-trip\n");
        term_write("  memtest       alloc 3 pages, free, verify\n");
        term_write("  atatest       ATA read/write test (sector 100)\n");
        return 0;
    }
    term_write("kerbash 0.3 - builtins:\n");
    term_write("  echo <args>   print arguments\n");
    term_write("  help [d-]     this message (d- for debug)\n");
    term_write("  clear         clear screen\n");
    term_write("  about         about Berin OS\n");
    term_write("  versi         show versions\n");
    term_write("  colors        ANSI color demo\n");
    term_write("  bls           list root directory\n");
    term_write("  fatls         list files in FAT32 root (dsk001)\n");
    term_write("  bcat <name>   print file contents\n");
    term_write("  elf <name>    load ELF from disk and run\n");
    term_write("  disks         list block devices (dsk000)\n");
    term_write("  uptime        ticks since boot\n");
    term_write("  bread         read BerFS superblock from disk\n");
    term_write("  ata           ATA drive info\n");
    term_write("  whoami        print current user\n");
    term_write("  meminfo       physical memory summary\n");
    term_write("  paging        paging status (CR3, CR2)\n");
    term_write("  reboot        reboot machine\n");
    term_write("  heap          kernel heap usage\n");
    term_write("  logoff        shut down (ACPI)\n");
    term_write("  \033[90m(for debug tools type 'help d-')\033[0m\n");
    return 0;
}

static int builtin_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    term_clear();
    return 0;
}

static int builtin_about(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("Berin v0.0.3 (x86_64)\n");
    term_write("Berin. Your own system.\n");
    term_write("vendor: Turker Games\n");
    term_write("mascot: Sorsha, green fox\n");
    return 0;
}

static int builtin_versi(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("Berin kernel 0.0.3 \"Sorsha\"\n");
    term_write("  kernel   0.0.3   x86_64, long mode\n");
    term_write("  kerbash  0.3     kernel shell\n");
    term_write("  serial   0.1     COM1 115200 8N1\n");
    term_write("  keyboard 0.1     PS/2 scancode set 1\n");
    term_write("  idt/pic/pit 0.2  256 vectors, 100 Hz\n");
    term_write("  vga      0.2     text mode 3, 80x25\n");
    term_write("vendor: Turker Games\n");
    return 0;
}

static int builtin_colors(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("fg: ");
    for (int c = 30; c <= 37; c++) {
        char b[8]; int n = 0;
        b[n++] = 0x1B; b[n++] = '['; b[n++] = '0' + (c/10); b[n++] = '0' + (c%10); b[n++] = 'm';
        b[n++] = '0' + (c - 30); b[n++] = ' '; b[n++] = 0;
        term_write(b);
    }
    term_write("\033[0m\nbright: ");
    for (int c = 90; c <= 97; c++) {
        char b[8]; int n = 0;
        b[n++] = 0x1B; b[n++] = '['; b[n++] = '0' + (c/10); b[n++] = '0' + (c%10); b[n++] = 'm';
        b[n++] = '0' + (c - 90); b[n++] = ' '; b[n++] = 0;
        term_write(b);
    }
    term_write("\033[0m\n");
    return 0;
}

static int builtin_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t t = get_ticks();
    term_write("up ");
    put_u64(t / 100);
    term_putchar('.');
    term_putchar('0' + (t / 10) % 10);
    term_putchar('0' + t % 10);
    term_write(" s (");
    put_u64(t);
    term_write(" ticks @ 100 Hz)\n");
    return 0;
}

static int builtin_crash(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ WARN ] crash: divide error\n");
    __asm__ __volatile__(
        "movq $0, %%rax\n\t"
        "movq $1, %%rbx\n\t"
        "divq %%rax\n\t"
        : : : "rax", "rbx", "rdx");
    return 0;
}

static int builtin_pf(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ WARN ] pf: reading unmapped 0xDEADBEEF\n");
    volatile uint64_t *bad = (volatile uint64_t *)0xDEADBEEF;
    uint64_t v = *bad;
    (void)v;
    return 0;
}

static int builtin_logoff(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ INFO ] logoff: halting\n");
    logoff_now();
    return 42;
}

static int builtin_heap(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("kernel heap:\n");
    term_write("  total: "); put_u64(heap_total()); term_write(" B\n");
    term_write("  used:  "); put_u64(heap_used());  term_write(" B\n");
    term_write("  free:  "); put_u64(heap_free());  term_write(" B\n");
    return 0;
}

static int builtin_kmtest(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t before = heap_used();
    void *a = kmalloc(64);
    void *b = kmalloc(128);
    void *c = kmalloc(32);
    term_write("a=0x"); put_hex64((uint64_t)a); term_putchar('\n');
    term_write("b=0x"); put_hex64((uint64_t)b); term_putchar('\n');
    term_write("c=0x"); put_hex64((uint64_t)c); term_putchar('\n');
    if (a) { for (int i = 0; i < 64;  i++) ((uint8_t *)a)[i] = 0xAA; }
    if (b) { for (int i = 0; i < 128; i++) ((uint8_t *)b)[i] = 0xBB; }
    if (c) { for (int i = 0; i < 32;  i++) ((uint8_t *)c)[i] = 0xCC; }
    kfree(b); kfree(a); kfree(c);
    uint64_t after = heap_used();
    if (after == before) term_write("heap restored: OK\n");
    else                 term_write("heap MISMATCH\n");
    return 0;
}

static int builtin_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t total = pmm_total_pages();
    uint64_t free_ = pmm_free_pages();
    uint64_t used  = pmm_used_pages();
    term_write("physical memory (4K pages):\n");
    term_write("  total: "); put_u64(total); term_write(" pages = "); put_u64(total * 4 / 1024); term_write(" MB\n");
    term_write("  used:  "); put_u64(used);  term_write(" pages = "); put_u64(used  * 4 / 1024); term_write(" MB\n");
    term_write("  free:  "); put_u64(free_); term_write(" pages = "); put_u64(free_ * 4 / 1024); term_write(" MB\n");
    return 0;
}

static int builtin_memtest(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t before = pmm_free_pages();
    void *a = pmm_alloc_page();
    void *b = pmm_alloc_page();
    void *c = pmm_alloc_page();
    term_write("a=0x"); put_hex64((uint64_t)a); term_putchar('\n');
    term_write("b=0x"); put_hex64((uint64_t)b); term_putchar('\n');
    term_write("c=0x"); put_hex64((uint64_t)c); term_putchar('\n');
    pmm_free_page(b); pmm_free_page(a); pmm_free_page(c);
    uint64_t after = pmm_free_pages();
    if (after == before) term_write("free pages restored: OK\n");
    else                 term_write("free pages MISMATCH\n");
    return 0;
}

static int builtin_paging(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("paging:\n");
    term_write("  enabled : "); term_write(paging_enabled() ? "yes" : "no"); term_putchar('\n');
    term_write("  CR3     : 0x"); put_hex64(paging_get_cr3()); term_putchar('\n');
    term_write("  CR2     : 0x"); put_hex64(paging_get_cr2()); term_putchar('\n');
    volatile uint64_t *probe = (volatile uint64_t *)0x100000;
    uint64_t v = *probe;
    term_write("  probe[0x100000] = 0x"); put_hex64(v); term_putchar('\n');
    term_write("  identity map: OK\n");
    return 0;
}

static int builtin_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ INFO ] halt: stopping CPU\n");
    for (;;) __asm__ __volatile__("cli; hlt");
    return 42;
}

static int builtin_bread(int argc, char **argv) {
    (void)argc; (void)argv;
    static uint8_t buf[4096];
    if (bdev_read(1, buf) != 0) { term_write("[ FAIL ] bdev_read\n"); return 0; }
    struct berfs_superblock *sb = (struct berfs_superblock *)buf;
    term_write("[ OK ] read block 1\n");
    term_write("  magic:   0x"); put_hex64(sb->sb_magic); term_putchar('\n');
    term_write("  version: ");   put_u64(sb->sb_version); term_putchar('\n');
    term_write("  blocks:  ");   put_u64(sb->sb_total_blocks); term_putchar('\n');
    term_write("  inodes:  ");   put_u64(sb->sb_total_inodes); term_putchar('\n');
    term_write("  free blk:");   put_u64(sb->sb_free_blocks); term_putchar('\n');
    term_write("  free ino:");   put_u64(sb->sb_free_inodes); term_putchar('\n');
    if (sb->sb_magic == BERFS_MAGIC) term_write("  BerFS: OK\n");
    else term_write("  BerFS: bad magic\n");
    return 0;
}

static int builtin_bls(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!g_root.mounted) { term_write("[ FAIL ] not mounted\n"); return 0; }
    term_write("root/\n");
    uint32_t pos = 0, ino;
    char name[BERFS_NAME_MAX + 1];
    uint8_t type;
    int r;
    while ((r = vfs_listdir("/", &pos, name, &ino, &type)) == 1) {
        term_write(type == VFS_DIR ? "  d " : "  - ");
        put_u64(ino); term_putchar(' ');
        term_write(name); term_putchar('\n');
    }
    return 0;
}

static int builtin_bcat(int argc, char **argv) {
    if (argc < 2) { term_write("usage: bcat <path>\n"); return 0; }
    int fd = vfs_open(argv[1], O_RDONLY);
    if (fd < 0) {
        term_write("\033[91mnot_found: "); term_write(argv[1]); term_write("\033[0m\n");
        return 0;
    }
    char buf[128];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) term_putchar(buf[i]);
    }
    vfs_close(fd);
    return 0;
}

static int builtin_elf(int argc, char **argv) {
    if (argc < 2) { term_write("usage: elf <path>\n"); return 0; }

    uint32_t size; uint8_t type;
    if (vfs_stat(argv[1], &size, &type) != 0) { term_write("[ FAIL ] not found\n"); return 0; }
    term_write("[ elf ] stat size: "); put_u64(size); term_putchar('\n');

    void *code = kmalloc(size);
    if (!code) { term_write("[ FAIL ] kmalloc\n"); return 0; }

    int fd = vfs_open(argv[1], O_RDONLY);
    if (fd < 0) { kfree(code); term_write("[ FAIL ] open\n"); return 0; }

    int n = vfs_read(fd, code, size);
    term_write("[ elf ] read returned: "); put_u64(n); term_putchar('\n');

    if (n != (int)size) {
        vfs_close(fd); kfree(code);
        term_write("[ FAIL ] read mismatch\n");
        return 0;
    }
    vfs_close(fd);

    term_write("[ elf ] first 16 bytes: ");
    for (int i = 0; i < 16; i++) {
        char hex[] = "0123456789ABCDEF";
        term_putchar(hex[((uint8_t*)code)[i] >> 4]);
        term_putchar(hex[((uint8_t*)code)[i] & 0xF]);
        term_putchar(' ');
    }
    term_putchar('\n');

    uint64_t *pml4 = (uint64_t *)paging_get_cr3();
    uint64_t entry = 0;

    int elf_ret = elf_load(code, size, pml4, &entry);
    term_write("[ elf ] elf_load returned: "); put_u64(elf_ret); term_putchar('\n');
    term_write("[ elf ] entry after load: 0x"); put_hex64(entry); term_putchar('\n');

    if (elf_ret != 0) {
        kfree(code);
        term_write("[ FAIL ] elf_load\n");
        return 0;
    }
    kfree(code);

    void *stack_phys = pmm_alloc_page();
    if (!stack_phys) { term_write("[ FAIL ] stack alloc\n"); return 0; }

    uint64_t user_stack_vaddr = 0x7FFFFFFFE000;
    vmm_map_page(pml4, user_stack_vaddr, (uint64_t)stack_phys, 0x07); // Упрощены флаги
    uint64_t user_rsp = user_stack_vaddr + 4096 - 16;

    term_write("[ elf ] entering ring 3 at 0x"); put_hex64(entry); term_putchar('\n');

    uint64_t rc = enter_user_mode(entry, user_rsp);
    term_write("[ elf ] user returned, code="); put_u64(rc); term_putchar('\n');
    return 0;
}

static int builtin_vmtest(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ vmm ] creating new PML4...\n");
    uint64_t *new_cr3 = vmm_create_pml4();
    if (!new_cr3) { term_write("[ FAIL ] pmm_alloc_page\n"); return 0; }
    term_write("  new CR3: 0x"); put_hex64((uint64_t)new_cr3); term_putchar('\n');
    uint64_t phys = vmm_get_phys(new_cr3, 0x100000);
    term_write("  virt 0x100000 -> phys 0x"); put_hex64(phys); term_putchar('\n');
    term_write("[ vmm ] mapping user page at 0x400000...\n");
    void *user_phys = pmm_alloc_page();
    if (!user_phys) { term_write("  [FAIL] alloc\n"); return 0; }
    vmm_map_page(new_cr3, 0x400000, (uint64_t)user_phys, 0x07); // Упрощены флаги
    uint64_t user_phys2 = vmm_get_phys(new_cr3, 0x400000);
    term_write("  virt 0x400000 -> phys 0x"); put_hex64(user_phys2); term_putchar('\n');
    if (user_phys2 == (uint64_t)user_phys) term_write("[ OK ] VMM works!\n");
    else term_write("[ FAIL ] mapping mismatch\n");
    return 0;
}

static int builtin_disks(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ SYSTEM DISKS ]\n");
    term_write("  dsk000 (sd*):\n");
    term_write("    model:   "); term_write(ata_model()); term_putchar('\n');
    term_write("    sectors: "); put_u64(ata_sector_count()); term_putchar('\n');
    term_write("    size:    "); put_u64(ata_sector_count() * 512 / 1024 / 1024); term_write(" MB\n");
    return 0;
}

static int builtin_fat(int argc, char **argv) {
    (void)argc; (void)argv;
    int r = fat32_init();
    if (r != 0) {
        term_write("[ FAIL ] fat32_init: ");
        if (r == -1)      term_write("no drive (hdb)\n");
        else if (r == -2) term_write("read error (hdb)\n");
        else if (r == -10) term_write("bad sector size\n");
        else if (r == -11) term_write("not FAT32\n");
        else { term_write("unknown ("); put_u64((uint64_t)(r < 0 ? -r : r)); term_write(")\n"); }
        return 0;
    }
    fat32_info();
    return 0;
}

static int builtin_fatls(int argc, char **argv) {
    (void)argc; (void)argv;
    if (fat32_init() != 0) {
        term_write("[ FAIL ] fat32_init: cannot mount\n");
        return 0;
    }
    fat32_ls();
    return 0;
}

static int builtin_fatcat(int argc, char **argv) {
    if (argc < 2) { term_write("usage: fatcat <FILE.TXT>\n"); return 0; }
    if (fat32_init() != 0) { term_write("[ FAIL ] fat32_init\n"); return 0; }
    char buf[256];
    int n = fat32_read(argv[1], buf, sizeof(buf) - 1);
    if (n < 0) term_write("[ FAIL ] file not found or read error\n");
    else { buf[n] = '\0'; term_write(buf); term_write("\n"); }
    return 0;
}

static int builtin_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("[ INFO ] reboot: resetting\n");
    reboot_now();
    return 42;
}

static int builtin_ata(int argc, char **argv) {
    (void)argc; (void)argv;
    int rc = ata_init();
    if (rc != 0) {
        term_write("[ FAIL ] ata_init: "); put_u64((uint64_t)rc); term_putchar('\n');
        return 0;
    }
    term_write("[ OK ] ata: "); term_write(ata_model());
    term_write("\n  sectors: "); put_u64(ata_sector_count());
    term_write("  ("); put_u64(ata_sector_count() * 512 / 1024 / 1024); term_write(" MB)\n");
    return 0;
}

static int builtin_atatest(int argc, char **argv) {
    (void)argc; (void)argv;
    static uint8_t buf[512];
    term_write("[ ata ] reading sector 0...\n");
    if (ata_read_sectors(0, 1, buf) != 0) { term_write("[ FAIL ] read\n"); return 0; }
    term_write("  first 16 bytes: ");
    for (int i = 0; i < 16; i++) {
        term_putchar("0123456789ABCDEF"[(buf[i] >> 4) & 0xF]);
        term_putchar("0123456789ABCDEF"[buf[i] & 0xF]);
        term_putchar(' ');
    }
    term_putchar('\n');
    for (int i = 0; i < 512; i++) buf[i] = (uint8_t)(i & 0xFF);
    if (ata_write_sectors(100, 1, buf) != 0) { term_write("[ FAIL ] write\n"); return 0; }
    term_write("[ OK ] wrote sector 100\n");
    static uint8_t buf2[512];
    if (ata_read_sectors(100, 1, buf2) != 0) { term_write("[ FAIL ] read back\n"); return 0; }
    for (int i = 0; i < 512; i++) {
        if (buf2[i] != (uint8_t)(i & 0xFF)) {
            term_write("[ FAIL ] mismatch at "); put_u64((uint64_t)i); term_putchar('\n');
            return 0;
        }
    }
    term_write("[ OK ] read back matches\n");
    return 0;
}

static int builtin_whoami(int argc, char **argv) {
    (void)argc; (void)argv;
    term_write("kerbash root\n");
    return 0;
}

/* ============================================================
 *  Shell Loop
 * ============================================================ */
struct builtin {
    const char *name;
    int (*fn)(int argc, char **argv);
};

static struct builtin builtins[] = {
    { "echo",    builtin_echo    },
    { "help",    builtin_help    },
    { "clear",   builtin_clear   },
    { "about",   builtin_about   },
    { "versi",   builtin_versi   },
    { "colors",  builtin_colors  },
    { "bls",     builtin_bls     },
    { "bcat",    builtin_bcat    },
    { "elf",     builtin_elf     },
    { "vmtest",  builtin_vmtest  },
    { "uptime",  builtin_uptime  },
    { "bread",   builtin_bread   },
    { "disks",   builtin_disks   },
    { "ata",     builtin_ata     },
    { "atatest", builtin_atatest },
    { "crash",   builtin_crash   },
    { "pf",      builtin_pf      },
    { "fatls",   builtin_fatls   },
    { "reboot",  builtin_reboot  },
    { "heap",    builtin_heap    },
    { "kmtest",  builtin_kmtest  },
    { "fat",     builtin_fat     },
    { "fatcat",  builtin_fatcat  },
    { "halt",    builtin_halt    },
    { "meminfo", builtin_meminfo },
    { "whoami",  builtin_whoami  },
    { "memtest", builtin_memtest },
    { "paging",  builtin_paging  },
    { "logoff",  builtin_logoff  },
    { 0, 0 },
};

static void print_prompt(void) {
    term_write("\033[90m[\033[91mroot\033[90m@\033[92mberin\033[90m \033[94m/\033[90m]\033[97m# \033[0m");
}

static void print_command_not_found(const char *name) {
    term_write("\033[91mcommand_not_found: ");
    term_write(name);
    term_write("\033[0m\n");
}

static void line_edit(void) {
    line_len = 0;
    for (;;) {
        char c = input_getchar();
        if (c == '\r' || c == '\n') {
            term_putchar('\n');
            line_buf[line_len] = '\0';
            return;
        }
        if (c == 0x7F || c == '\b') {
            if (line_len > 0) {
                line_len--;
                term_putchar('\b');
                term_putchar(' ');
                term_putchar('\b');
            }
            continue;
        }
        if ((unsigned char)c < 0x20) continue;
        if (line_len < MAX_LINE - 1) {
            line_buf[line_len++] = c;
            term_putchar(c);
        }
    }
}

void kerbash_run(void) {
    for (;;) {
        print_prompt();
        line_edit();
        if (line_len == 0) continue;

        char *argv[MAX_ARGS];
        int argc = tokenize(line_buf, argv, MAX_ARGS);
        if (argc == 0) continue;

        const char *cmd = argv[0];
        int found = 0;
        for (int i = 0; builtins[i].name; i++) {
            if (str_eq(builtins[i].name, cmd)) {
                int rc = builtins[i].fn(argc, argv);
                if (rc == 42) return;
                found = 1;
                break;
            }
        }
        if (!found) print_command_not_found(cmd);
    }
}
