#include "drive_scene.h"
#include "drive_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
_Static_assert(sizeof(DriveTriangle)==40,"triangle format");
void drive_scene_free(DriveScene*s){drive_stream_close(s->stream);free(s->world);free(s->car);free(s->road);free(s->path);memset(s,0,sizeof(*s));}
static int point_valid(DrivePoint p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z)&&fabsf(p.x)<2000&&fabsf(p.y)<2000&&fabsf(p.z)<2000;}
int drive_scene_load(const char*path,DriveScene*s){
 memset(s,0,sizeof(*s));FILE*f=fopen(path,"rb");if(!f)return -1;
 uint32_t h[6];
 if(fread(h,4,6,f)!=6||memcmp(h,"NFW3",4)||h[1]!=1||!h[2]||h[2]>30000||!h[3]||h[3]>3000||!h[4]||h[4]>6000||h[5]<2||h[5]>128)goto fail;
 s->world_count=h[2];s->car_count=h[3];s->road_count=h[4];s->path_count=h[5];
 s->world=calloc(s->world_count,sizeof(*s->world));s->car=calloc(s->car_count,sizeof(*s->car));s->road=calloc(s->road_count,sizeof(*s->road));s->path=calloc(s->path_count,sizeof(*s->path));
 if(!s->world||!s->car||!s->road||!s->path)goto fail;
 if(fread(s->world,40,s->world_count,f)!=s->world_count||fread(s->car,40,s->car_count,f)!=s->car_count||fread(s->road,40,s->road_count,f)!=s->road_count||fread(s->path,12,s->path_count,f)!=s->path_count||fgetc(f)!=EOF)goto fail;
 DriveTriangle *sets[]={s->world,s->car,s->road};unsigned ns[]={s->world_count,s->car_count,s->road_count};
 for(int k=0;k<3;k++)for(unsigned i=0;i<ns[k];i++)for(int j=0;j<3;j++)if(!point_valid(sets[k][i].p[j]))goto fail;
 for(unsigned i=0;i<s->path_count;i++){
  if(!point_valid(s->path[i]))goto fail;
  float z;if(!drive_ground(s,s->path[i].x,s->path[i].y,s->path[i].z,&z)||fabsf(z-s->path[i].z)>2)goto fail;
  if(i){float x=s->path[i].x-s->path[i-1].x,y=s->path[i].y-s->path[i-1].y;if(x*x+y*y<0.01f)goto fail;}
 }
 fclose(f);return 0;
fail:fclose(f);drive_scene_free(s);return -1;
}
int drive_ground(const DriveScene*s,float x,float y,float previous_z,float*z){
 if(s->stream){
  int found=drive_stream_ground(s->stream,x,y,previous_z,z);
  if(found)return 1; /* The guide is a fallback only; avoid a full route scan on valid ground. */
  /* Temporary narrow support at gaps in exported terrain, using the road graph.
     Height continuity and distance bounds avoid snapping to distant overpasses. */
  float nearest=16,guide=0;int supported=0;
  for(unsigned i=1;i<s->race_guide_count;i++){
   DrivePoint a=s->race_guide[i-1],b=s->race_guide[i];float dx=b.x-a.x,dy=b.y-a.y,n=dx*dx+dy*dy;if(n<.001f)continue;
   float t=fmaxf(0,fminf(1,((x-a.x)*dx+(y-a.y)*dy)/n));float px=a.x+t*dx-x,py=a.y+t*dy-y,h=a.z+t*(b.z-a.z),d=px*px+py*py;
   if(d<nearest&&fabsf(h-previous_z)<4){nearest=d;guide=h;supported=1;}
  }
  if(supported&&!found){*z=guide;return 1;}
  return found;
 }
 float best=4;int found=0;
 for(unsigned i=0;i<s->road_count;i++){
  unsigned index=s->road_indices?s->road_indices[i]:i;
  DrivePoint a=s->road[index].p[0],b=s->road[index].p[1],c=s->road[index].p[2];
  float det=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(fabsf(det)<0.001f)continue;
  float u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/det;
  float v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/det;
  if(u< -0.0001f||v< -0.0001f||u+v>1.0001f)continue;
  float height=u*a.z+v*b.z+(1-u-v)*c.z,delta=fabsf(height-previous_z);
  if(delta<best){best=delta;*z=height;found=1;}
 }
 return found;
}
static float length(DrivePoint a,DrivePoint b){return hypotf(b.x-a.x,b.y-a.y);}
static void route(const DriveScene*s,float dist,DriveCar*c){
 float total=0;for(unsigned i=1;i<s->path_count;i++)total+=length(s->path[i-1],s->path[i]);
 if(total<0.1f)return;
 dist=fmodf(dist,total*2);int reverse=dist>total;if(reverse)dist=2*total-dist;
 for(unsigned i=1;i<s->path_count;i++){
  DrivePoint a=s->path[i-1],b=s->path[i];float n=length(a,b);
  if(dist<=n||i+1==s->path_count){float t=fminf(1,dist/n);c->p=(DrivePoint){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};c->yaw=atan2f(b.x-a.x,b.y-a.y)+(reverse?3.14159265f:0);return;}dist-=n;
 }
}
void drive_reset(const DriveScene*s,DriveState*d){
 memset(d,0,sizeof(*d));d->nitro=1;d->rival_count=3;d->rpm=900;d->gear=1;route(s,0,&d->player);
 for(int i=0;i<3;i++){d->distance[i]=12+12*i;route(s,d->distance[i],&d->rivals[i]);}
}
static float bounded(float x,float m){return fmaxf(-m,fminf(m,x));}
/* Cosmetic contact quadrants: front-left, front-right, rear-left, rear-right. */
static void contact_damage(DriveCar*c,float nx,float ny,float severity){
 float side=nx*cosf(c->yaw)-ny*sinf(c->yaw),front=nx*sinf(c->yaw)+ny*cosf(c->yaw);
 unsigned zone=(front<0?2:0)+(side>0?1:0);
 c->damage[zone]=fminf(1,c->damage[zone]+fmaxf(0,severity-2)*.035f);
}
void drive_contact(const DriveScene*s,DriveCar*a,DriveCar*b){
 if(fabsf(a->p.z-b->p.z)>1.5f)return;
 /* Two circles per car approximate the long body, rather than a single sphere. */
 for(int i=-1;i<=1;i+=2)for(int j=-1;j<=1;j+=2){
  float dx=b->p.x+sinf(b->yaw)*j-a->p.x-sinf(a->yaw)*i;
  float dy=b->p.y+cosf(b->yaw)*j-a->p.y-cosf(a->yaw)*i;
  float dist=hypotf(dx,dy);if(dist>=1.7f)continue;
  float nx=dist>.001f?dx/dist:cosf(a->yaw),ny=dist>.001f?dy/dist:-sinf(a->yaw);
  float avx=sinf(a->yaw)*a->speed+cosf(a->yaw)*a->lateral_speed;
  float avy=cosf(a->yaw)*a->speed-sinf(a->yaw)*a->lateral_speed;
  float closing=(avx-sinf(b->yaw)*b->speed-cosf(b->yaw)*b->lateral_speed)*nx+(avy-cosf(b->yaw)*b->speed+sinf(b->yaw)*b->lateral_speed)*ny;
  float x=a->p.x-nx*(1.7f-dist+.001f),y=a->p.y-ny*(1.7f-dist+.001f),z;
  if(drive_ground(s,x,y,a->p.z,&z))a->p=(DrivePoint){x,y,z};
  if(closing<=0)continue;
  float impulse=closing*.54f;
  float bvx=sinf(b->yaw)*b->speed+cosf(b->yaw)*b->lateral_speed+nx*impulse;
  float bvy=cosf(b->yaw)*b->speed-sinf(b->yaw)*b->lateral_speed+ny*impulse;
  b->speed=bounded(bvx*sinf(b->yaw)+bvy*cosf(b->yaw),72);
  b->lateral_speed=bounded(bvx*cosf(b->yaw)-bvy*sinf(b->yaw),12);
  avx-=nx*impulse;avy-=ny*impulse;
  a->speed=bounded(avx*sinf(a->yaw)+avy*cosf(a->yaw),72);
  a->lateral_speed=bounded(avx*cosf(a->yaw)-avy*sinf(a->yaw),12);
  a->yaw_rate=bounded(a->yaw_rate+(nx*cosf(a->yaw)-ny*sinf(a->yaw))*i*closing*.06f,1.8f);
  contact_damage(a,nx,ny,closing);contact_damage(b,-nx,-ny,closing);
 }
}
void drive_step_controls(const DriveScene*s,DriveState*d,float throttle,float brake,float steer,int handbrake,int nitro,float dt){
 if(!isfinite(dt)||dt<=0||dt>0.05f||!isfinite(throttle)||!isfinite(brake)||!isfinite(steer))return;
 throttle=fmaxf(0,fminf(1,throttle));brake=fmaxf(0,fminf(1,brake));steer=fmaxf(-1,fminf(1,steer));
 DriveCar*c=&d->player;
 d->steering+=(steer-d->steering)*fminf(1,dt*7);steer=d->steering;
 d->boosting=nitro&&throttle>.1f&&c->speed>1&&d->nitro>0&&!handbrake;
 d->nitro=fmaxf(0,fminf(1,d->nitro+dt*(d->boosting?-.22f:(!nitro&&c->speed>4?.045f:0))));
 c->braking=brake>.1f||handbrake;
 float brake_force=c->speed>0.2f?-brake*14:c->speed<-.2f?(throttle>0?brake*10:-brake*4):-brake*4;
 float acc=throttle*9+12*d->boosting+brake_force-0.0025f*c->speed*fabsf(c->speed)-0.10f*c->speed;
 if(handbrake)acc-=c->speed*1.4f;
 c->acceleration=acc;
 c->speed=fmaxf(-5,fminf(72,c->speed+acc*dt));
 float oldyaw=c->yaw;c->wheel_steer=steer*(handbrake?.65f:.45f)/(1+fabsf(c->speed)*0.035f);
 float requested=c->speed/2.7f*tanf(c->wheel_steer);
 /* Grip limits tighten the turning circle progressively with speed. */
 requested=bounded(requested,(handbrake?12.f:9.5f)/fmaxf(5,fabsf(c->speed)));
 c->yaw_rate+=(requested-c->yaw_rate)*(1-expf(-dt*(handbrake?4.f:7.f)));
 c->yaw+=c->yaw_rate*dt;
 c->lateral_speed=bounded((c->lateral_speed-c->speed*c->yaw_rate*dt)*expf(-dt*(handbrake?1.5f:9.f)),12);
 float lean_target=bounded(c->speed*c->yaw_rate*.006f,.085f),pitch_target=bounded(-acc*.004f,.065f);
 c->lean_velocity+=(70*(lean_target-c->lean)-15*c->lean_velocity)*dt;
 c->pitch_velocity+=(70*(pitch_target-c->pitch)-15*c->pitch_velocity)*dt;
 c->lean+=c->lean_velocity*dt;c->pitch+=c->pitch_velocity*dt;
 float x=c->p.x+(sinf(c->yaw)*c->speed+cosf(c->yaw)*c->lateral_speed)*dt,y=c->p.y+(cosf(c->yaw)*c->speed-sinf(c->yaw)*c->lateral_speed)*dt,z;
 if(drive_ground(s,x,y,c->p.z,&z)){c->p=(DrivePoint){x,y,z};c->wheel_distance+=c->speed*dt;}else{c->speed=0;c->lateral_speed=0;c->yaw_rate=0;c->yaw=oldyaw;}
 d->seconds+=dt;d->travel+=fabsf(c->speed)*dt;
 if(c->speed<-.3f)d->gear=-1;
 else{if(d->gear<1)d->gear=1;while(d->gear<6&&c->speed>d->gear*11.f)d->gear++;while(d->gear>1&&c->speed<(d->gear-1)*11.f-2)d->gear--;}
 float rpm=c->speed<0?1000+fabsf(c->speed)*700:1100+fabsf(c->speed)/(d->gear*11.f)*6500;
 d->rpm+=(fminf(8200,rpm)-d->rpm)*fminf(1,dt*12);
 for(int i=0;i<d->rival_count;i++){
  if(!d->race_active){float yaw=d->rivals[i].yaw;d->distance[i]+=(7+i)*dt;route(s,d->distance[i],&d->rivals[i]);d->rivals[i].speed=7+i;d->rivals[i].wheel_distance+=(7+i)*dt;float turn=atan2f(sinf(d->rivals[i].yaw-yaw),cosf(d->rivals[i].yaw-yaw));d->rivals[i].wheel_steer=fmaxf(-.5f,fminf(.5f,atanf(turn*2.7f/((7+i)*dt))));}
  drive_contact(s,c,&d->rivals[i]);
 }
}

