#include "drive_stream.h"
#include "wall_contact.h"
#include "camera_contact.h"
#include "breakable_props.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define SLOTS DRIVE_STREAM_SLOTS
#define BUDGET (96u*1024u*1024u)
typedef struct {int x,y,ready;unsigned count,age,indices[80000];} GroundCache;
typedef struct {int x,y,ready;unsigned age,count,materials[4096];const DriveTriangle*triangles[4096];} WallCache;
typedef struct {int x,y,ready;unsigned count;const DriveTriangle *triangles[8192];unsigned materials[8192];} CameraCache;
struct DriveStream {char root[256];FILE *pack;DriveTileInfo *index;unsigned count,cell,bytes;int cx,cy,ready,textured,started;unsigned ground_age,wall_age;GroundCache ground[8];WallCache walls[8];CameraCache camera;DriveTile tiles[SLOTS];};
static const DriveTileInfo *find(const DriveStream*s,int x,int y){
 unsigned lo=0,hi=s->count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;const DriveTileInfo*t=&s->index[mid];
  if(t->x<x||(t->x==x&&t->y<y))lo=mid+1;else hi=mid;
 }
 return lo<s->count&&s->index[lo].x==x&&s->index[lo].y==y?&s->index[lo]:NULL;
}
static void drop(DriveStream*s,DriveTile*t){
 s->bytes-=t->info.world_count*(s->textured?68:40)+t->info.road_count*40+t->cluster_count*sizeof(DriveCluster);free(t->clusters);free(t->world);free(t->road);free(t->uv);memset(t,0,sizeof(*t));
}
void drive_stream_close(DriveStream*s){if(!s)return;for(unsigned i=0;i<SLOTS;i++)drop(s,&s->tiles[i]);if(s->pack)fclose(s->pack);free(s->index);free(s);}
DriveStream *drive_stream_open(const char*root){
 DriveStream*s=calloc(1,sizeof(*s));if(!s)return NULL;
 if(strlen(root)>=sizeof(s->root)){free(s);return NULL;}strcpy(s->root,root);
 char path[320];snprintf(path,sizeof(path),"%s/world.nfi",root);FILE*f=fopen(path,"rb");if(!f){free(s);return NULL;}
 unsigned h[4];int ok=fread(h,4,4,f)==4&&!memcmp(h,"NFWI",4)&&(h[1]==2||h[1]==3)&&h[2]==128&&h[3]>0&&h[3]<=65536;
 if(ok){s->textured=h[1]==3;s->count=h[3];s->cell=h[2];s->index=calloc(s->count,sizeof(*s->index));ok=s->index&&fread(s->index,sizeof(*s->index),s->count,f)==s->count&&fgetc(f)==EOF;}
 fclose(f);
 unsigned expected=0;
 if(ok)for(unsigned i=0;i<s->count;i++){
  DriveTileInfo*t=&s->index[i];
  if(t->x< -256||t->x>255||t->y< -256||t->y>255||t->world_count>150000||t->road_count>80000||(!t->world_count&&!t->road_count)){ok=0;break;}
  if(t->offset!=expected){ok=0;break;}
  expected+=16+(s->textured?68:40)*t->world_count+40*t->road_count;if(expected>512u*1024u*1024u){ok=0;break;}
  if(i&&(t->x<s->index[i-1].x||(t->x==s->index[i-1].x&&t->y<=s->index[i-1].y))){ok=0;break;}
 }
 if(ok){snprintf(path,sizeof(path),"%s/world.nfp",root);s->pack=fopen(path,"rb");ok=s->pack&&fseek(s->pack,0,SEEK_END)==0&&ftell(s->pack)==(long)expected;}
 if(!ok){drive_stream_close(s);return NULL;}return s;
}
static int load(DriveStream*s,const DriveTileInfo*info,DriveTile*out){
 unsigned nc=(info->world_count+63)/64;unsigned bytes=info->world_count*(s->textured?68:40)+info->road_count*40+nc*sizeof(DriveCluster);if(bytes>BUDGET-s->bytes)return -1;
 FILE*f=s->pack;clearerr(f);if(fseek(f,(long)info->offset,SEEK_SET)!=0)return -1;
 unsigned h[4];int ok=fread(h,4,4,f)==4&&!memcmp(h,"NFWT",4)&&h[1]==(unsigned)(s->textured?2:1)&&h[2]==info->world_count&&h[3]==info->road_count;
 DriveTriangle*w=NULL,*r=NULL;DriveUV*uv=NULL;
 if(ok){w=malloc(info->world_count*40);r=malloc(info->road_count*40);ok=(!info->world_count||w)&&(!info->road_count||r);}
 if(ok)ok=fread(w,40,info->world_count,f)==info->world_count;
 if(ok&&s->textured&&info->world_count){uv=malloc(info->world_count*sizeof(*uv));ok=uv&&fread(uv,sizeof(*uv),info->world_count,f)==info->world_count;
  if(ok)for(unsigned i=0;i<info->world_count&&ok;i++){if(uv[i].material>4096)ok=0;for(int k=0;k<6;k++)if(!isfinite(uv[i].uv[k])||fabsf(uv[i].uv[k])>4096)ok=0;}}
 if(ok)ok=fread(r,40,info->road_count,f)==info->road_count;
 if(ok){DriveTriangle*sets[]={w,r};unsigned counts[]={info->world_count,info->road_count};
  for(int k=0;k<2&&ok;k++)for(unsigned i=0;i<counts[k]&&ok;i++)for(int j=0;j<3;j++){
   DrivePoint p=sets[k][i].p[j];if(!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z)||fabsf(p.x)>=32768||fabsf(p.y)>=32768||fabsf(p.z)>=32768){ok=0;break;}
  }
 }
 if(!ok){free(w);free(r);free(uv);return -1;}
 DriveCluster*clusters=nc?calloc(nc,sizeof(*clusters)):NULL;
 if(nc&&!clusters){free(w);free(r);free(uv);return -1;}
 for(unsigned i=0;i<nc;i++){
  DrivePoint lo=w[i*64].p[0],hi=lo;unsigned end=(i+1)*64;if(end>info->world_count)end=info->world_count;
  for(unsigned j=i*64;j<end;j++)for(int k=0;k<3;k++){DrivePoint p=w[j].p[k];lo.x=fminf(lo.x,p.x);lo.y=fminf(lo.y,p.y);lo.z=fminf(lo.z,p.z);hi.x=fmaxf(hi.x,p.x);hi.y=fmaxf(hi.y,p.y);hi.z=fmaxf(hi.z,p.z);}
  clusters[i].center=(DrivePoint){(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};float x=hi.x-lo.x,y=hi.y-lo.y,z=hi.z-lo.z;clusters[i].radius=sqrtf(x*x+y*y+z*z)*.5f+.01f;
 }
 out->clusters=clusters;out->cluster_count=nc;out->info=*info;out->world=w;out->road=r;out->uv=uv;s->bytes+=bytes;return 0;
}
int drive_stream_update(DriveStream*s,DrivePoint p){
 if(!s||!isfinite(p.x)||!isfinite(p.y)||fabsf(p.x)>=32768||fabsf(p.y)>=32768)return -1;
 int x=(int)floorf(p.x/s->cell),y=(int)floorf(p.y/s->cell);
 if(s->ready&&s->cx==x&&s->cy==y)return 0;
 /* Startup/teleports remain synchronous behind the loading screen.
    Ordinary driving brings in at most one missing sector per call. */
 int limit=(!s->started||abs(x-s->cx)>1||abs(y-s->cy)>1)?SLOTS:1;
 int changed=0;
 for(unsigned i=0;i<SLOTS;i++){
  DriveTile*t=&s->tiles[i];if((t->world||t->road)&&(abs(t->info.x-x)>2||abs(t->info.y-y)>2)){drop(s,t);changed=1;}
 }
 int ok=1,pending=0,loaded=0;
 /* Center first, then neighbors, so the road under the player has priority. */
 int dx[SLOTS],dy[SLOTS],nslots=0;
 for(int ring=0;ring<=2;ring++)for(int ox=-ring;ox<=ring;ox++)for(int oy=-ring;oy<=ring;oy++)if(abs(ox)==ring||abs(oy)==ring){dx[nslots]=ox;dy[nslots++]=oy;}
 for(int n=0;n<nslots;n++){
  const DriveTileInfo*info=find(s,x+dx[n],y+dy[n]);if(!info)continue;
  int exists=0;for(unsigned i=0;i<SLOTS;i++)if((s->tiles[i].world||s->tiles[i].road)&&s->tiles[i].info.x==info->x&&s->tiles[i].info.y==info->y)exists=1;
  if(exists)continue;
  if(loaded>=limit){pending=1;continue;}
  int slot=0;for(unsigned i=0;i<SLOTS;i++)if(!s->tiles[i].world&&!s->tiles[i].road){slot=1;if(load(s,info,&s->tiles[i])<0)ok=0;else{loaded++;changed=1;}break;}
  if(!slot)ok=0;
 }
 if(changed){for(unsigned c=0;c<8;c++){s->ground[c].ready=0;s->walls[c].ready=0;}s->camera.ready=0;}
 s->cx=x;s->cy=y;s->started=1;s->ready=ok&&!pending;return ok?0:-1;
}
int drive_stream_ground(DriveStream*s,float x,float y,float z,float*out){
 int cx=(int)floorf(x/s->cell),cy=(int)floorf(y/s->cell);
 for(unsigned i=0;i<SLOTS;i++){
  const DriveTile*t=&s->tiles[i];if(t->road&&t->info.x==cx&&t->info.y==cy){int gx=(int)floorf(x/16),gy=(int)floorf(y/16);
   GroundCache*cache=NULL,*victim=&s->ground[0];
   for(unsigned k=0;k<8;k++){GroundCache*c=&s->ground[k];if(c->ready&&c->x==gx&&c->y==gy)cache=c;if(!c->ready||c->age<victim->age)victim=c;}
   if(!cache){cache=victim;
    cache->count=0;float left=gx*16,right=left+16,bottom=gy*16,top=bottom+16;
    for(unsigned j=0;j<t->info.road_count;j++){
     const DrivePoint*p=t->road[j].p;float minx=fminf(p[0].x,fminf(p[1].x,p[2].x)),maxx=fmaxf(p[0].x,fmaxf(p[1].x,p[2].x));
     float miny=fminf(p[0].y,fminf(p[1].y,p[2].y)),maxy=fmaxf(p[0].y,fmaxf(p[1].y,p[2].y));
     if(maxx>=left&&minx<=right&&maxy>=bottom&&miny<=top)cache->indices[cache->count++]=j;
    }
    cache->x=gx;cache->y=gy;cache->ready=1;
   }
   cache->age=++s->ground_age;
   DriveScene local={.road_count=cache->count,.road=t->road,.road_indices=cache->indices};return drive_ground(&local,x,y,z,out);}
 }
 return 0;
}
const DriveTile *drive_stream_tile(const DriveStream*s,unsigned slot){return s&&slot<SLOTS?&s->tiles[slot]:NULL;}
unsigned drive_stream_count(const DriveStream*s){return s?s->count:0;}

