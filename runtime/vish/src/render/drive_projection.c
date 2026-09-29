#include "drive_projection.h"
#include <string.h>
void drive_perspective_matrix(float m[16]){memset(m,0,16*sizeof(float));m[0]=520.f/480;m[5]=520.f/272;m[10]=1;m[11]=1;m[14]=-1;}
DrivePoint drive_unproject_vertex(DrivePoint p){return (DrivePoint){(p.x-480)*p.z/520,(272-p.y)*p.z/520,p.z};}
