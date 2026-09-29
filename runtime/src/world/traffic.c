#include "traffic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
_Static_assert(sizeof(TrafficRoad)==40,"traffic road format");
static float distance(DrivePoint a,DrivePoint b){return hypotf(a.x-b.x,a.y-b.y);}
static int valid(DrivePoint p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z)&&fabsf(p.x)<32768&&fabsf(p.y)<32768&&fabsf(p.z)<32768;}
void traffic_close(Traffic*t){free(t->roads);memset(t,0,sizeof(*t));}
int traffic_open(Traffic*t,const char*path){
 memset(t,0,sizeof(*t));FILE*f=fopen(path,"rb");if(!f)return -1;unsigned h[3];
 int ok=fread(h,4,3,f)==3&&!memcmp(h,"NFTR",4)&&h[1]==1&&h[2]>0&&h[2]<=131072;
 if(ok){t->count=h[2];t->roads=calloc(t->count,sizeof(*t->roads));ok=t->roads&&fread(t->roads,40,t->count,f)==t->count&&fgetc(f)==EOF;}
 if(ok)for(unsigned i=0;i<t->count;i++){
  TrafficRoad*r=&t->roads[i];if(!valid(r->a)||!valid(r->b)||distance(r->a,r->b)<.05f){ok=0;break;}
  for(int j=0;j<4;j++)if(r->next[j]!=0xffffffffu){
   if(r->next[j]>=t->count){ok=0;break;}
   DrivePoint p=t->roads[r->next[j]].a;if(distance(r->b,p)>.05f||fabsf(r->b.z-p.z)>.05f){ok=0;break;}
  }
 }
 fclose(f);if(!ok){traffic_close(t);return -1;}t->seed=27;return 0;
}
static unsigned random_next(Traffic*t){t->seed=t->seed*1664525u+1013904223u;return t->seed;}
static DrivePoint lane(const TrafficRoad*r,float u){
 float yaw=atan2f(r->b.x-r->a.x,r->b.y-r->a.y);
 return (DrivePoint){r->a.x+(r->b.x-r->a.x)*u+cosf(yaw)*1.4f,r->a.y+(r->b.y-r->a.y)*u-sinf(yaw)*1.4f,r->a.z+(r->b.z-r->a.z)*u};
}
static int spawn(Traffic*t,const DriveScene*s,const DriveState*d){
 int slot=-1;for(int i=0;i<TRAFFIC_MAX;i++)if(!t->cars[i].active){slot=i;break;}if(slot<0)return 1;
 unsigned start=t->spawn_cursor%t->count;
 if(!t->spawn_cursor)start=random_next(t)%t->count;
 for(unsigned k=0;k<t->count&&k<512;k++){
  unsigned index=(start+k)%t->count;t->spawn_cursor=index+1;TrafficRoad*r=&t->roads[index];if(r->next[0]==0xffffffffu)continue;
  float mx=(r->a.x+r->b.x)*.5f-d->player.p.x,my=(r->a.y+r->b.y)*.5f-d->player.p.y;float d2=mx*mx+my*my;if(d2<60*60||d2>160*160)continue;
  DrivePoint p=lane(r,.5f);float dx=p.x-d->player.p.x,dy=p.y-d->player.p.y,dist=hypotf(dx,dy);
  if(dist<65||dist>155||fabsf(p.z-d->player.p.z)>8)continue;
  /* Spawn ahead only beyond traffic draw range; closer spawns are beside/behind. */
  if(dx*sinf(d->player.yaw)+dy*cosf(d->player.yaw)>dist*.25f&&dist<125)continue;
  int clear=1;for(int i=0;i<TRAFFIC_MAX;i++)if(t->cars[i].active&&distance(p,t->cars[i].car.p)<20)clear=0;
  float z;if(!clear||!drive_ground(s,p.x,p.y,p.z,&z)||fabsf(z-p.z)>1.5f)continue;
  p.z=z;TrafficCar*c=&t->cars[slot];memset(c,0,sizeof(*c));c->active=1;c->model=slot%2;c->edge=index;c->car.p=p;c->car.yaw=atan2f(r->b.x-r->a.x,r->b.y-r->a.y);c->car.speed=7;return 1;
 }
 return 0;
}
void traffic_step(Traffic*t,const DriveScene*s,DriveState*d,float dt){
 if(!t->roads||!isfinite(dt)||dt<=0||dt>.05f)return;
 t->spawn_timer-=dt;if(t->spawn_timer<=0){t->spawn_timer=spawn(t,s,d)?2:.1f;}
 for(int i=0;i<TRAFFIC_MAX;i++){
  TrafficCar*c=&t->cars[i];if(!c->active)continue;
  if(distance(c->car.p,d->player.p)>180){c->active=0;continue;}
  TrafficRoad*r=&t->roads[c->edge];float dx=r->b.x-r->a.x,dy=r->b.y-r->a.y;
  float u=((c->car.p.x-r->a.x)*dx+(c->car.p.y-r->a.y)*dy)/(dx*dx+dy*dy);
  if(u>.9f&&r->next[0]!=0xffffffffu){c->edge=r->next[0];r=&t->roads[c->edge];u=0;}
  DrivePoint target=lane(r,1);float target_speed=c->model?11:9;
  unsigned edge=c->edge;float look=distance(c->car.p,target);
  for(int hop=0;hop<8&&look<8+c->car.speed*.5f;hop++){
   unsigned next=t->roads[edge].next[0];if(next==0xffffffffu)break;
   edge=next;look+=distance(t->roads[edge].a,t->roads[edge].b);target=lane(&t->roads[edge],1);
  }
  float turn=fabsf(atan2f(sinf(atan2f(target.x-c->car.p.x,target.y-c->car.p.y)-c->car.yaw),cosf(atan2f(target.x-c->car.p.x,target.y-c->car.p.y)-c->car.yaw)));
  target_speed/=1+turn*2;
  if(r->next[0]==0xffffffffu)target_speed=fminf(target_speed,sqrtf(8*fmaxf(0,distance(c->car.p,target)-3)));
  /* Follow vehicles with a speed-dependent stopping gap, on the same elevation. */
  DriveCar *others[7]={&d->player,&d->rivals[0],&d->rivals[1],&d->rivals[2],&t->cars[0].car,&t->cars[1].car,&t->cars[2].car};
  for(int j=0;j<7;j++){
   if((j>=1&&j<=3&&j>d->rival_count)||(j>=4&&(!t->cars[j-4].active||j-4==i)))continue;
   DriveCar*o=others[j];float ex=o->p.x-c->car.p.x,ey=o->p.y-c->car.p.y;
   float front=ex*sinf(c->car.yaw)+ey*cosf(c->car.yaw),side=ex*cosf(c->car.yaw)-ey*sinf(c->car.yaw);
   if(front>0&&front<6+c->car.speed*1.5f&&fabsf(side)<2.2f&&fabsf(o->p.z-c->car.p.z)<1.5f)target_speed=fminf(target_speed,fmaxf(0,(front-5)*.6f));
  }
  drive_npc(s,&c->car,&c->steering,target,target_speed,dt);
  for(int j=0;j<i;j++)if(t->cars[j].active)drive_contact(s,&c->car,&t->cars[j].car);
  drive_contact(s,&d->player,&c->car);drive_contact(s,&c->car,&d->player);
  for(int j=0;j<d->rival_count;j++){drive_contact(s,&d->rivals[j],&c->car);drive_contact(s,&c->car,&d->rivals[j]);}
 }
}
