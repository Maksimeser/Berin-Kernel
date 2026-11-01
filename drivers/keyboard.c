#include "keyboard.h"
#include "io.h"
#include "sched.h"

#define KB_DATA   0x60
#define KB_STATUS 0x64
#define BUF_SIZE  256

static const char map_lower[128] = {
    0,   27,  '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0,   ' ',
};

static const char map_upper[128] = {
    0,   27,  '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0,   ' ',
};

static int shift_down = 0;
static int caps_lock = 0;
static char buf[BUF_SIZE];
static volatile int head, tail;

void kb_init(void) {
    while (inb(KB_STATUS) & 0x01) (void)inb(KB_DATA);
    head = tail = 0;
    shift_down = 0;
    caps_lock = 0;
}

void kb_handler(void) {
    uint8_t sc = inb(KB_DATA);

    /* Отпускание клавиши */
    if (sc & 0x80) {
        uint8_t code = sc & 0x7F;
        if (code == 0x2A || code == 0x36) shift_down = 0;
        return;
    }

    /* Нажатие Shift */
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }

    /* Нажатие Caps Lock */
    if (sc == 0x3A) { caps_lock = !caps_lock; return; }

    /* Определяем, нужен ли верхний регистр */
    int use_upper = 0;
    if (sc >= 0x10 && sc <= 0x32) {
        use_upper = shift_down ^ caps_lock;
    } else {
        use_upper = shift_down;
    }

    char c = use_upper ? map_upper[sc] : map_lower[sc];
    if (!c) return;

    int next = (head + 1) % BUF_SIZE;
    if (next != tail) { buf[head] = c; head = next; }

    // ВОТ СЮДА ВСТАВЬ ЭТОТ БЛОК (внутрь функции, перед закрывающей скобкой):
    extern struct task *kb_waiting_task;
    if (kb_waiting_task) {
        kb_waiting_task->state = TASK_READY;
        kb_waiting_task = 0;
    }
}

int  kb_ready(void) { return head != tail; }
char kb_getchar(void) {
    while (head == tail) __asm__ __volatile__("pause");
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}
