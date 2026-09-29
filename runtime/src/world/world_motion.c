#include "world_motion.h"
#include "drive_stream.h"
#include "breakable_props.h"
#include <math.h>
#include <stddef.h>
static int blocked(const DriveScene*s,const DriveCar*a,const DriveCar*b,int(*solid)(unsigned),DrivePoint*n){
 for(int side=-1;side<=1;side+=2){DrivePoint from=a->p,to=b->p;
  from.x+=sinf(a->yaw)*side*1.1f;from.y+=cosf(a->yaw)*side*1.1f;
  to.x+=sinf(b->yaw)*side*1.1f;to.y+=cosf(b->yaw)*side*1.1f;
  if(drive_stream_contact(s->stream,from,to,.85f,solid,n))return 1;
 }return 0;
}
int drive_world_contact(const DriveScene*s,DriveCar*c,const DriveCar*before,int(*solid)(unsigned)){
 props_hit(before,c);float dx=c->p.x-before->p.x,dy=c->p.y-before->p.y;
 if(hypotf(dx,dy)<.0001f)return 0;
 DrivePoint n;if(!blocked(s,before,c,solid,&n))return 0;
 float dot=dx*n.x+dy*n.y;DriveCar slide=*c;
 slide.p.x=before->p.x+dx-dot*n.x;slide.p.y=before->p.y+dy-dot*n.y;
 /* Retain the pre-impact orientation so turning does not push the nose through. */
 slide.yaw=before->yaw;float z;
 if(hypotf(slide.p.x-before->p.x,slide.p.y-before->p.y)>.001f&&
    drive_ground(s,slide.p.x,slide.p.y,before->p.z,&z)&&fabsf(z-before->p.z)<1.5f){
  slide.p.z=z;
  if(!blocked(s,before,&slide,solid,NULL)){
   float sn=sinf(slide.yaw),co=cosf(slide.yaw),vx=c->speed*sn+c->lateral_speed*co,vy=c->speed*co-c->lateral_speed*sn;
   float into=vx*n.x+vy*n.y;vx=(vx-into*n.x)*.95f;vy=(vy-into*n.y)*.95f;
   slide.speed=vx*sn+vy*co;slide.lateral_speed=vx*co-vy*sn;slide.yaw_rate*=.25f;
   float actual=hypotf(slide.p.x-before->p.x,slide.p.y-before->p.y);
   slide.wheel_distance=before->wheel_distance+copysignf(actual,c->speed);*c=slide;return 1;
  }
 }
 c->p=before->p;c->yaw=before->yaw;c->wheel_distance=before->wheel_distance;
 c->speed=c->lateral_speed=c->yaw_rate=0;return 1;
}
