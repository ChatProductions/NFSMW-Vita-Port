#ifndef BREAKABLE_PROPS_H
#define BREAKABLE_PROPS_H
#include "drive_scene.h"
int props_load(const char*path);
void props_reset(void);
void props_step(float dt);
void props_hit(const DriveCar*before,const DriveCar*after);
int props_triangle(const DriveTriangle*t,unsigned material);
void props_transform(const DriveTriangle*t,unsigned material,DriveTriangle*out);
#endif
