#ifndef CAMERA_CONTACT_H
#define CAMERA_CONTACT_H
#include "drive_scene.h"
#include <math.h>
/* Two-sided segment test: winding must not make bridge undersides invisible. */
static inline int camera_segment_triangle(DrivePoint a,DrivePoint b,const DriveTriangle*t,float*out){
 DrivePoint v=t->p[0],u={t->p[1].x-v.x,t->p[1].y-v.y,t->p[1].z-v.z},w={t->p[2].x-v.x,t->p[2].y-v.y,t->p[2].z-v.z},d={b.x-a.x,b.y-a.y,b.z-a.z};
 DrivePoint h={d.y*w.z-d.z*w.y,d.z*w.x-d.x*w.z,d.x*w.y-d.y*w.x};
 float det=u.x*h.x+u.y*h.y+u.z*h.z;if(!isfinite(det)||fabsf(det)<1e-7f)return 0;
 DrivePoint s={a.x-v.x,a.y-v.y,a.z-v.z};float inv=1/det,x=(s.x*h.x+s.y*h.y+s.z*h.z)*inv;if(x<0||x>1)return 0;
 DrivePoint q={s.y*u.z-s.z*u.y,s.z*u.x-s.x*u.z,s.x*u.y-s.y*u.x};float y=(d.x*q.x+d.y*q.y+d.z*q.z)*inv;if(y<0||x+y>1)return 0;
 float distance=(w.x*q.x+w.y*q.y+w.z*q.z)*inv;if(!isfinite(distance)||distance<0||distance>1)return 0;*out=distance;return 1;
}
#endif
