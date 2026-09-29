#ifndef WORLD_MOTION_H
#define WORLD_MOTION_H
#include "drive_scene.h"
/* Swept front/rear circles, with a checked tangent slide along a hit wall. */
int drive_world_contact(const DriveScene*s,DriveCar*car,const DriveCar*before,int(*solid)(unsigned));
#endif
