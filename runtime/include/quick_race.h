#ifndef NFSMW_QUICK_RACE_H
#define NFSMW_QUICK_RACE_H
typedef struct {int event,difficulty,traffic;} QuickRace;
int quick_race_valid(const QuickRace*q);
int drive_run_quick(const QuickRace*q);
void quick_race_ui(int custom);
#endif
