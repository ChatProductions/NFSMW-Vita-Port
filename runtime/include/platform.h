#ifndef NFSMW_PLATFORM_H
#define NFSMW_PLATFORM_H
#include <stdint.h>
int platform_init(void);
uint32_t platform_buttons(void);
void platform_wait_frame(void);
void platform_log_memory(const char *stage);
void platform_log(const char *message);
void platform_exit(void);
#endif
