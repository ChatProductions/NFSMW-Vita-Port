#include "free_roam.h"
#include "drive_stream.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int valid(DrivePoint p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z)&&fabsf(p.x)<32768&&fabsf(p.y)<32768&&fabsf(p.z)<32768;}
int roam_open(FreeRoam*r,const char*path){
 memset(r,0,sizeof(*r));FILE*f=fopen(path,"rb");if(!f)return -1;unsigned h[3];
 int ok=fread(h,4,3,f)==3&&!memcmp(h,"NFRM",4)&&h[1]==1&&h[2]>0&&h[2]<=65536;
 if(ok){r->count=h[2];r->roads=calloc(r->count,sizeof(*r->roads));ok=r->roads&&fread(r->roads,sizeof(*r->roads),r->count,f)==r->count&&fgetc(f)==EOF;}
 if(ok)for(unsigned i=0;i<r->count;i++)if(!valid(r->roads[i].a)||!valid(r->roads[i].b)){ok=0;break;}
 fclose(f);if(!ok){roam_close(r);return -1;}return 0;
}
void roam_close(FreeRoam*r){free(r->roads);memset(r,0,sizeof(*r));}
static uint32_t checksum(const unsigned char*p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
static int read_position(const char*path,unsigned slot,DriveCar*c,uint32_t*generation){
 char name[320];if(snprintf(name,sizeof(name),"%s.%u",path,slot)>=(int)sizeof(name))return -1;
 FILE*f=fopen(name,"rb");if(!f)return -1;unsigned char b[32];int ok=fread(b,1,sizeof(b),f)==sizeof(b)&&fgetc(f)==EOF;fclose(f);if(!ok)return -1;
 uint32_t version,sum;memcpy(&version,b+4,4);memcpy(generation,b+8,4);memset(c,0,sizeof(*c));memcpy(&c->p,b+12,12);memcpy(&c->yaw,b+24,4);memcpy(&sum,b+28,4);
 return memcmp(b,"NFRO",4)||version!=1||sum!=checksum(b,28)||!valid(c->p)||!isfinite(c->yaw)||fabsf(c->yaw)>3.142f?-1:0;
}
static int newest(const int*ok,const uint32_t*g){return ok[1]&&(!ok[0]||(int32_t)(g[1]-g[0])>0)?1:0;}
int roam_save(const char*path,const DriveCar*c){
 if(!valid(c->p)||!isfinite(c->yaw)||fabsf(c->yaw)>100000)return -1;
 DriveCar old[2];uint32_t g[2]={0};int ok[2]={read_position(path,0,&old[0],&g[0])==0,read_position(path,1,&old[1],&g[1])==0};
 int last=newest(ok,g),slot=ok[last]?1-last:0;uint32_t next=ok[last]?g[last]+1:1;
 unsigned char b[32]={0};memcpy(b,"NFRO",4);uint32_t version=1;memcpy(b+4,&version,4);memcpy(b+8,&next,4);memcpy(b+12,&c->p,12);float yaw=atan2f(sinf(c->yaw),cosf(c->yaw));memcpy(b+24,&yaw,4);uint32_t sum=checksum(b,28);memcpy(b+28,&sum,4);
 char name[320];if(snprintf(name,sizeof(name),"%s.%d",path,slot)>=(int)sizeof(name))return -1;
 FILE*f=fopen(name,"wb");if(!f)return -1;int written=fwrite(b,1,sizeof(b),f)==sizeof(b);if(fclose(f))written=0;
 DriveCar check;uint32_t generation;return written&&read_position(path,slot,&check,&generation)==0&&generation==next?0:-1;
}
int roam_restore(const char*path,DriveScene*s,DriveState*d){
 DriveCar c[2];uint32_t g[2]={0};int ok[2]={read_position(path,0,&c[0],&g[0])==0,read_position(path,1,&c[1],&g[1])==0};int first=newest(ok,g);
 for(int n=0;n<2;n++){int i=n?1-first:first;if(!ok[i])continue;
  if(s->stream&&drive_stream_update(s->stream,c[i].p)<0)continue;
  float z;if(!drive_ground(s,c[i].p.x,c[i].p.y,c[i].p.z,&z)||fabsf(z-c[i].p.z)>2)continue;
  c[i].p.z=z;d->player=c[i];return 0;
 }
 return -1;
}
void roam_start(FreeRoam*r,DriveState*d){d->rival_count=0;d->race_active=0;r->anchor=r->recovery=d->player;r->save_seconds=0;}
void roam_tick(FreeRoam*r,const DriveScene*s,DriveState*d,float dt){
 d->rival_count=0;r->save_seconds+=dt;float z;
 if(hypotf(d->player.p.x-r->anchor.p.x,d->player.p.y-r->anchor.p.y)>=12&&drive_ground(s,d->player.p.x,d->player.p.y,d->player.p.z,&z)&&fabsf(z-d->player.p.z)<1){r->recovery=r->anchor;r->anchor=d->player;}
}
void roam_recover(FreeRoam*r,DriveScene*s,DriveState*d){
 DriveCar c=r->recovery;float z;
 if(s->stream&&drive_stream_update(s->stream,c.p)<0)return;
 if(!drive_ground(s,c.p.x,c.p.y,c.p.z,&z))return;
 c.p.z=z;c.speed=c.braking=c.wheel_steer=c.lateral_speed=c.yaw_rate=c.lean=c.pitch=c.lean_velocity=c.pitch_velocity=0;d->player=c;d->rpm=900;d->gear=1;d->boosting=0;d->rival_count=0;r->anchor=c;r->cached=0;
}
void roam_nearby(FreeRoam*r,DrivePoint p){
 int bx=(int)floorf(p.x/16),by=(int)floorf(p.y/16);if(r->cached&&r->bx==bx&&r->by==by)return;
 r->near_count=0;r->bx=bx;r->by=by;r->cached=1;
 float x=bx*16+8,y=by*16+8;
 for(unsigned i=0;i<r->count&&r->near_count<1024;i++){
  RoamRoad t=r->roads[i];if(fmaxf(t.a.x,t.b.x)<x-160||fminf(t.a.x,t.b.x)>x+160||fmaxf(t.a.y,t.b.y)<y-160||fminf(t.a.y,t.b.y)>y+160)continue;
  r->nearby[r->near_count++]=i;
 }
}
