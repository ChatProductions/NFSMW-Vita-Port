#include "breakable_props.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define LIMIT 2048
typedef struct {float lo[3],hi[3],angle,dx,dy;} Prop;
static Prop props[LIMIT];static unsigned count,materials[8],material_count;
void props_reset(void){for(unsigned i=0;i<count;i++)props[i].angle=props[i].dx=props[i].dy=0;}
int props_load(const char*path){count=material_count=0;FILE*f=fopen(path,"rb");if(!f)return -1;unsigned h[4];int ok=fread(h,4,4,f)==4&&!memcmp(h,"NFBP",4)&&h[1]==1&&h[2]>0&&h[2]<=LIMIT&&h[3]>0&&h[3]<=8;
 if(ok){count=h[2];material_count=h[3];ok=fread(materials,4,material_count,f)==material_count;}
 for(unsigned i=0;ok&&i<material_count;i++)ok=materials[i]>0&&materials[i]<=4096;
 for(unsigned i=0;ok&&i<count;i++){Prop*p=&props[i];memset(p,0,sizeof(*p));ok=fread(p->lo,4,3,f)==3&&fread(p->hi,4,3,f)==3;for(int k=0;ok&&k<3;k++)ok=isfinite(p->lo[k])&&isfinite(p->hi[k])&&fabsf(p->lo[k])<32768&&fabsf(p->hi[k])<32768&&p->hi[k]>=p->lo[k]&&p->hi[k]-p->lo[k]<4;}
 if(fgetc(f)!=EOF)ok=0;
 fclose(f);if(!ok){count=material_count=0;return -1;}return 0;
}
int props_triangle(const DriveTriangle*t,unsigned material){int match=0;for(unsigned i=0;i<material_count;i++)if(materials[i]==material)match=1;if(!match)return -1;
 for(unsigned i=0;i<count;i++){Prop*p=&props[i];int inside=1;for(int v=0;v<3&&inside;v++){DrivePoint a=t->p[v];if(a.x<p->lo[0]-.03f||a.x>p->hi[0]+.03f||a.y<p->lo[1]-.03f||a.y>p->hi[1]+.03f||a.z<p->lo[2]-.03f||a.z>p->hi[2]+.03f)inside=0;}if(inside)return (int)i;}return -1;
}
void props_step(float dt){if(!isfinite(dt)||dt<=0)return;for(unsigned i=0;i<count;i++)if(props[i].angle>0)props[i].angle=fminf(1.5707963f,props[i].angle+fminf(.1f,dt)*4);}
void props_hit(const DriveCar*a,const DriveCar*b){float vx=b->p.x-a->p.x,vy=b->p.y-a->p.y,m=hypotf(vx,vy);if(m<.001f||m>12)return;
 for(unsigned i=0;i<count;i++){Prop*p=&props[i];if(p->angle>0||fabsf(b->p.z-p->lo[2])>2)continue;float x=(p->lo[0]+p->hi[0])*.5f,y=(p->lo[1]+p->hi[1])*.5f;
  for(int side=-1;side<=1;side+=2){float ax=a->p.x+side*1.1f*sinf(a->yaw),ay=a->p.y+side*1.1f*cosf(a->yaw),bx=b->p.x+side*1.1f*sinf(b->yaw),by=b->p.y+side*1.1f*cosf(b->yaw),dx=bx-ax,dy=by-ay,n=dx*dx+dy*dy,t=n>1e-8f?fmaxf(0,fminf(1,((x-ax)*dx+(y-ay)*dy)/n)):0;if(hypotf(x-ax-t*dx,y-ay-t*dy)<1.15f){p->angle=.001f;p->dx=vx/m;p->dy=vy/m;break;}}
 }
}
void props_transform(const DriveTriangle*t,unsigned material,DriveTriangle*out){*out=*t;int id=props_triangle(t,material);if(id<0)return;Prop*p=&props[id];if(!p->angle)return;float cx=(p->lo[0]+p->hi[0])*.5f,cy=(p->lo[1]+p->hi[1])*.5f,c=cosf(p->angle),s=sinf(p->angle);
 for(int i=0;i<3;i++){DrivePoint*a=&out->p[i];float x=a->x-cx,y=a->y-cy,h=a->z-p->lo[2],along=x*p->dx+y*p->dy,shift=along*(c-1)+h*s+.6f*s;a->x+=p->dx*shift;a->y+=p->dy*shift;a->z=p->lo[2]+fmaxf(.025f,h*c-along*s+.2f*s);}
}
