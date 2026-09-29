#include "first_race.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(RaceGate)==24,"gate record");
static float length(DrivePoint a,DrivePoint b){return hypotf(a.x-b.x,a.y-b.y);}
void first_race_free(FirstRace*r){free(r->points);free(r->lengths);free(r->gates);memset(r,0,sizeof(*r));}
int first_race_load(const char*path,FirstRace*r){
 memset(r,0,sizeof(*r));FILE*f=fopen(path,"rb");if(!f)return -1;unsigned h[4];float params[4];
 if(fread(h,4,4,f)!=4||memcmp(h,"NFR1",4)||(h[1]!=2&&h[1]!=3)||h[2]<2||h[2]>4096||!h[3]||h[3]>32||fread(params,4,4,f)!=4)goto fail;
 for(int i=0;i<4;i++)if(!isfinite(params[i]))goto fail;
 if(params[0]<0||params[0]>=1||params[1]<0||params[1]>100||params[2]<0||params[2]>3600||params[3]<0||params[3]>30000)goto fail;
 float lateral=0;if(h[1]==3&&(fread(&lateral,4,1,f)!=1||!isfinite(lateral)||fabsf(lateral)>20))goto fail;r->opponent_lateral=lateral;
 r->count=h[2];r->gate_count=h[3];r->start_fraction=params[0];r->start_speed=params[1];r->start_time=params[2];r->opponent_start=params[3];
 r->points=calloc(r->count,sizeof(*r->points));r->lengths=calloc(r->count,sizeof(float));r->gates=calloc(r->gate_count,sizeof(*r->gates));
 if(!r->points||!r->lengths||!r->gates||fread(r->points,12,r->count,f)!=r->count)goto fail;
 for(unsigned i=0;i<r->gate_count;i++){if(fread(&r->gates[i],h[1]==3?24:20,1,f)!=1)goto fail;if(h[1]==2)r->gates[i].vertical_radius=15;}
 if(fgetc(f)!=EOF)goto fail;
 for(unsigned i=0;i<r->count;i++){
  DrivePoint p=r->points[i];if(!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z)||fabsf(p.x)>32768||fabsf(p.y)>32768||fabsf(p.z)>32768)goto fail;
  if(i){float d=length(p,r->points[i-1]);if(d<.001f||d>200)goto fail;r->lengths[i]=r->lengths[i-1]+d;}
 }
 r->total=r->lengths[r->count-1];if(r->total<100||r->total>30000)goto fail;
 for(unsigned i=0;i<r->gate_count;i++){RaceGate*g=&r->gates[i];if(!isfinite(g->vertical_radius)||g->vertical_radius<1||g->vertical_radius>200||!isfinite(g->p.x)||!isfinite(g->p.y)||!isfinite(g->p.z)||!isfinite(g->radius)||!isfinite(g->distance)||g->radius<1||g->radius>200||g->distance<0||g->distance>r->total+1||(i&&g->distance<=r->gates[i-1].distance))goto fail;}
 fclose(f);return 0;
fail:fclose(f);first_race_free(r);return -1;
}
static unsigned locate(const FirstRace*r,float distance){unsigned i=0;while(i+2<r->count&&r->lengths[i+1]<distance)i++;return i;}
static void at(const FirstRace*r,float distance,DriveCar*c){
 distance=fmaxf(0,fminf(r->total,distance));unsigned i=locate(r,distance);DrivePoint a=r->points[i],b=r->points[i+1];float t=(distance-r->lengths[i])/(r->lengths[i+1]-r->lengths[i]);
 c->p=(DrivePoint){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};c->yaw=atan2f(b.x-a.x,b.y-a.y);
}
/* The exported grid-to-road connector can point backwards for a few centimetres.
   Use the route ahead for grid headings, while preserving the actual spawn point. */
