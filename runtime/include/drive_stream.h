#ifndef NFSMW_DRIVE_STREAM_H
#define NFSMW_DRIVE_STREAM_H
#include "drive_scene.h"
#define DRIVE_STREAM_SLOTS 25
typedef struct {int x,y;unsigned world_count,road_count,offset;} DriveTileInfo;
typedef struct {float uv[6];unsigned material;} DriveUV;
typedef struct {DrivePoint center;float radius;} DriveCluster;
typedef struct {unsigned cluster_count;DriveCluster *clusters;DriveTileInfo info;DriveTriangle *world,*road;DriveUV *uv;} DriveTile;
DriveStream *drive_stream_open(const char *root);
void drive_stream_close(DriveStream *s);
/* Retains 5x5 sectors. Initial load/teleports load all; ordinary updates load
   at most one missing sector. Call each frame even when the center is unchanged.
   Pending neighbors are not errors; malformed/failed reads return -1. */
int drive_stream_update(DriveStream*s,DrivePoint p);
float drive_stream_camera(DriveStream*s,DrivePoint from,DrivePoint to,int(*solid)(unsigned));
int drive_stream_contact(DriveStream*s,DrivePoint from,DrivePoint to,float radius,int(*solid)(unsigned),DrivePoint*normal);
int drive_stream_blocked(DriveStream*s,DrivePoint from,DrivePoint to,float radius,int(*solid)(unsigned));
int drive_stream_ground(DriveStream*s,float x,float y,float z,float*out);
const DriveTile *drive_stream_tile(const DriveStream*s,unsigned slot);
unsigned drive_stream_count(const DriveStream*s);
#endif
