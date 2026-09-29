#ifndef NFSMW_CAREER_STORE_H
#define NFSMW_CAREER_STORE_H
#include <stdint.h>
typedef struct { uint32_t sequence; char alias[32]; uint32_t first_best_ms,first_finishes,ronnie_best_ms,ronnie_finishes; } CareerSave;
/* 0 loaded/saved, 1 absent, -1 invalid/I/O. Never imports PC saved games. */
int career_load(const char *directory, CareerSave *save);
int career_save(const char *directory, const char *alias, CareerSave *save);
int career_record_first(const char *directory, uint32_t milliseconds);
int career_record_ronnie(const char *directory, uint32_t milliseconds);
const char *career_first_event(void);
const char *career_start_car(void);
#endif
