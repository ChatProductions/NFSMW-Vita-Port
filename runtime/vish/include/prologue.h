#ifndef PROLOGUE_H
#define PROLOGUE_H
#include "drive_scene.h"
typedef struct {DrivePoint call,kill;float call_radius,kill_radius,call_seconds,broken_seconds;int event,called,broken,transition;} Prologue;
int prologue_load(Prologue*p,const char*path,int event);
void prologue_reset(Prologue*p);
/* Returns one only on the first encounter with the call marker. */
int prologue_step(Prologue*p,const DriveCar*car,float dt);
#endif
