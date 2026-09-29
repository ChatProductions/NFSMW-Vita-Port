#ifndef NFSMW_WORLD_MAP_H
#define NFSMW_WORLD_MAP_H
#include "free_roam.h"
#include "first_race.h"
#include <vita2d.h>
/* Modal map freezes simulation; returns after closing or a graphics failure. */
void world_map_run(vita2d_pgf *font,const FreeRoam *roads,const DriveState *state,const FirstRace *race);
#endif
