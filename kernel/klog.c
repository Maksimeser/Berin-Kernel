#include "klog.h"

static int g_klog_enabled = 0;

int  klog_enabled(void) { return g_klog_enabled; }
void klog_set(int on)   { g_klog_enabled = on ? 1 : 0; }