static void grid_heading(const FirstRace*r,float distance,DriveCar*c){DriveCar forward={0};at(r,fminf(r->total,distance+12),&forward);float x=forward.p.x-c->p.x,y=forward.p.y-c->p.y;if(x*x+y*y>1)c->yaw=atan2f(x,y);}
static void lateral_start(const FirstRace*r,DriveCar*c){float blend=fmaxf(0,1-fmaxf(0,r->rival_progress-r->opponent_start)/50);c->p.x+=cosf(c->yaw)*r->opponent_lateral*blend;c->p.y-=sinf(c->yaw)*r->opponent_lateral*blend;}
void first_race_start(FirstRace*r,const DriveScene*s,DriveState*d){
 drive_reset(s,d);r->scene=s;r->ai_steering=0;r->countdown=r->initial_countdown;d->race_active=1;d->rival_count=1;d->seconds=r->start_time;r->elapsed=0;r->finished=0;r->active=1;r->place=2;r->progress=0;r->rival_progress=fminf(r->total,r->opponent_start);r->rival_speed=r->start_speed;
 r->next=0;while(r->next<r->gate_count&&r->gates[r->next].distance<=r->progress)r->next++;
 r->segment=locate(r,r->progress);at(r,r->progress,&d->player);d->player.speed=r->start_speed;grid_heading(r,r->progress,&d->player);at(r,r->rival_progress,&d->rivals[0]);grid_heading(r,r->rival_progress,&d->rivals[0]);lateral_start(r,&d->rivals[0]);d->rivals[0].speed=r->rival_speed;
 r->ai_anchor=d->rivals[0].p;r->ai_stuck=r->ai_reverse=r->ai_cooldown=0;r->ai_recoveries=0;
}
const RaceGate*first_race_target(const FirstRace*r){return r&&r->active&&r->next<r->gate_count?&r->gates[r->next]:NULL;}
void first_race_recover(FirstRace*r,DriveState*d){
 float distance=0;if(r->next&&r->gates[r->next-1].distance>distance)distance=r->gates[r->next-1].distance;
 r->progress=distance;r->segment=locate(r,distance);at(r,distance,&d->player);grid_heading(r,distance,&d->player);d->player.speed=0;d->player.lateral_speed=d->player.yaw_rate=0;d->steering=0;
}
void first_race_step(FirstRace*r,DriveState*d,float dt){
 if(!r->active||r->finished||!isfinite(dt)||dt<=0||dt>.05f)return;
 if(r->countdown>0){r->countdown=fmaxf(0,r->countdown-dt);return;}
 r->elapsed+=dt;float best=1e30f,progress=r->progress;unsigned first=r->segment>20?r->segment-20:0,end=r->segment+40;if(end+1>=r->count)end=r->count-2;
 for(unsigned i=first;i<=end;i++){
  DrivePoint a=r->points[i],b=r->points[i+1];float dx=b.x-a.x,dy=b.y-a.y,t=((d->player.p.x-a.x)*dx+(d->player.p.y-a.y)*dy)/(dx*dx+dy*dy);t=fmaxf(0,fminf(1,t));
  float candidate=r->lengths[i]+t*(r->lengths[i+1]-r->lengths[i]);
  /* Revisited roads must not snap progress behind an already passed gate. */
  if(r->next&&candidate<r->gates[r->next-1].distance-25)continue;
  float x=a.x+t*dx-d->player.p.x,y=a.y+t*dy-d->player.p.y,z=a.z+t*(b.z-a.z)-d->player.p.z,dist=x*x+y*y+z*z;
  if(dist<best){best=dist;r->segment=i;progress=candidate;}
 }
 if(best<80*80)r->progress=progress;
 const RaceGate*g=first_race_target(r);
 if(g&&length(d->player.p,g->p)<=g->radius+4&&fabsf(d->player.p.z-g->p.z)<g->vertical_radius&&r->progress>=g->distance-40)r->next++;
 /* Project actual rival position onto the nearby route; never advance by teleport. */
 unsigned ri=locate(r,r->rival_progress);float closest=1e30f;
 for(unsigned i=ri>12?ri-12:0;i+1<r->count&&i<ri+30;i++){
  DrivePoint a=r->points[i],b=r->points[i+1],p=d->rivals[0].p;
  float dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy,t=fmaxf(0,fminf(1,((p.x-a.x)*dx+(p.y-a.y)*dy)/n));
  float ex=a.x+t*dx-p.x,ey=a.y+t*dy-p.y,ez=a.z+t*(b.z-a.z)-p.z,dist=ex*ex+ey*ey+ez*ez;
  /* Overlapping outbound/return lanes require heading to identify the branch. */
  float alignment=(dx*sinf(d->rivals[0].yaw)+dy*cosf(d->rivals[0].yaw))/sqrtf(n);
  float score=dist+12*(1-alignment);
  if(score<closest){closest=score;r->rival_progress=r->lengths[i]+t*(r->lengths[i+1]-r->lengths[i]);}
 }
 DriveCar aim={0};float look=4+fmaxf(0,d->rivals[0].speed)*.3f;
 at(r,r->rival_progress+look,&aim);
 /* Ground-checked driving line, rather than aiming at the road center throughout. */
 DriveCar ahead={0};at(r,r->rival_progress+look+15,&ahead);
 float bend=atan2f(sinf(ahead.yaw-aim.yaw),cosf(ahead.yaw-aim.yaw));
 float weight=fminf(1,fabsf(bend)/.25f),straight=r->opponent_lateral<0?-1.5f:1.5f;
 float line=straight*(1-weight)+fmaxf(-2,fminf(2,bend*8))*weight,zline;
 DrivePoint candidate={aim.p.x+cosf(aim.yaw)*line,aim.p.y-sinf(aim.yaw)*line,aim.p.z};
 if(r->scene&&drive_ground(r->scene,candidate.x,candidate.y,candidate.z,&zline)&&fabsf(zline-candidate.z)<.75f){candidate.z=zline;aim.p=candidate;}
 float target=36*(r->ai_scale>0?r->ai_scale:1);
 /* Curvature and braking-distance envelope anticipate bends at multiple distances. */
 for(int k=1;k<=6;k++){
  float distance=k*12.f;DriveCar near={0},far={0};at(r,r->rival_progress+distance-6,&near);at(r,r->rival_progress+distance+6,&far);
  float turn=fabsf(atan2f(sinf(far.yaw-near.yaw),cosf(far.yaw-near.yaw)));
  float curve_speed=sqrtf(4.5f*(r->ai_scale>0?r->ai_scale:1)/fmaxf(.004f,turn/12.f));
  target=fminf(target,sqrtf(curve_speed*curve_speed+2*8*fmaxf(0,distance-12)));
 }
 target=fminf(target,sqrtf(2*8*fmaxf(0,r->total-r->rival_progress-2)));
 if(closest>36)target=fminf(target,8);
 /* Yield or pass the player instead of moving through their position. */
 DriveCar *op=&d->rivals[0];float dx=d->player.p.x-op->p.x,dy=d->player.p.y-op->p.y;
 float forward=dx*sinf(op->yaw)+dy*cosf(op->yaw),side=dx*cosf(op->yaw)-dy*sinf(op->yaw);
 if(forward>0&&forward<25&&fabsf(side)<2.5f&&fabsf(d->player.p.z-op->p.z)<2){
  float offset=side>=0?-2.f:2.f,z;
  DrivePoint pass={aim.p.x+cosf(aim.yaw)*offset,aim.p.y-sinf(aim.yaw)*offset,aim.p.z};
  if(r->scene&&drive_ground(r->scene,pass.x,pass.y,pass.z,&z)){pass.z=z;aim.p=pass;}
  if(forward<8)target=fminf(target,fmaxf(0,d->player.speed-2));
 }
 /* Detect actual displacement, including the previous frame's wall correction.
    Back up briefly and try the other side; never teleport or grant route progress. */
 if(length(op->p,r->ai_anchor)>.8f){r->ai_anchor=op->p;r->ai_stuck=0;}
 else if(target>3&&r->ai_reverse<=0&&r->ai_cooldown<=0)r->ai_stuck+=dt;
 if(r->ai_stuck>2.5f){r->ai_stuck=0;r->ai_reverse=1.4f;r->ai_recoveries++;}
 if(r->ai_reverse>0){target=-3;r->ai_reverse=fmaxf(0,r->ai_reverse-dt);r->ai_cooldown=2.5f;}
 else if(r->ai_cooldown>0){
  r->ai_cooldown=fmaxf(0,r->ai_cooldown-dt);target=fminf(target,7);
  float offset=(r->ai_recoveries&1)?4:-4,z;DrivePoint escape={aim.p.x+cosf(aim.yaw)*offset,aim.p.y-sinf(aim.yaw)*offset,aim.p.z};
  if(r->scene&&drive_ground(r->scene,escape.x,escape.y,escape.z,&z)&&fabsf(z-escape.z)<.75f){escape.z=z;aim.p=escape;}
 }
 if(r->scene)drive_npc(r->scene,op,&r->ai_steering,aim.p,target,dt);
 r->rival_speed=op->speed;
 r->place=r->progress>=r->rival_progress?1:2;
 if(r->next==r->gate_count){r->finished=1;d->player.speed=0;d->rivals[0].speed=0;}
}