void drive_step(const DriveScene*s,DriveState*d,float throttle,float brake,float steer,float dt){drive_step_controls(s,d,throttle,brake,steer,0,0,dt);}

/* Pursuit controller uses the same integration and grip as the player. */
void drive_npc(const DriveScene*s,DriveCar*c,float *steering,DrivePoint target,float speed,float dt){
 float dx=target.x-c->p.x,dy=target.y-c->p.y;
 float side=dx*cosf(c->yaw)-dy*sinf(c->yaw),dist2=fmaxf(9,dx*dx+dy*dy);
 float wheel=atanf(2*2.7f*side/dist2);
 float command=bounded(wheel*(1+fabsf(c->speed)*.035f)/.45f,1);
 float heading=atan2f(side,dx*sinf(c->yaw)+dy*cosf(c->yaw));
 if(speed<0)command=-command; /* Backing up turns the body toward the forward target. */
 else if(fabsf(heading)>1.f){command=heading>0?1:-1;speed=fminf(speed,5);}
 float error=speed-c->speed;
 DriveState temp={0};temp.player=*c;temp.steering=*steering;temp.gear=1;
 drive_step_controls(s,&temp,fmaxf(0,fminf(1,error*.4f)),fmaxf(0,fminf(1,-error*.3f)),command,0,0,dt);
 if(speed>=0&&c->speed>=0&&temp.player.speed<0)temp.player.speed=0;
 *c=temp.player;*steering=temp.steering;
}
