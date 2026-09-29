#include "prologue.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
int prologue_load(Prologue*p,const char*path,int event){
 memset(p,0,sizeof(*p));FILE*f=fopen(path,"rb");if(!f)return -1;unsigned h[3];float v[8];int ok=fread(h,4,3,f)==3&&!memcmp(h,"NFST",4)&&h[1]==1&&h[2]==(unsigned)event&&(event==1||event==4)&&fread(v,4,8,f)==8&&fgetc(f)==EOF;fclose(f);
 if(ok)for(int i=0;i<8;i++)if(!isfinite(v[i])||fabsf(v[i])>32768)ok=0;
 if(!ok||v[3]<1||v[3]>200||v[7]<0||v[7]>200||(event==4&&v[7]<1))return -1;
 p->event=event;p->call=(DrivePoint){v[0],v[1],v[2]};p->kill=(DrivePoint){v[4],v[5],v[6]};p->call_radius=v[3];p->kill_radius=v[7];return 0;
}
void prologue_reset(Prologue*p){p->called=p->broken=p->transition=0;p->call_seconds=p->broken_seconds=0;}
static int reached(DrivePoint a,DrivePoint b,float radius){float x=a.x-b.x,y=a.y-b.y;return x*x+y*y<=(radius+5)*(radius+5)&&fabsf(a.z-b.z)<20;}
int prologue_step(Prologue*p,const DriveCar*car,float dt){
 if(!p->event||p->transition||!isfinite(dt)||dt<=0||dt>.1f)return 0;
 int call=0;
 if(!p->called&&reached(car->p,p->call,p->call_radius)){p->called=1;call=1;}
 if(p->called)p->call_seconds+=dt;
 if(p->event==1&&p->call_seconds>=11.5f)p->transition=2;
 if(p->event==4&&p->called&&!p->broken&&reached(car->p,p->kill,p->kill_radius))p->broken=1;
 if(p->broken){p->broken_seconds+=dt;if(p->broken_seconds>=2&&fabsf(car->speed)<.5f)p->transition=5;}
 return call;
}
