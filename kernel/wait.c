/* Berin OS - wait spinner + progress bar (Phase 4.4) */

#include "wait.h"

extern void term_write(const char *s);
extern void term_putchar(char c);
extern void term_set_fg(uint8_t fg);

#define C_GREEN   10
#define C_YELLOW  14
#define C_GRAY     7
#define C_RED     12

static char     wait_label[64];
static int      wait_label_len;
static int      wait_started;
static uint32_t wait_counter;

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void copy_label(const char *s) {
    int i = 0;
    while (s[i] && i < (int)sizeof(wait_label) - 1) { wait_label[i] = s[i]; i++; }
    wait_label[i] = '\0';
    wait_label_len = i;
}

static void erase_line(void) {
    term_putchar('\r');
    for (int i = 0; i < wait_label_len + 8; i++) term_putchar(' ');
    term_putchar('\r');
}

void wait_start(const char *label) {
    copy_label(label);
    wait_started = 1;
    wait_counter = 0;

    term_set_fg(C_GRAY);
    term_write(wait_label);
    term_write("...");
    term_set_fg(C_GREEN);
}

void wait_step(void) {
    if (!wait_started) return;
    wait_counter++;
    /* spinner: | / - \ ; step every call for simplicity */
    char spin = "|/-\\"[wait_counter & 3];
    term_putchar('\b');
    term_putchar(spin);
}

void wait_done_ok(void) {
    if (!wait_started) return;
    erase_line();
    term_set_fg(C_GREEN);
    term_write("[ OK ] ");
    term_write(wait_label);
    term_putchar('\n');
    wait_started = 0;
}

void wait_done_fail(const char *reason) {
    if (!wait_started) return;
    erase_line();
    term_set_fg(C_RED);
    term_write("[ FAIL ] ");
    term_write(wait_label);
    term_write(": ");
    term_write(reason);
    term_putchar('\n');
    term_set_fg(C_GREEN);
    wait_started = 0;
}

/* ---------- progress bar ---------- */

static uint32_t prog_total, prog_cur;
static int      prog_label_len;
static char     prog_label[64];
static int      prog_started;

void progress_start(const char *label, uint32_t total) {
    int i = 0;
    while (label[i] && i < (int)sizeof(prog_label) - 1) { prog_label[i] = label[i]; i++; }
    prog_label[i] = '\0';
    prog_label_len = i;

    prog_total = total ? total : 1;
    prog_cur   = 0;
    prog_started = 1;

    term_set_fg(C_GRAY);
    term_write(prog_label);
    term_write(" [");
    for (int k = 0; k < 30; k++) term_putchar('-');
    term_write("]   0%");
    term_set_fg(C_GREEN);
}

void progress_update(uint32_t current) {
    if (!prog_started) return;
    prog_cur = current;
    uint32_t pct = (current * 100u) / prog_total;
    if (pct > 100) pct = 100;
    int fill = (int)((pct * 30u) / 100u);

    /* move cursor back to start of bar */
    int back = 30 - fill + 1 + 4;    /* bar chars + bracket + " 999%" space */
    term_putchar('\r');
    /* write full line */
    term_set_fg(C_GRAY);
    term_write(prog_label);
    term_write(" [");
    term_set_fg(C_GREEN);
    for (int k = 0; k < fill; k++) term_putchar('#');
    term_set_fg(C_GRAY);
    for (int k = fill; k < 30; k++) term_putchar('-');
    term_write("] ");
    if (pct < 10)  term_putchar(' ');
    if (pct < 100) term_putchar(' ');
    /* percent number */
    char buf[4]; int n = 0;
    if (pct == 0) buf[n++] = '0';
    else { uint32_t v = pct; char rev[4]; int r = 0; while (v) { rev[r++] = '0' + v % 10; v /= 10; } while (r) buf[n++] = rev[--r]; }
    for (int k = 0; k < n; k++) term_putchar(buf[k]);
    term_putchar('%');
    term_set_fg(C_GREEN);
    (void)back;
}

void progress_done(void) {
    if (!prog_started) return;
    progress_update(prog_total);
    term_putchar('\n');
    prog_started = 0;
}
