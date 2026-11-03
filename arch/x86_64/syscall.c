#include "syscall.h"
#include "vfs.h"
#include "kstring.h"
#include "elf.h"
#include "sched.h"
#include "paging.h"
#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "io.h"

extern void term_write(const char *s);
extern void term_putchar(char c);
extern void reboot_now(void);
extern void logoff_now(void);
extern uint64_t enter_user_mode(uint64_t entry, uint64_t user_rsp);
extern uint64_t kernel_rsp_saved;

/* ================= ФАЙЛОВЫЕ СИСТЕМНЫЕ ВЫЗОВЫ ================= */

static uint64_t sys_write(uint64_t fd, const void *buf, uint64_t len) {
    if (fd == 1 || fd == 2) {
        for (uint64_t i = 0; i < len; i++) term_putchar(((const char *)buf)[i]);
        return len;
    }
    int r = vfs_write((int)fd, buf, (uint32_t)len);
    if (r < 0) return (uint64_t)-1;
    return (uint64_t)r;
}

static uint64_t sys_read(uint64_t fd, void *buf, uint64_t len) {
    if (fd == 0) {
        extern char berin_input_getchar(void);
        extern struct task *kb_waiting_task;
        extern void schedule(void);
        extern int kb_ready(void);
        extern int serial_rx_ready(void);

        uint8_t *p = (uint8_t *)buf;
        uint64_t n = 0;
        while (n < len) {
            if (!kb_ready() && !serial_rx_ready()) {
                current_task->state = TASK_WAIT_KB;
                kb_waiting_task = current_task;
                schedule(); // Засыпаем
                continue;
            }
            char c = berin_input_getchar();
            if (c == 3) return (uint64_t)-2; // Ctrl+C
            if (c == '\r' || c == '\n') { term_putchar('\n'); p[n++] = '\n'; break; }
            if (c == 0x7F || c == '\b') {
                if (n > 0) { term_putchar('\b'); term_putchar(' '); term_putchar('\b'); }
                p[n++] = (uint8_t)c;
                continue;
            }
            if ((unsigned char)c < 0x20) continue;
            p[n++] = (uint8_t)c;
            term_putchar(c);
        }
        return n;
    }
    int n = vfs_read((int)fd, buf, (uint32_t)len);
    if (n < 0) return (uint64_t)-1;
    return (uint64_t)n;
}

static uint64_t sys_open(const char *path, uint64_t flags) {
    if (!path) return (uint64_t)-1;
    if ((flags & O_CREAT) && vfs_resolve(path, 0, 0) != 0) {
        vfs_create(path);
    }
    int fd = vfs_open(path, (int)flags);
    if (fd < 0) return (uint64_t)-1;
    return (uint64_t)fd;
}

static uint64_t sys_close(uint64_t fd) { return (uint64_t)vfs_close((int)fd); }

struct sys_stat_buf { uint32_t size; uint8_t type; uint8_t pad[3]; };
static uint64_t sys_stat(const char *path, struct sys_stat_buf *out) {
    if (!path || !out) return (uint64_t)-1;
    uint32_t size; uint8_t type;
    if (vfs_stat(path, &size, &type) != 0) return (uint64_t)-1;
    out->size = size; out->type = type;
    return 0;
}

static uint64_t sys_listdir(const char *path, uint32_t *pos, char *name, uint32_t *ino, uint8_t *type) {
    if (!path || !pos || !name || !ino || !type) return (uint64_t)-1;
    int r = vfs_listdir(path, pos, name, ino, type);
    return r < 0 ? (uint64_t)-1 : (uint64_t)r;
}

static uint64_t sys_mkdir(const char *path, uint64_t mode) {
    (void)mode;
    if (!path) return (uint64_t)-1;
    return (uint64_t)vfs_mkdir(path);
}

static uint64_t sys_unlink(const char *path) {
    if (!path) return (uint64_t)-1;
    return (uint64_t)vfs_unlink(path);
}

