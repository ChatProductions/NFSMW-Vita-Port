#ifndef WALL_CONTACT_H
#define WALL_CONTACT_H
#include "drive_scene.h"
#include <math.h>
static inline float wall_distance2(DrivePoint p,DrivePoint a,DrivePoint b){float x=b.x-a.x,y=b.y-a.y,n=x*x+y*y,t=n>1e-8f?fmaxf(0,fminf(1,((p.x-a.x)*x+(p.y-a.y)*y)/n)):0;float dx=p.x-a.x-t*x,dy=p.y-a.y-t*y;return dx*dx+dy*dy;}
static inline int wall_contact(DrivePoint start,DrivePoint end,float radius,const DriveTriangle*t){
 DrivePoint a=t->p[0],b=t->p[1],c=t->p[2];float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
 float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,n=nx*nx+ny*ny+nz*nz;if(n<1e-8f||nz*nz>.04f*n)return 0;
 float z=(start.z+end.z)*.5f+.7f;DrivePoint cut[3];unsigned count=0;
 for(unsigned i=0;i<3;i++){DrivePoint p=t->p[i],q=t->p[(i+1)%3];if((p.z<=z&&q.z>z)||(q.z<=z&&p.z>z)){float u=(z-p.z)/(q.z-p.z);cut[count++]=(DrivePoint){p.x+u*(q.x-p.x),p.y+u*(q.y-p.y),z};}}
 if(count!=2)return 0;
 a=cut[0];b=cut[1];float before=wall_distance2(start,a,b),after=wall_distance2(end,a,b),r2=radius*radius;
 if(before<r2&&after>before+1e-6f)return 0; /* Allow escape from an existing overlap. */
 float dx=end.x-start.x,dy=end.y-start.y,ex=b.x-a.x,ey=b.y-a.y,det=dx*ey-dy*ex;
 if(fabsf(det)>1e-8f){float ax=a.x-start.x,ay=a.y-start.y,u=(ax*ey-ay*ex)/det,v=(ax*dy-ay*dx)/det;if(u>=0&&u<=1&&v>=0&&v<=1)return 1;}
 return fminf(fminf(before,after),fminf(wall_distance2(a,start,end),wall_distance2(b,start,end)))<r2;
}
#endif
