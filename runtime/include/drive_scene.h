#ifndef NFSMW_DRIVE_SCENE_H
#define NFSMW_DRIVE_SCENE_H
#include <stdint.h>
typedef struct {float x,y,z;} DrivePoint;
typedef struct {DrivePoint p[3];uint32_t color;} DriveTriangle;
typedef struct DriveStream DriveStream;
typedef struct {DriveStream *stream;const unsigned *road_indices;const DrivePoint *race_guide;unsigned race_guide_count;unsigned world_count,car_count,road_count,path_count;DriveTriangle *world,*car,*road;DrivePoint *path;} DriveScene;
typedef struct {DrivePoint p;float yaw,speed,wheel_distance,wheel_steer,braking,yaw_rate,lateral_speed,acceleration,lean,pitch,lean_velocity,pitch_velocity;float damage[4];} DriveCar;
typedef struct {DriveCar player,rivals[3];float distance[3];float nitro,rpm,travel,seconds,steering;int gear,boosting,race_active,rival_count;} DriveState;
int drive_scene_load(const char *path,DriveScene *s);
void drive_scene_free(DriveScene *s);
int drive_ground(const DriveScene*s,float x,float y,float previous_z,float*z);
void drive_reset(const DriveScene*s,DriveState*d);
void drive_step(const DriveScene*s,DriveState*d,float throttle,float brake,float steering,float dt);
void drive_step_controls(const DriveScene*s,DriveState*d,float throttle,float brake,float steer,int handbrake,int nitro,float dt);
/* event: 0 exploration, 1 flash-forward, 2 Ronnie, 3 Bull, 4 Razor breakdown.
   Returns 2/3 for next scene, 5 for breakdown aftermath, 0 for menu. */
void drive_npc(const DriveScene*s,DriveCar*c,float *steering,DrivePoint target,float speed,float dt);
void drive_contact(const DriveScene*s,DriveCar*a,DriveCar*b);
int drive_run(int event);
#endif
