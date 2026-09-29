#ifndef DRIVE_PROJECTION_H
#define DRIVE_PROJECTION_H
#include "drive_scene.h"
/* Column-major matrix for the pinned vita2d row-vector shader upload convention.
   Positive camera Z, near=0.5 and infinite far plane; W supplies perspective UVs. */
void drive_perspective_matrix(float matrix[16]);
DrivePoint drive_unproject_vertex(DrivePoint projected);
#endif