/* ================= ПРОЦЕССЫ ================= */
static uint64_t sys_exit(uint64_t code) {
    if (current_task && current_task->pid == 1) {
        extern void term_write(const char *s);
        term_write("\n[ FATAL ] init exited. System halted.\n");
        for (;;) __asm__ __volatile__("cli; hlt");
    }

    task_exit((int)code);
    return 0;
}

static uint64_t sys_spawn(const char *path, uint64_t uid) {
    if (!path) return (uint64_t)-1;
    term_write("[spawn] start\n");

    uint32_t size; uint8_t type;
    if (vfs_stat(path, &size, &type) != 0) { term_write("[spawn] stat FAIL\n"); return (uint64_t)-1; }
    term_write("[spawn] stat OK\n");

    void *code = kmalloc(size);
    if (!code) { term_write("[spawn] kmalloc FAIL\n"); return (uint64_t)-1; }
    term_write("[spawn] kmalloc OK\n");

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { term_write("[spawn] open FAIL\n"); kfree(code); return (uint64_t)-1; }
    term_write("[spawn] open OK\n");

    uint32_t total_read = 0;
    while (total_read < size) {
        int bytes = vfs_read(fd, (uint8_t*)code + total_read, size - total_read);
        if (bytes <= 0) break;
        total_read += bytes;
    }
    vfs_close(fd);
    term_write("[spawn] read OK\n");

    if (total_read != size) { term_write("[spawn] read INCOMPLETE\n"); kfree(code); return (uint64_t)-1; }

    uint64_t *new_pml4 = vmm_create_pml4_clean();
    if (!new_pml4) { term_write("[spawn] pml4 FAIL\n"); kfree(code); return (uint64_t)-1; }
    term_write("[spawn] pml4 OK\n");

    uint64_t entry = 0;
    if (elf_load(code, size, new_pml4, &entry) != 0) {
        term_write("[spawn] elf_load FAIL\n");
        kfree(code);
        return (uint64_t)-1;
    }
    term_write("[spawn] elf_load OK\n");
    kfree(code);

    uint64_t user_stack_top = 0x7FFFFFFFF000;
    for (int i = 0; i < 16; i++) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) { term_write("[spawn] stack pmm FAIL\n"); return (uint64_t)-1; }
        memset(stack_phys, 0, 4096);
        uint64_t vaddr = user_stack_top - (i + 1) * 0x1000;
        if (vmm_map_page(new_pml4, vaddr, (uint64_t)stack_phys, 0x07) != 0) {
            term_write("[spawn] stack map FAIL\n");
            return (uint64_t)-1;
        }
    }
    term_write("[spawn] stack OK\n");

    uint64_t user_rsp = user_stack_top - 16;
    struct task *t = task_create(entry, new_pml4, user_rsp);
    if (!t) { term_write("[spawn] task_create FAIL\n"); return (uint64_t)-1; }

    t->uid = (uint32_t)uid;
    term_write("[spawn] task created OK\n");
    return t->pid;
}

