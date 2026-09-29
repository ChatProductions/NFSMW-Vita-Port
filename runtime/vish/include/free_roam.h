#ifndef NFSMW_FREE_ROAM_H
#define NFSMW_FREE_ROAM_H
#include "drive_scene.h"
typedef struct {DrivePoint a,b;} RoamRoad;
typedef struct {RoamRoad *roads;unsigned count;unsigned nearby[1024],near_count;int bx,by,cached;DriveCar anchor,recovery;float save_seconds;int save_failed;} FreeRoam;
int roam_open(FreeRoam*r,const char*path);
void roam_close(FreeRoam*r);
int roam_restore(const char*path,DriveScene*s,DriveState*d);
int roam_save(const char*path,const DriveCar*c);
void roam_start(FreeRoam*r,DriveState*d);
void roam_tick(FreeRoam*r,const DriveScene*s,DriveState*d,float dt);
void roam_recover(FreeRoam*r,DriveScene*s,DriveState*d);
void roam_nearby(FreeRoam*r,DrivePoint p);
#endif
