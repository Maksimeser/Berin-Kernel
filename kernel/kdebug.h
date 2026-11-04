#ifndef BERIN_KDEBUG_H
#define BERIN_KDEBUG_H

#include "klog.h"
#include "term.h"

#define KLOG(...)  do { if (klog_enabled()) { term_write(__VA_ARGS__); } } while (0)
#define KLOGP(c)   do { if (klog_enabled()) { term_putchar(c);     } } while (0)

#endif
