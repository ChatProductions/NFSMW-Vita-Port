#ifndef DRIVE_HUD_H
#define DRIVE_HUD_H
#include "drive_scene.h"
#include "free_roam.h"
#include "first_race.h"
#include <vita2d.h>
void drive_hud_race(vita2d_pgf*font,const FirstRace*r,const DriveState*d);
void drive_hud_load(void);
void drive_hud_free(void);
void drive_hud_draw(vita2d_pgf *font,const DriveState*d,float fps,int show_help,int stream_error);
void drive_hud_pause(vita2d_pgf*font,int choice,int camera_mode,int show_help,int show_fps,int confirm);
void drive_hud_roam(vita2d_pgf*f,FreeRoam*r,const DriveState*d);
#endif
