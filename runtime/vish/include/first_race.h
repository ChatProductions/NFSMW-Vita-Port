#ifndef FIRST_RACE_H
#define FIRST_RACE_H
#include "drive_scene.h"
typedef struct {DrivePoint p;float radius,distance,vertical_radius;} RaceGate;
typedef struct {
 const DriveScene *scene;float ai_steering;
 DrivePoint *points;float *lengths;RaceGate *gates;
 unsigned count,gate_count,next,segment;
 float ai_scale;
 DrivePoint ai_anchor;float ai_stuck,ai_reverse,ai_cooldown;unsigned ai_recoveries;
 float start_fraction,start_speed,start_time,opponent_start,opponent_lateral,total,progress,rival_progress,elapsed,rival_speed;
 float countdown,initial_countdown;
 const char *event_id,*opponent_name;
 int active,finished,place;
} FirstRace;
int first_race_load(const char*path,FirstRace*r);
void first_race_free(FirstRace*r);
void first_race_start(FirstRace*r,const DriveScene*s,DriveState*d);
void first_race_step(FirstRace*r,DriveState*d,float dt);
void first_race_recover(FirstRace*r,DriveState*d);
const RaceGate*first_race_target(const FirstRace*r);
#endif
