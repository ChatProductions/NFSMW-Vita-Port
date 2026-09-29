#include "quick_race.h"
int quick_race_valid(const QuickRace*q){return q&&(q->event==2||q->event==3)&&q->difficulty>=0&&q->difficulty<=2&&(q->traffic==0||q->traffic==1);}
