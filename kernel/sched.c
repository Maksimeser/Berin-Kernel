#include "sched.h"
#include "heap.h"
#include "pmm.h"
#include "kstring.h"
#include "vmm.h"
#include "paging.h"
#include "gdt.h"

#define KSTACK_PAGES 1   /* 1 страница = 4 КБ, нам хватит */

/* PID 0 (idle) — статически в .bss */
static struct task idle_task;
static uint8_t     idle_kstack[KSTACK_PAGES * 4096] __attribute__((aligned(16)));

struct task *current_task = 0;
struct task *kb_waiting_task = 0;
static uint32_t next_pid = 1;

// Функция, в которую мы прыгнем, когда планировщик переключится на idle
void idle_loop(void) {
    for (;;) {
        __asm__ __volatile__("sti; hlt"); // Ждем прерывания
        schedule();                       // Проверяем, не проснулся ли кто-то
    }
}

void sched_init(void) {
    memset(&idle_task, 0, sizeof(idle_task));
    idle_task.pid = 0;
    idle_task.state = TASK_IDLE;
    idle_task.next = &idle_task;

    extern uint64_t paging_get_cr3(void);
    idle_task.pml4 = (uint64_t *)paging_get_cr3();
    idle_task.kstack_base = idle_kstack;
    idle_task.kstack_top  = (uint64_t)idle_kstack + sizeof(idle_kstack);

    idle_task.kernel_rsp = 0;

    current_task = &idle_task;
    tss_set_rsp0(idle_task.kstack_top);
    next_pid = 1;
}

static uint8_t *alloc_kstack(void) {
    return (uint8_t *)pmm_alloc_page();
}

struct task *task_create(uint64_t entry, uint64_t *pml4, uint64_t user_rsp) {
    struct task *t = (struct task *)kmalloc(sizeof(struct task));
    if (!t) return 0;
    memset(t, 0, sizeof(struct task));
    t->uid = 0;
    t->gid = 0;
    t->pid = next_pid++;
    t->parent_pid = current_task->pid;
    t->state = TASK_READY;
    t->pml4 = pml4;

    uint8_t *kstack = alloc_kstack();
    if (!kstack) return 0;
    t->kstack_base = kstack;
    t->kstack_top  = (uint64_t)kstack + KSTACK_PAGES * 4096;

    uint64_t *sp = (uint64_t *)t->kstack_top;
    *--sp = 0x23;       // SS
    *--sp = user_rsp;   // RSP
    *--sp = 0x202;      // RFLAGS
    *--sp = 0x1B;       // CS
    *--sp = entry;      // RIP
    *--sp = (uint64_t)task_ret_to_user; // Адрес возврата для switch_to
    for (int i = 0; i < 6; i++) *--sp = 0; // 6 регистров
    t->kernel_rsp = (uint64_t)sp;

    t->next = current_task->next;
    current_task->next = t;

    return t;
}

void schedule(void) {
    struct task *prev = current_task;
    struct task *next = current_task->next;
    if (!next || next == current_task) return;

    // Ищем готовую задачу
    while (next != current_task) {
        if (next->state == TASK_READY) break;
        next = next->next;
    }

    if (next == current_task) {
        // Если никого нет, и мы не idle, принудительно идем в idle
        if (prev->pid != 0) {
            next = &idle_task;
        } else {
            return; // Мы уже idle, и делать нечего
        }
    }

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    current_task = next;

    if (current_task->state != TASK_IDLE) {
        current_task->state = TASK_RUNNING;
    }

    if (current_task->kstack_top) tss_set_rsp0(current_task->kstack_top);
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(current_task->pml4));

    switch_to(prev, current_task);
}

void task_exit(int code) {
    current_task->exit_code = code;
    current_task->state = TASK_ZOMBIE;

    // Пробуждаем родителя, если он ждет нас, и перепривязываем детей
    struct task *t = current_task->next;
    while (t != current_task) {
        if (t->pid == current_task->parent_pid && t->state == TASK_WAITING) {
            t->state = TASK_READY; // Будим login/init
        }
        if (t->parent_pid == current_task->pid) {
            t->parent_pid = 1; // Сироты идут к init
        }
        t = t->next;
    }

    schedule();
    for (;;) __asm__ __volatile__("cli; hlt");
}

int task_reap(uint32_t pid) {
    struct task *prev = current_task;
    struct task *curr = current_task->next;

    while (curr != current_task) {
        if (curr->pid == pid && curr->state == TASK_ZOMBIE) {
            prev->next = curr->next;
            int code = curr->exit_code;

            if (curr->kstack_base) {
                pmm_free_page(curr->kstack_base);
            }
            kfree(curr);
            return code;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}
