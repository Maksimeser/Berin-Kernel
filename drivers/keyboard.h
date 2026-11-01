#ifndef BERIN_KEYBOARD_H
#define BERIN_KEYBOARD_H

void kb_init(void);
void kb_handler(void);
int  kb_ready(void);
char kb_getchar(void);

#endif