int drive_stream_contact(DriveStream*s,DrivePoint from,DrivePoint to,float radius,int(*solid)(unsigned),DrivePoint *normal){
 if(!s||!isfinite(from.x+from.y+from.z+to.x+to.y+to.z)||radius<=0||radius>2)return 0;
 int x=(int)floorf(from.x/16),y=(int)floorf(from.y/16);WallCache*cache=NULL,*victim=&s->walls[0];
 for(unsigned i=0;i<8;i++){WallCache*c=&s->walls[i];if(c->ready&&c->x==x&&c->y==y)cache=c;if(!c->ready||c->age<victim->age)victim=c;}
 if(!cache){cache=victim;cache->count=0;float left=x*16-12,right=x*16+28,bottom=y*16-12,top=y*16+28;
  for(unsigned k=0;k<SLOTS;k++){DriveTile*t=&s->tiles[k];if(!t->world)continue;
   for(unsigned c=0;c<t->cluster_count&&cache->count<4096;c++){DriveCluster*g=&t->clusters[c];if(g->center.x+g->radius<left||g->center.x-g->radius>right||g->center.y+g->radius<bottom||g->center.y-g->radius>top)continue;
    unsigned end=(c+1)*64;if(end>t->info.world_count)end=t->info.world_count;
    for(unsigned i=c*64;i<end&&cache->count<4096;i++){DriveTriangle*tri=&t->world[i];DrivePoint a=tri->p[0],b=tri->p[1],v=tri->p[2];float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=v.x-a.x,vy=v.y-a.y,vz=v.z-a.z;float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,n=nx*nx+ny*ny+nz*nz;
     if(n<1e-8f||nz*nz>.04f*n)continue;
     if(fmaxf(a.x,fmaxf(b.x,v.x))<left||fminf(a.x,fminf(b.x,v.x))>right||fmaxf(a.y,fmaxf(b.y,v.y))<bottom||fminf(a.y,fminf(b.y,v.y))>top)continue;
     cache->triangles[cache->count]=tri;cache->materials[cache->count++]=t->uv?t->uv[i].material:0;
    }
   }
  }
  cache->x=x;cache->y=y;cache->ready=1;
 }
 cache->age=++s->wall_age;
 for(unsigned i=0;i<cache->count;i++)if((!solid||solid(cache->materials[i]))&&props_triangle(cache->triangles[i],cache->materials[i])<0&&wall_contact(from,to,radius,cache->triangles[i])){
  if(normal){const DrivePoint*p=cache->triangles[i]->p;float ux=p[1].x-p[0].x,uy=p[1].y-p[0].y,uz=p[1].z-p[0].z,vx=p[2].x-p[0].x,vy=p[2].y-p[0].y,vz=p[2].z-p[0].z;float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,n=hypotf(nx,ny);*normal=(DrivePoint){nx/n,ny/n,0};}return 1;
 }
 return 0;
}