/* ================= ДИСПЕТЧЕР ================= */
static uint64_t sys_execve(struct regs *r, const char *path, char *const argv[]) {
    if (!path) return (uint64_t)-1;

    term_write("[execve] start\n");

    uint32_t size; uint8_t type;
    if (vfs_stat(path, &size, &type) != 0) {
        term_write("[execve] stat FAILED\n");
        return (uint64_t)-1;
    }

    void *code = kmalloc(size);
    if (!code) return (uint64_t)-1;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) { kfree(code); return (uint64_t)-1; }

    uint32_t total_read = 0;
    while (total_read < size) {
        int bytes = vfs_read(fd, (uint8_t*)code + total_read, size - total_read);
        if (bytes <= 0) break;
        total_read += bytes;
    }
    vfs_close(fd);

    if (total_read != size) {
        term_write("[execve] read incomplete!\n");
        kfree(code);
        return (uint64_t)-1;
    }

    term_write("[execve] file read OK\n");

    uint64_t *new_pml4 = current_task->pml4;
    uint64_t entry = 0;

    if (elf_load(code, size, new_pml4, &entry) != 0) {
        term_write("[execve] elf_load FAILED\n");
        kfree(code);
        return (uint64_t)-1;
    }
    kfree(code);

    term_write("[execve] elf loaded OK\n");

    // === СТЕК НА 16 СТРАНИЦ (64 КБ) ===
    uint64_t user_stack_top = 0x7FFFFFFFF000;
    for (int i = 0; i < 16; i++) {
        void *stack_phys = pmm_alloc_page();
        if (!stack_phys) { term_write("[execve] stack FAIL\n"); return (uint64_t)-1; }
        memset(stack_phys, 0, 4096);
        uint64_t vaddr = user_stack_top - (i + 1) * 0x1000;
        if (vmm_map_page(new_pml4, vaddr, (uint64_t)stack_phys, 0x07) != 0) {
            term_write("[execve] map FAIL\n");
            return (uint64_t)-1;
        }
    }

    int argc = 0;
    if (argv) while (argv[argc]) argc++;

    // 1. Собираем argv в KERNEL-КУЧЕ (в старой PML4 всё ещё)
    uint8_t *kbuf = (uint8_t *)kmalloc(64 * 1024);
    if (!kbuf) return (uint64_t)-1;
    uint64_t kbuf_top = (uint64_t)kbuf + 64 * 1024;

    uint64_t *kargv_ptrs = (uint64_t *)kmalloc(sizeof(uint64_t) * (argc + 1));
    if (!kargv_ptrs) { kfree(kbuf); return (uint64_t)-1; }

    for (int i = 0; i < argc; i++) {
        int len = 0;
        while (argv[i][len]) len++;
        len++;
        kbuf_top -= len;
        memcpy((void *)kbuf_top, argv[i], len);
        kargv_ptrs[i] = kbuf_top;
    }

    kbuf_top &= ~0xFULL;
    kbuf_top -= 8; *(uint64_t *)kbuf_top = 0;  // envp NULL
    kbuf_top -= 8; *(uint64_t *)kbuf_top = 0;  // argv[argc] NULL

    for (int i = argc - 1; i >= 0; i--) {
        kbuf_top -= 8;
        *(uint64_t *)kbuf_top = kargv_ptrs[i];
    }

    if ((kbuf_top & 0xF) != 8) { kbuf_top -= 8; *(uint64_t *)kbuf_top = 0; }
    kbuf_top -= 8;
    *(uint64_t *)kbuf_top = argc;

    uint64_t offset = (uint64_t)kbuf + 64 * 1024 - kbuf_top;
    uint64_t user_rsp = user_stack_top - offset;

    // 2. ПЕРЕКЛЮЧАЕМ PML4 — теперь user stack доступен
    current_task->pml4 = new_pml4;
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(new_pml4));

    // 3. Копируем из kernel-кучи в user stack (уже в новой PML4)
    memcpy((void *)user_rsp, (void *)kbuf_top, offset);

    kfree(kargv_ptrs);
    kfree(kbuf);

    r->rip = entry;
    r->rsp = user_rsp;
    r->rax = 0;
    r->rbx = 0;
    r->rcx = 0;
    r->rdx = 0;
    r->rbp = 0;
    r->rsi = 0;
    r->rdi = 0;

    term_write("[execve] jumping to user space\n");
    return 0;
}

static uint64_t sys_read_noecho(void *buf, uint64_t len) {
    extern char berin_input_getchar(void);
    __asm__ __volatile__("sti");
    uint8_t *p = (uint8_t *)buf;
    uint64_t n = 0;
    while (n < len - 1) {
        char c = berin_input_getchar();
        if (c == '\r' || c == '\n') { term_putchar('\n'); break; }
        if (c == 0x7F || c == '\b') {
            if (n > 0) { n--; term_putchar('\b'); term_putchar(' '); term_putchar('\b'); }
            continue;
        }
        if ((unsigned char)c < 0x20) continue;
        p[n++] = (uint8_t)c;
        term_putchar('*');
    }
    p[n] = '\0';
    return n;
}

