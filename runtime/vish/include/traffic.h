#ifndef NFSMW_TRAFFIC_H
#define NFSMW_TRAFFIC_H
#include "drive_scene.h"
#define TRAFFIC_MAX 3
typedef struct {DrivePoint a,b;unsigned next[4];} TrafficRoad;
typedef struct {DriveCar car;unsigned edge;float steering;int active,model;} TrafficCar;
typedef struct {TrafficRoad *roads;unsigned count,seed,spawn_cursor;float spawn_timer;TrafficCar cars[TRAFFIC_MAX];} Traffic;
int traffic_open(Traffic*t,const char*path);
void traffic_close(Traffic*t);
void traffic_step(Traffic*t,const DriveScene*s,DriveState*d,float dt);
#endif