int drive_stream_blocked(DriveStream*s,DrivePoint from,DrivePoint to,float radius,int(*solid)(unsigned)){
 return drive_stream_contact(s,from,to,radius,solid,NULL);
}
/* Reuses a local candidate set between frames; both walls and bridge undersides
   participate. Cached triangle pointers are invalidated whenever sectors change. */
float drive_stream_camera(DriveStream*s,DrivePoint from,DrivePoint to,int(*solid)(unsigned)){
 if(!s||!isfinite(from.x+from.y+from.z+to.x+to.y+to.z))return 1;
 float distance=sqrtf((to.x-from.x)*(to.x-from.x)+(to.y-from.y)*(to.y-from.y)+(to.z-from.z)*(to.z-from.z));
 if(distance<.01f||distance>12)return 1;
 CameraCache*cache=&s->camera;int x=(int)floorf(from.x/16),y=(int)floorf(from.y/16);
 if(!cache->ready||cache->x!=x||cache->y!=y){
  cache->count=0;float left=x*16-13,right=x*16+29,bottom=y*16-13,top=y*16+29;
  for(unsigned k=0;k<SLOTS;k++){DriveTile*t=&s->tiles[k];
   for(unsigned c=0;c<t->cluster_count&&cache->count<8192;c++){DriveCluster*g=&t->clusters[c];
    if(g->center.x+g->radius<left||g->center.x-g->radius>right||g->center.y+g->radius<bottom||g->center.y-g->radius>top)continue;
    unsigned end=(c+1)*64;if(end>t->info.world_count)end=t->info.world_count;
    for(unsigned i=c*64;i<end&&cache->count<8192;i++){DriveTriangle*tri=&t->world[i];const DrivePoint*p=tri->p;
     if(fmaxf(p[0].x,fmaxf(p[1].x,p[2].x))<left||fminf(p[0].x,fminf(p[1].x,p[2].x))>right||fmaxf(p[0].y,fmaxf(p[1].y,p[2].y))<bottom||fminf(p[0].y,fminf(p[1].y,p[2].y))>top)continue;
     cache->triangles[cache->count]=tri;cache->materials[cache->count++]=t->uv?t->uv[i].material:0;
    }
   }
  }
  cache->x=x;cache->y=y;cache->ready=1;
 }
 float fraction=1;
 for(unsigned i=0;i<cache->count;i++){
  unsigned m=cache->materials[i];const DriveTriangle*t=cache->triangles[i];
  if((solid&&!solid(m))||props_triangle(t,m)>=0)continue;
  const DrivePoint*p=t->p;
  if(fmaxf(p[0].x,fmaxf(p[1].x,p[2].x))<fminf(from.x,to.x)-.22f||fminf(p[0].x,fminf(p[1].x,p[2].x))>fmaxf(from.x,to.x)+.22f||
     fmaxf(p[0].y,fmaxf(p[1].y,p[2].y))<fminf(from.y,to.y)-.22f||fminf(p[0].y,fminf(p[1].y,p[2].y))>fmaxf(from.y,to.y)+.22f||
     fmaxf(p[0].z,fmaxf(p[1].z,p[2].z))<fminf(from.z,to.z)-.22f||fminf(p[0].z,fminf(p[1].z,p[2].z))>fmaxf(from.z,to.z)+.22f)continue;
  /* A small cross around the ray protects the near plane from thin edges. */
  const DrivePoint offsets[]={{0,0,0},{.22f,0,0},{-.22f,0,0},{0,.22f,0},{0,-.22f,0},{0,0,.22f},{0,0,-.22f}};
  for(unsigned j=0;j<7;j++){DrivePoint o=offsets[j],a={from.x+o.x,from.y+o.y,from.z+o.z},b={to.x+o.x,to.y+o.y,to.z+o.z};float hit;
   if(camera_segment_triangle(a,b,t,&hit))fraction=fminf(fraction,fmaxf(0,hit-.25f/distance));
  }
 }
 return fraction;
}