static uint64_t sys_wait4(uint64_t pid, int *status) {
    extern struct task *current_task;
    extern void schedule(void);
    extern int task_reap(uint32_t pid);

    for (;;) {
        struct task *t = current_task->next;
        int found = 0;
        while (t && t != current_task) {
            if (t->pid == pid && t->parent_pid == current_task->pid) {
                found = 1;
                if (t->state == TASK_ZOMBIE) {
                    int code = task_reap(t->pid);
                    if (status) *status = code;
                    return pid;
                }
                break;
            }
            t = t->next;
        }
        if (!found) return (uint64_t)-1;
        current_task->state = TASK_WAITING;
        schedule();
    }
}

static uint64_t sys_getuid(void) {
    return current_task->uid;
}

static uint64_t sys_setuid(uint64_t uid) {
    if (current_task->uid != 0) {
        term_write("[sys] Permission denied (not root)\n");
        return (uint64_t)-1;
    }
    current_task->uid = (uint32_t)uid;
    return 0;
}

static uint64_t sys_reboot(void) {
    reboot_now();
    return 0;
}

static uint64_t sys_shutdown(void) {
    logoff_now();
    return 0;
}

static uint64_t sys_fork(struct regs *r) {
    term_write("[fork] start\n");
    uint64_t *new_pml4 = vmm_fork_pml4();
    term_write("[fork] pml4 done\n");

    if (!new_pml4) return (uint64_t)-1;

    struct task *t = task_create(r->rip, new_pml4, r->rsp);
    term_write("[fork] task done\n");

    if (!t) return (uint64_t)-1;

    t->uid = current_task->uid;
    return t->pid;
}

void syscall_dispatch(struct regs *r) {
    __asm__ __volatile__("sti");

    uint64_t nr  = r->rax;
    uint64_t a1  = r->rdi;
    uint64_t a2  = r->rsi;
    uint64_t a3  = r->rdx;
    uint64_t ret = (uint64_t)-1;

    switch (nr) {
        case 202:         ret = sys_read_noecho((void *)a1, a2);     break;
        case SYS_WRITE:   ret = sys_write(a1, (const void *)a2, a3); break;
        case SYS_OPEN:    ret = sys_open((const char *)a1, a2);      break;
        case SYS_READ:    ret = sys_read(a1, (void *)a2, a3);        break;
        case SYS_CLOSE:   ret = sys_close(a1);                       break;
        case SYS_STAT:    ret = sys_stat((const char *)a1, (struct sys_stat_buf *)a2); break;
        case SYS_LISTDIR: ret = sys_listdir((const char *)a1, (uint32_t *)a2, (char *)a3, (uint32_t *)r->r10, (uint8_t *)r->r8);           break;
        case 83:          ret = sys_mkdir((const char *)a1, a2);     break;
        case 87:          ret = sys_unlink((const char *)a1);        break;
        case 57:          ret = sys_fork(r);                         break;
        case 59:          ret = sys_execve(r, (const char *)a1, (char *const *)a2); break;
        case 60:          sys_exit(a1);                              break;
        case 61:          ret = sys_wait4(a1, (int *)a2);            break;
        case 102:         ret = sys_getuid();                        break;
        case 103:         ret = sys_setuid(a1);                      break;
        case 200:
            term_write("[syscall 200] rdi=0x");
            for (int i = 60; i >= 0; i -= 4) {
                int d = (a1 >> i) & 0xF;
                term_putchar(d < 10 ? '0' + d : 'a' + d - 10);
            }
            term_write("\n");
            ret = sys_spawn((const char *)a1, a2);
            break;
        case 210:         ret = sys_reboot();                        break;
        case 211:         ret = sys_shutdown();                      break;
        default:          term_write("[sys] unknown syscall\n");     break;
    }
    r->rax = ret;
}
