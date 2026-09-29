#ifndef NFSMW_MENU_SCENE_H
#define NFSMW_MENU_SCENE_H
#include <stdint.h>
int menu_load(void);
/* 1: root back; 2: resume career; 3: new career; 4: load career. */
int menu_input(uint32_t pressed);
void menu_form(const char*title,const char*const*labels,const char*const*values,unsigned count,unsigned selected,const char*hint);
void menu_draw(void);
void menu_present(const char *const *lines, unsigned count, int keyboard);
void menu_unload(void);
#endif
