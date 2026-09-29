#include "drive_scene.h"
#include "renderer.h"
#include "gfx_guard.h"
#include "drive_projection.h"
#include "drive_audio.h"
#include "splash_audio.h"
#include "prologue.h"
#include "free_roam.h"
#include "world_map.h"
#include "settings.h"
#include "quick_race.h"
#include "breakable_props.h"
#include "traffic.h"
/* Adapter to pinned libvita2d a8f15ab; restored before any 2D HUD draw. */
extern float _vita2d_ortho_matrix[16];
#include "drive_stream.h"
#include "world_motion.h"
#include "drive_hud.h"
#include "world_textures.h"
#include "first_race.h"
#include "career_store.h"
#include "platform.h"
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_DRAW 24000
static float view_distance=120.f;
#define VIEW_DISTANCE view_distance
/* CPU visibility/clipping, GPU perspective/depth. Opaque materials batch first;
   blended surfaces retain back-to-front ordering without depth writes. */
typedef struct {DrivePoint p[3];float uv[6];unsigned color;} CarTriangle;
typedef struct {DrivePoint p[3];float uv[6];float depth;unsigned color;int textured;} Face;
typedef struct {CarTriangle *mesh;unsigned count;unsigned char *parts;DrivePoint pivots[4];float radius,brake_uv[2];vita2d_texture *texture;} CarAsset;
static CarAsset cars[4];
static void free_asset(CarAsset*a){free(a->mesh);free(a->parts);if(a->texture)vita2d_free_texture(a->texture);*a=(CarAsset){0};}
static void free_car(void){for(int i=0;i<4;i++)free_asset(&cars[i]);}
static int load_car(CarAsset*a,const char*root){
 char path[256];snprintf(path,sizeof(path),"%s/car.nfc",root);FILE*f=fopen(path,"rb");unsigned h[3];if(!f)return -1;
 if(fread(h,4,3,f)!=3||h[0]!=0x5443464e||(h[1]!=1&&h[1]!=2)||!h[2]||h[2]>6000){fclose(f);return -1;}
 a->count=h[2];a->mesh=calloc(a->count,sizeof(*a->mesh));
 if(!a->mesh||fread(a->mesh,sizeof(*a->mesh),a->count,f)!=a->count||fgetc(f)!=EOF){fclose(f);free_asset(a);return -1;}fclose(f);
 for(unsigned i=0;i<a->count;i++){
  if(h[1]==1)a->mesh[i].color=0xfffffffe;
  else if(a->mesh[i].color!=0xffffffff&&a->mesh[i].color!=0xfffffffe){free_asset(a);return -1;}
  for(int j=0;j<3;j++){DrivePoint p=a->mesh[i].p[j];if(!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z)||fabsf(p.x)>10||fabsf(p.y)>10||fabsf(p.z)>10){free_asset(a);return -1;}}
  for(int j=0;j<6;j++)if(!isfinite(a->mesh[i].uv[j])||a->mesh[i].uv[j]<0||a->mesh[i].uv[j]>1){free_asset(a);return -1;}
 }
 unsigned char png[24];snprintf(path,sizeof(path),"%s/car.png",root);f=fopen(path,"rb");if(!f){free_asset(a);return -1;}
 int ok=fread(png,1,24,f)==24;fclose(f);
 if(!ok||png[0]!=137||png[1]!=80||png[2]!=78||png[3]!=71||png[16]!=0||png[17]!=0||(png[18]!=8&&png[18]!=2)||png[19]!=0||png[20]!=0||png[21]!=0||png[22]!=png[18]||png[23]!=0){free_asset(a);return -1;}
 snprintf(path,sizeof(path),"%s/car.nfa",root);f=fopen(path,"rb");
 if(f){
  unsigned ah[3];float values[15];int valid=fread(ah,4,3,f)==3&&ah[0]==0x5741464e&&ah[1]==1&&ah[2]==a->count&&fread(values,4,15,f)==15;
  if(valid){for(int i=0;i<15;i++)if(!isfinite(values[i])||fabsf(values[i])>10)valid=0;}
  if(valid&&(values[12]<.1f||values[12]>1||fabsf(values[13])>1||fabsf(values[14])>1))valid=0;
  if(valid){a->parts=malloc(a->count);valid=a->parts&&fread(a->parts,1,a->count,f)==a->count&&fgetc(f)==EOF;}
  if(valid)for(unsigned i=0;i<a->count;i++){
   if(a->parts[i]>5)valid=0;
   if(a->parts[i]==5)for(int j=0;j<6;j++){float shifted=a->mesh[i].uv[j]+values[13+j%2];if(shifted<0||shifted>1)valid=0;}
  }
  fclose(f);if(!valid){free_asset(a);return -1;}memcpy(a->pivots,values,48);a->radius=values[12];a->brake_uv[0]=values[13];a->brake_uv[1]=values[14];
 }
 snprintf(path,sizeof(path),"%s/car.png",root);
 a->texture=vita2d_load_PNG_file(path);if(!a->texture){free_asset(a);return -1;}return 0;
}
static Face *faces;static unsigned face_count;
static DrivePoint camera;static float cs,sn,cp,sp;
/* At most 25 tiles of 150000 faces, grouped by 64. Fixed, bounded scratch space. */
typedef struct {const DriveTile *tile;unsigned cluster;float distance;int inside_range;} VisibleCluster;
static VisibleCluster visible_clusters[DRIVE_STREAM_SLOTS*((150000+63)/64)];
static int cluster_order(const void*a,const void*b){
 const VisibleCluster*x=a,*y=b;return (x->distance>y->distance)-(x->distance<y->distance);
}
static DrivePoint view(DrivePoint p){
 float x=p.x-camera.x,y=p.y-camera.y,z=p.z-camera.z,d=x*sn+y*cs;
 return (DrivePoint){x*cs-y*sn,d*sp+z*cp,d*cp-z*sp};
}
static int cluster_visible(const DriveCluster*c){
#ifdef NFSMW_DISABLE_CLUSTER_CULL
 (void)c;return 1;
#else
 DrivePoint p=view(c->center);float r=c->radius;
 if(p.z-r>VIEW_DISTANCE)return 0;
 if(p.z+r<.5f)return 0;
 if(fabsf(p.x)*520>p.z*480+r*707.674f)return 0;
 if(fabsf(p.y)*520>p.z*272+r*586.944f)return 0;
 return 1;
#endif
}
static void submit_uv(DrivePoint a,DrivePoint b,DrivePoint c,unsigned color,const float *uv,int texture_id){
 DrivePoint in[4]={view(a),view(b),view(c)},out[5];float outuv[5][2];int n=0;
 for(int i=0;i<3;i++){
  DrivePoint p=in[i],q=in[(i+1)%3];int ip=p.z>=0.5f,iq=q.z>=0.5f;
  if(ip){out[n]=p;for(int k=0;k<2;k++)outuv[n][k]=uv?uv[i*2+k]:0;n++;}
  if(ip!=iq){float t=(0.5f-p.z)/(q.z-p.z);out[n]=(DrivePoint){p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),0.5f};for(int k=0;k<2;k++)outuv[n][k]=uv?uv[i*2+k]+t*(uv[((i+1)%3)*2+k]-uv[i*2+k]):0;n++;}
 }
 for(int i=1;i+1<n&&face_count<MAX_DRAW;i++){
  DrivePoint v[3]={out[0],out[i],out[i+1]};float depth=(v[0].z+v[1].z+v[2].z)/3;
  if(v[0].z>240&&v[1].z>240&&v[2].z>240)continue;
  int left=1,right=1,above=1,below=1;
  for(int j=0;j<3;j++){
   /* Keep camera-space vertices for the GPU; test side planes without divides. */
   left&=520*v[j].x < -480*v[j].z;right&=520*v[j].x > 480*v[j].z;above&=520*v[j].y > 272*v[j].z;below&=520*v[j].y < -272*v[j].z;
  }
  if(left||right||above||below)continue;
  Face*f=&faces[face_count++];for(int j=0;j<3;j++)f->p[j]=v[j];f->depth=depth;f->color=color;f->textured=uv?texture_id:0;int ids[3]={0,i,i+1};for(int j=0;j<3;j++)for(int k=0;k<2;k++)f->uv[j*2+k]=outuv[ids[j]][k];
 }
}
static void submit(DrivePoint a,DrivePoint b,DrivePoint c,unsigned color){submit_uv(a,b,c,color,NULL,0);}
static int face_opaque(const Face*f){if(!f->textured)return (f->color>>24)==255;if(f->textured==1||f->textured>=4098)return f->color==0xffffffff;return world_textures_opaque(f->textured-1);}
static int render_order(const void*a,const void*b){const Face*x=a,*y=b;int ox=face_opaque(x),oy=face_opaque(y);if(ox!=oy)return oy-ox;if(ox&&x->textured!=y->textured)return x->textured-y->textured;if(!ox){int bx=(int)(x->depth/6),by=(int)(y->depth/6);if(bx!=by)return by-bx;if(x->textured!=y->textured)return x->textured-y->textured;}return ox?(x->depth>y->depth)-(x->depth<y->depth):(x->depth<y->depth)-(x->depth>y->depth);}
static void depth_state(int test,int write){SceGxmContext*c=vita2d_get_context();sceGxmSetFrontDepthFunc(c,test?SCE_GXM_DEPTH_FUNC_LESS_EQUAL:SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(c,test?SCE_GXM_DEPTH_FUNC_LESS_EQUAL:SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetFrontDepthWriteEnable(c,write?SCE_GXM_DEPTH_WRITE_ENABLED:SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(c,write?SCE_GXM_DEPTH_WRITE_ENABLED:SCE_GXM_DEPTH_WRITE_DISABLED);}
static float body_pitch[7],body_roll[7];
static void car(const DriveCar*c,int actor,const DriveScene*scene,int visual_id,float dt){
 CarAsset*a=&cars[actor];
 float co=cosf(c->yaw),si=sinf(c->yaw),roll=a->radius>0?-c->wheel_distance/a->radius:0;
 float rc=cosf(roll),rs=sinf(roll),sc=cosf(c->wheel_steer),ss=sinf(c->wheel_steer);
 float axle_front=a->pivots[0].y,axle_back=a->pivots[2].y,wheelbase=fmaxf(1,axle_front-axle_back);
 float front,back,left,right,blend=fminf(1,dt*8);
 if(drive_ground(scene,c->p.x+si*axle_front,c->p.y+co*axle_front,c->p.z,&front)&&drive_ground(scene,c->p.x+si*axle_back,c->p.y+co*axle_back,c->p.z,&back))body_pitch[visual_id]+=(atan2f(front-back,wheelbase)-body_pitch[visual_id])*blend;
 if(drive_ground(scene,c->p.x+co*.67f,c->p.y-si*.67f,c->p.z,&left)&&drive_ground(scene,c->p.x-co*.67f,c->p.y+si*.67f,c->p.z,&right))body_roll[visual_id]+=(fmaxf(-.35f,fminf(.35f,atan2f(left-right,1.34f)))-body_roll[visual_id])*blend;
 float cp=cosf(c->pitch),sp=sinf(c->pitch),cl=cosf(c->lean),sl=sinf(c->lean);
 float pc=cosf(body_pitch[visual_id]),ps=sinf(body_pitch[visual_id]),bc=cosf(body_roll[visual_id]),bs=sinf(body_roll[visual_id]);
 for(unsigned i=0;i<a->count;i++){
  unsigned part=a->parts?a->parts[i]:0;DrivePoint p[3];float uv[6];memcpy(uv,a->mesh[i].uv,sizeof(uv));
  for(int j=0;j<3;j++){
   DrivePoint q=a->mesh[i].p[j];
   if(part>=1&&part<=4){DrivePoint pivot=a->pivots[part-1];float x=q.x-pivot.x,y=q.y-pivot.y,z=q.z-pivot.z,rotated=y*rc-z*rs;q.z=pivot.z+y*rs+z*rc;
    q.x=pivot.x+(part<=2?x*sc+rotated*ss:x);q.y=pivot.y+(part<=2?-x*ss+rotated*sc:rotated);}
   if(part==5&&c->braking){uv[j*2]+=a->brake_uv[0];uv[j*2+1]+=a->brake_uv[1];}
   /* Localized cosmetic deformation; wheels retain their original shape. */
   if(part==0||part==5){
    unsigned zone=(q.y<0?2:0)+(q.x>0?1:0);
    float dent=c->damage[zone]*fminf(1,fabsf(q.y)/1.8f);
    q.y*=1-.07f*dent;q.x*=1-.04f*dent;
    float by=q.y*cp-(q.z-.35f)*sp,bz=q.y*sp+(q.z-.35f)*cp;
    q.y=by;q.x=q.x*cl-bz*sl;q.z=bz*cl+a->mesh[i].p[j].x*sl+.35f;
   }
   float tilted_y=q.y*pc-q.z*ps,tilted_z=q.y*ps+q.z*pc;float tilted_x=q.x*bc-tilted_z*bs;tilted_z=q.x*bs+tilted_z*bc;
   p[j]=(DrivePoint){c->p.x+tilted_x*co+tilted_y*si,c->p.y-tilted_x*si+tilted_y*co,c->p.z+tilted_z};
  }
  submit_uv(p[0],p[1],p[2],a->mesh[i].color,uv,actor?4097+actor:1);
 }
}
/* Bounded rear view: cached geometry, diffuse vertex colors, no extra texture loads. */
typedef struct {DrivePoint p[3];float depth;unsigned color;} RearFace;
static RearFace rear_faces[4096];static unsigned rear_count;
static DrivePoint rear_camera;static float rear_cs,rear_sn;
static DrivePoint rear_view(DrivePoint p){float x=p.x-rear_camera.x,y=p.y-rear_camera.y;return (DrivePoint){x*rear_cs-y*rear_sn,p.z-rear_camera.z,x*rear_sn+y*rear_cs};}
static void rear_submit(DrivePoint a,DrivePoint b,DrivePoint c,unsigned color){
 if(rear_count>=4096)return;DrivePoint p[3]={rear_view(a),rear_view(b),rear_view(c)};
 if(p[0].z<.5f||p[1].z<.5f||p[2].z<.5f)return;
 float depth=(p[0].z+p[1].z+p[2].z)/3;if(depth>55)return;
 int left=1,right=1,up=1,down=1;for(int i=0;i<3;i++){left&=p[i].x< -p[i].z;right&=p[i].x>p[i].z;up&=p[i].y>p[i].z*.3f;down&=p[i].y< -p[i].z*.3f;}
 if(left||right||up||down)return;RearFace*f=&rear_faces[rear_count++];memcpy(f->p,p,sizeof(p));f->depth=depth;f->color=color|0xff000000u;
}
static int rear_order(const void*a,const void*b){float x=((const RearFace*)a)->depth,y=((const RearFace*)b)->depth;return (x<y)-(x>y);}
static void rear_car(const DriveCar*c,int actor){
 if(hypotf(c->p.x-rear_camera.x,c->p.y-rear_camera.y)>50)return;
 float co=cosf(c->yaw),si=sinf(c->yaw);CarAsset*a=&cars[actor];
 for(unsigned i=0;i<a->count&&rear_count<4096;i++){
  DrivePoint v[3];for(int j=0;j<3;j++){DrivePoint p=a->mesh[i].p[j];v[j]=(DrivePoint){c->p.x+p.x*co+p.y*si,c->p.y-p.x*si+p.y*co,c->p.z+p.z};}
  rear_submit(v[0],v[1],v[2],a->mesh[i].color==0xffffffff?RGBA8(145,150,155,255):RGBA8(45,50,55,255));
 }
}
static void rear_build(const DriveScene*s,const DriveState*d,const Traffic*t){
 rear_count=0;rear_cs=-cosf(d->player.yaw);rear_sn=-sinf(d->player.yaw);rear_camera=d->player.p;rear_camera.z+=1.4f;
 for(int i=0;i<d->rival_count;i++)rear_car(&d->rivals[i],d->race_active?1:0);
 for(int i=0;i<TRAFFIC_MAX;i++)if(t->cars[i].active)rear_car(&t->cars[i].car,2+t->cars[i].model);
 unsigned clusters=0;
 for(unsigned k=0;k<DRIVE_STREAM_SLOTS;k++){
  const DriveTile*tile=drive_stream_tile(s->stream,k);
  for(unsigned c=0;c<tile->cluster_count;c++){
   const DriveCluster*g=&tile->clusters[c];DrivePoint p=rear_view(g->center);float r=g->radius;
   if(p.z+r<.5f||p.z-r>55||fabsf(p.x)>p.z+r*1.42f||fabsf(p.y)>.3f*p.z+r*1.05f)continue;
   visible_clusters[clusters++]=(VisibleCluster){tile,c,fmaxf(0,sqrtf(p.x*p.x+p.y*p.y+p.z*p.z)-r),0};
  }
 }
 qsort(visible_clusters,clusters,sizeof(*visible_clusters),cluster_order);
 for(unsigned k=0;k<clusters&&rear_count<4096;k++){
  const DriveTile*tile=visible_clusters[k].tile;unsigned c=visible_clusters[k].cluster,end=(c+1)*64;if(end>tile->info.world_count)end=tile->info.world_count;
  for(unsigned i=c*64;i<end&&rear_count<4096;i++){if(tile->uv&&world_textures_event_barrier(tile->uv[i].material))continue;const DriveTriangle*f=&tile->world[i];DriveTriangle moved;props_transform(f,tile->uv?tile->uv[i].material:0,&moved);rear_submit(moved.p[0],moved.p[1],moved.p[2],moved.color);}
 }
 qsort(rear_faces,rear_count,sizeof(*rear_faces),rear_order);
}
static void rear_draw(void){
 vita2d_draw_rectangle(316,6,328,104,RGBA8(15,15,15,255));vita2d_draw_rectangle(320,10,320,96,RGBA8(115,122,110,255));
 /* Clip projected polygons to the mirror, so no triangle covers the main view. */
 unsigned capacity=vita2d_pool_free_space()>65536?(vita2d_pool_free_space()-65536)/sizeof(vita2d_color_vertex):0;if(capacity>18000)capacity=18000;
 if(capacity<3)return;vita2d_color_vertex*v=vita2d_pool_memalign(capacity*sizeof(*v),4);if(!v)return;unsigned used=0;
 for(unsigned i=0;i<rear_count&&used+18<=capacity;i++){
  DrivePoint a[12],b[12];unsigned n=3;for(unsigned j=0;j<3;j++){DrivePoint p=rear_faces[i].p[j];a[j]=(DrivePoint){480-160*p.x/p.z,58-160*p.y/p.z,0};}
  for(int edge=0;edge<4&&n;edge++){
   unsigned m=0;float bound=edge==0?320:edge==1?640:edge==2?10:106;
   for(unsigned j=0;j<n;j++){DrivePoint p=a[j],q=a[(j+1)%n];float x=edge<2?p.x:p.y,y=edge<2?q.x:q.y;int ip=(edge==0||edge==2)?x>=bound:x<=bound,iq=(edge==0||edge==2)?y>=bound:y<=bound;
    if(ip)b[m++]=p;if(ip!=iq){float t=(bound-x)/(y-x);b[m++]=(DrivePoint){p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),0};}}
   n=m;memcpy(a,b,n*sizeof(*a));
  }
  for(unsigned j=1;j+1<n;j++){unsigned ids[3]={0,j,j+1};for(unsigned k=0;k<3;k++){DrivePoint p=a[ids[k]];v[used++]=(vita2d_color_vertex){p.x,p.y,.5f,rear_faces[i].color};}}
 }
 if(used)vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,v,used);
}

static int quick_active;static QuickRace quick_config;
int drive_run(int event){
 if(props_load("ux0:data/nfsmw/runtime/world/breakable.nfb")<0)platform_log("Cone instance data unavailable; rigid fallback remains");
 gfx_guard_reset();platform_log_memory("before-driving");
 char event_log[80];snprintf(event_log,sizeof(event_log),"Drive 00.43 preparing event %d",event);platform_log(event_log);
 float chase_length=11;
 int next_event=0;int first_event=event!=0;if(event<0||event>4)return -1;
 memset(body_pitch,0,sizeof(body_pitch));memset(body_roll,0,sizeof(body_roll));
 renderer_loading("Preparando Rockport",0,7);
 DriveScene s;if(drive_scene_load("ux0:data/nfsmw/runtime/world/rockport.nfw",&s)<0){platform_log("World missing or invalid");return -1;}
 s.stream=drive_stream_open("ux0:data/nfsmw/runtime/world/tiles");
 if(!s.stream||drive_stream_update(s.stream,s.path[0])<0){platform_log("Full world index or tiles missing");drive_scene_free(&s);return -1;}
 renderer_loading("Preparando tu coche",1,7);
 if(load_car(&cars[0],"ux0:data/nfsmw/runtime/world/car")<0){platform_log("Car mesh or atlas missing/invalid");drive_scene_free(&s);return -1;}
 renderer_loading("Preparando escenario",2,7);
 if(world_textures_open("ux0:data/nfsmw/runtime/world/textures")<0){platform_log("World texture pack missing/invalid");free_car();drive_scene_free(&s);return -1;}
 renderer_loading("Preparando carrera",3,7);
 FirstRace race={0};
 if(first_event&&(first_race_load(event==4?"ux0:data/nfsmw/runtime/race/razor/first.nfr":event==3?"ux0:data/nfsmw/runtime/race/bull/first.nfr":event==2?"ux0:data/nfsmw/runtime/race/ronnie/first.nfr":"ux0:data/nfsmw/runtime/race/first.nfr",&race)<0||load_car(&cars[1],event==3?"ux0:data/nfsmw/runtime/race/bull/opponent":event==2?"ux0:data/nfsmw/runtime/race/ronnie/opponent":"ux0:data/nfsmw/runtime/race/opponent")<0)){first_race_free(&race);world_textures_close();free_car();drive_scene_free(&s);return -1;}
 faces=calloc(MAX_DRAW,sizeof(*faces));vita2d_pgf*font=vita2d_load_default_pgf();
 if(!faces||!font){first_race_free(&race);world_textures_close();free_car();free(faces);if(font)vita2d_free_pgf(font);drive_scene_free(&s);return -1;}
 if(first_event){race.event_id=event==4?"16.2.1":event==3?"16.2.3":event==2?"16.2.2":"16.1.0";race.opponent_name=event==3?"BULL":event==2?"RONNIE":"RAZOR";race.initial_countdown=event>=2?3:0;s.race_guide=race.points;s.race_guide_count=race.count;}
 if(event==2){race.opponent_start=0;race.opponent_lateral=3.2f;}
 if(quick_active){race.start_fraction=0;race.start_time=0;race.start_speed=0;race.opponent_start=0;race.opponent_lateral=3.2f;race.initial_countdown=3;race.ai_scale=quick_config.difficulty==0?.8f:quick_config.difficulty==1?1.f:1.1f;}
 DriveState d;drive_reset(&s,&d);if(first_event){first_race_start(&race,&s,&d);drive_stream_update(s.stream,d.player.p);}uint64_t last=sceKernelGetProcessTimeWide();float accumulator=0;unsigned previous=platform_buttons();int paused=0,pause_choice=0,result_saved=-1;
 Prologue story={0};if(!quick_active&&(event==1||event==4)){if(prologue_load(&story,event==1?"ux0:data/nfsmw/runtime/race/story/first.nfs":"ux0:data/nfsmw/runtime/race/story/razor.nfs",event)<0){first_race_free(&race);world_textures_close();free_car();free(faces);faces=NULL;vita2d_free_pgf(font);drive_scene_free(&s);return -1;}}
 FreeRoam roam={0};
 if(roam_open(&roam,"ux0:data/nfsmw/runtime/world/roam.nfm")<0)platform_log("Free roam radar missing; driven trail remains available");
 if(!first_event){
  if(roam_restore("ux0:data/nfsmw/runtime/save/free-roam.dat",&s,&d)==0)platform_log("Free roam position restored");
  roam_start(&roam,&d);
 }
 SceTouchPanelInfo touch_panel={0};int touch_ready=sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&touch_panel)>=0&&touch_panel.maxAaX>touch_panel.minAaX&&touch_panel.maxAaY>touch_panel.minAaY;
 SceTouchSamplingState previous_touch=SCE_TOUCH_SAMPLING_STATE_STOP;int touch_held=0;
 if(touch_ready){sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT,&previous_touch);touch_ready=sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START)>=0;}
 renderer_loading("Preparando trafico",4,7);
 Traffic traffic={0};
 if((quick_active&&!quick_config.traffic)||traffic_open(&traffic,"ux0:data/nfsmw/runtime/traffic/roads.nft")<0||load_car(&cars[2],"ux0:data/nfsmw/runtime/traffic/TRAF4DSEDA")<0||load_car(&cars[3],"ux0:data/nfsmw/runtime/traffic/TRAFTAXI")<0){traffic_close(&traffic);free_asset(&cars[2]);free_asset(&cars[3]);platform_log("Traffic resources unavailable; continuing without civilians");}
 renderer_loading("Preparando sonido y radio",5,7);
 splash_audio_stop();drive_audio_start("ux0:data/nfsmw/runtime/race/story");DrivePoint oil_origin=d.player.p;
 unsigned profile_frames=0,profile_clusters=0,profile_skipped=0,profile_capped=0;double profile_sim=0,profile_project=0,profile_textures=0,profile_draw=0;
 GameSettings options;settings_load(SETTINGS_ROOT,&options);settings_apply_audio(&options);view_distance=90+30*options.value[OPT_VIEW];
 int stream_error=0,camera_mode=options.value[OPT_CAMERA],show_help=options.value[OPT_HELP],show_fps=options.value[OPT_FPS],confirm=0;float look_yaw=0,look_pitch=.3f,fps_average=0;drive_hud_load();
 uint64_t last_slow_log=0;float rear_age=1;int show_mirror=options.value[OPT_MIRROR],loading_first_frame=1;
 renderer_loading("Preparando texturas cercanas",6,7);
 platform_log("Rockport tiled geometry entered; physical controls");
 for(;;){
  SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);unsigned pressed=pad.buttons&~previous;previous=pad.buttons;
  int was_paused=paused;
  if(first_event&&race.finished){
   if(pressed&SCE_CTRL_CIRCLE)break;
   if(!quick_active&&event==1&&result_saved==0&&(pressed&SCE_CTRL_TRIANGLE)){next_event=2;break;}
   if(!quick_active&&event==2&&race.place==1&&(pressed&SCE_CTRL_TRIANGLE)){next_event=3;break;}
   if(pressed&SCE_CTRL_CROSS){first_race_start(&race,&s,&d);props_reset();prologue_reset(&story);drive_audio_reset_call();paused=0;confirm=0;accumulator=0;result_saved=-1;}
   else pressed=0;
  }
  if(pressed&SCE_CTRL_START){paused=!paused;pause_choice=0;confirm=0;}
  if(paused){
   if(confirm){if(pressed&SCE_CTRL_CIRCLE)confirm=0;else if(pressed&SCE_CTRL_CROSS)break;}
   else if(pressed&SCE_CTRL_CIRCLE){paused=0;pause_choice=0;}
   else if(pressed&SCE_CTRL_DOWN)pause_choice=(pause_choice+1)%6;
   else if(pressed&SCE_CTRL_UP)pause_choice=(pause_choice+5)%6;
   else if(pressed&SCE_CTRL_CROSS){
    if(pause_choice==0)paused=0;
    else if(pause_choice==1){if(first_event)first_race_recover(&race,&d);else roam_recover(&roam,&s,&d);accumulator=0;paused=0;}
    else if(pause_choice==2)camera_mode=(camera_mode+1)%3;
    else if(pause_choice==3)show_help=!show_help;
    else if(pause_choice==4){options.value[OPT_CAMERA]=camera_mode;options.value[OPT_HELP]=show_help;options.value[OPT_FPS]=show_fps;options.value[OPT_MIRROR]=show_mirror;if(settings_save(SETTINGS_ROOT,&options)<0)platform_log("Could not persist pause preferences");drive_audio_update(d.rpm,0,1,!story.broken);settings_ui_run();settings_load(SETTINGS_ROOT,&options);camera_mode=options.value[OPT_CAMERA];show_help=options.value[OPT_HELP];show_fps=options.value[OPT_FPS];show_mirror=options.value[OPT_MIRROR];view_distance=90+30*options.value[OPT_VIEW];rear_age=1;last=sceKernelGetProcessTimeWide();accumulator=0;previous=platform_buttons();continue;}
    else confirm=1;
   }
  }
  if(!paused&&!was_paused&&(pressed&SCE_CTRL_TRIANGLE)){if(first_event)first_race_recover(&race,&d);else roam_recover(&roam,&s,&d);accumulator=0;}
  int open_map=!paused&&!was_paused&&!race.finished&&(pressed&SCE_CTRL_SELECT);
  if(touch_ready){
   SceTouchData touch={0};if(sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1)>0){
    if(touch.reportNum&&!touch_held&&!paused){
     float x=(touch.report[0].x-touch_panel.minAaX)*960.f/(touch_panel.maxAaX-touch_panel.minAaX),y=(touch.report[0].y-touch_panel.minAaY)*544.f/(touch_panel.maxAaY-touch_panel.minAaY);
     if(!race.finished&&(x-100)*(x-100)+(y-436)*(y-436)<=76*76)open_map=1;
     else if(y>=6&&y<=110&&x>=316&&x<=644){show_mirror=!show_mirror;rear_age=1;}
     else if(y>=12&&y<=70&&x>=704&&x<=950){if(x<766)drive_radio_change(-1);else if(x>890)drive_radio_change(1);else drive_radio_toggle();}
    }
    touch_held=touch.reportNum>0;
   }
  }
  if(open_map){drive_audio_update(d.rpm,0,1,!story.broken);world_map_run(font,&roam,&d,first_event?&race:NULL);last=sceKernelGetProcessTimeWide();accumulator=0;sceCtrlPeekBufferPositive(0,&pad,1);previous=pad.buttons;touch_held=1;if(gfx_guard_error()){next_event=-1;break;}continue;}
  uint64_t now=sceKernelGetProcessTimeWide();float elapsed=(now-last)/1000000.0f;float dt=elapsed;last=now;
  if(dt>0.1f)dt=0.1f;
  float stick=((int)pad.lx-128)/127.0f;
  float deadzone=options.value[OPT_DEADZONE]/100.f;
  float amount=fminf(1,fmaxf(0,(fabsf(stick)-deadzone)/(1-deadzone)));
  float steer=fmaxf(-1,fminf(1,copysignf(amount*(.55f+.45f*amount),stick)*options.value[OPT_STEER]/100.f));
  if(pad.buttons&SCE_CTRL_LEFT)steer=-1;
  if(pad.buttons&SCE_CTRL_RIGHT)steer=1;
  float throttle=!!(pad.buttons&SCE_CTRL_CROSS),brake=!!(pad.buttons&SCE_CTRL_SQUARE);
  DriveCar before_player=d.player,before_rivals[3],before_traffic[TRAFFIC_MAX];memcpy(before_rivals,d.rivals,sizeof(before_rivals));for(int i=0;i<TRAFFIC_MAX;i++)before_traffic[i]=traffic.cars[i].car;
  float before_progress=race.progress;unsigned before_next=race.next,before_segment=race.segment;int before_finished=race.finished;
  float before_travel=d.travel;
  uint64_t phase_start=sceKernelGetProcessTimeWide();
  stream_error=drive_stream_update(s.stream,d.player.p)<0;
  if(!paused&&!was_paused&&!race.finished){accumulator+=dt;while(accumulator>=1.0f/120){if(first_event&&race.countdown>0){first_race_step(&race,&d,1.0f/120);accumulator-=1.0f/120;continue;}drive_step_controls(&s,&d,story.broken?0:throttle,brake,steer,!!(pad.buttons&SCE_CTRL_CIRCLE),!story.broken&&!!(pad.buttons&SCE_CTRL_RTRIGGER),1.0f/120);traffic_step(&traffic,&s,&d,1.f/120);if(story.broken){d.player.speed*=.995f;if(fabsf(d.player.speed)<.5f)d.player.speed=0;d.rpm=0;d.boosting=0;}if(first_event){first_race_step(&race,&d,1.0f/120);if(race.finished){accumulator=0;break;}}accumulator-=1.0f/120;}}else accumulator=0;
  if(!paused&&!was_paused&&!race.finished)props_step(dt);
  int player_hit=drive_world_contact(&s,&d.player,&before_player,world_textures_collidable);for(int i=0;i<d.rival_count;i++)drive_world_contact(&s,&d.rivals[i],&before_rivals[i],world_textures_collidable);for(int i=0;i<TRAFFIC_MAX;i++)if(traffic.cars[i].active&&hypotf(traffic.cars[i].car.p.x-before_traffic[i].p.x,traffic.cars[i].car.p.y-before_traffic[i].p.y)<10)drive_world_contact(&s,&traffic.cars[i].car,&before_traffic[i],world_textures_collidable);
  if(player_hit){
   d.travel=before_travel+hypotf(d.player.p.x-before_player.p.x,d.player.p.y-before_player.p.y);
   /* A rejected movement cannot pass a gate or save a finish. Reproject next tick. */
   if(first_event&&!before_finished){race.progress=before_progress;race.next=before_next;race.segment=before_segment;race.finished=0;race.place=race.progress>=race.rival_progress?1:2;}
  }
  if(first_event&&race.finished&&!before_finished){result_saved=quick_active?2:event>=3?1:event==2?career_record_ronnie("ux0:data/nfsmw/runtime/save",(unsigned)(race.elapsed*1000)):career_record_first("ux0:data/nfsmw/runtime/save",(unsigned)(race.elapsed*1000));platform_log(quick_active?"Quick race finished; career untouched":result_saved==0?"First-event trial result saved; career stage unchanged":"First-event result could not be saved");}
  if(!paused&&!was_paused&&!race.finished&&race.countdown<=0){
   if(prologue_step(&story,&d.player,dt)){drive_audio_call();platform_log("Original Mia oil call triggered at source marker");}
   if(story.transition){next_event=story.transition;if(event==1){result_saved=career_record_first("ux0:data/nfsmw/runtime/save",(unsigned)fmaxf(1,race.elapsed*1000));platform_log(result_saved==0?"Prologue transition saved":"Prologue transition could not be saved");}break;}
  }
  stream_error|=drive_stream_update(s.stream,d.player.p)<0; /* Cover the post-physics camera too. */
  if(!first_event&&!paused&&!was_paused){
   roam_tick(&roam,&s,&d,dt);
   if(roam.save_seconds>=30){roam.save_seconds=0;int failed=roam_save("ux0:data/nfsmw/runtime/save/free-roam.dat",&d.player)<0;if(failed&&!roam.save_failed)platform_log("Free roam position could not be saved");roam.save_failed=failed;}
  }
  drive_audio_update(d.rpm,throttle,paused||race.finished,!story.broken);
  uint64_t phase_sim=sceKernelGetProcessTimeWide();
  float rx=((int)pad.rx-128)/127.f,ry=((int)pad.ry-128)/127.f;
  if(options.value[OPT_INVERT])ry=-ry;
  if(fabsf(rx)<.18f)rx=0;
  if(fabsf(ry)<.18f)ry=0;
  float blend=fminf(1,dt*9);float target_yaw=(pad.buttons&SCE_CTRL_LTRIGGER)?3.14159265f:rx*2.8f;look_yaw+=(target_yaw-look_yaw)*blend;look_pitch+=(.3f+ry*.25f-look_pitch)*blend;
  cs=cosf(d.player.yaw+look_yaw);sn=sinf(d.player.yaw+look_yaw);cp=cosf(look_pitch);sp=sinf(look_pitch);
  float camera_distance=camera_mode==0?7.5f:camera_mode==1?11.f:0;
  camera=(DrivePoint){d.player.p.x-sn*camera_distance*cp,d.player.p.y-cs*camera_distance*cp,d.player.p.z+1+camera_distance*sp};
  if(camera_distance>0){DrivePoint anchor=d.player.p;anchor.z+=1;
   float fraction=drive_stream_camera(s.stream,anchor,camera,world_textures_collidable);
   float allowed=camera_distance*fraction;chase_length=fminf(allowed,chase_length+dt*5);fraction=chase_length/camera_distance;
   camera.x=anchor.x+(camera.x-anchor.x)*fraction;camera.y=anchor.y+(camera.y-anchor.y)*fraction;camera.z=anchor.z+(camera.z-anchor.z)*fraction;
   camera_distance*=fraction;
  }
  face_count=0;
  if(camera_mode!=2&&camera_distance>2)car(&d.player,0,&s,0,paused?0:dt);
  for(int i=0;i<d.rival_count;i++)car(&d.rivals[i],first_event?1:0,&s,i+1,paused?0:dt);
  for(int i=0;i<TRAFFIC_MAX;i++)if(traffic.cars[i].active&&hypotf(traffic.cars[i].car.p.x-d.player.p.x,traffic.cars[i].car.p.y-d.player.p.y)<110)car(&traffic.cars[i].car,2+traffic.cars[i].model,&s,4+i,paused?0:dt);
  if(event==4){
   DrivePoint p=oil_origin;p.z+=.035f;submit((DrivePoint){p.x-1,p.y-1,p.z},(DrivePoint){p.x+1,p.y-1,p.z},(DrivePoint){p.x+1,p.y+2,p.z},RGBA8(12,10,6,155));submit((DrivePoint){p.x-1,p.y-1,p.z},(DrivePoint){p.x+1,p.y+2,p.z},(DrivePoint){p.x-1,p.y+2,p.z},RGBA8(12,10,6,155));
  }
  if(story.broken)for(int i=0;i<8;i++){
   float t=fmodf(story.broken_seconds+i*.19f,1.6f),size=.18f+t*.23f;DrivePoint p=d.player.p;p.z+=1.1f+t;p.x+=sinf(d.player.yaw)*1.2f+cosf(i*2.f)*t*.25f;p.y+=cosf(d.player.yaw)*1.2f;
   DrivePoint a={p.x-cs*size,p.y+sn*size,p.z-size},b={p.x+cs*size,p.y-sn*size,p.z-size},c={p.x+cs*size,p.y-sn*size,p.z+size},e={p.x-cs*size,p.y+sn*size,p.z+size};unsigned color=RGBA8(150,155,145,(unsigned)(50*(1-t/1.6f)));submit(a,b,c,color);submit(a,c,e,color);
  }
  unsigned visible_count=0;
  for(unsigned k=0;k<DRIVE_STREAM_SLOTS;k++){
   const DriveTile*tile=drive_stream_tile(s.stream,k);
   for(unsigned cluster=0;cluster<tile->cluster_count;cluster++){
    profile_clusters++;const DriveCluster*c=&tile->clusters[cluster];
    if(!cluster_visible(c)){profile_skipped++;continue;}
    float x=c->center.x-camera.x,y=c->center.y-camera.y,z=c->center.z-camera.z;
    float center_distance=sqrtf(x*x+y*y+z*z);
    visible_clusters[visible_count++]=(VisibleCluster){tile,cluster,fmaxf(0,center_distance-c->radius),center_distance+c->radius<=VIEW_DISTANCE};
   }
  }
  qsort(visible_clusters,visible_count,sizeof(*visible_clusters),cluster_order);
  for(unsigned k=0;k<visible_count&&face_count<MAX_DRAW;k++){
   const DriveTile*tile=visible_clusters[k].tile;unsigned cluster=visible_clusters[k].cluster;
   unsigned end=(cluster+1)*64;if(end>tile->info.world_count)end=tile->info.world_count;
   for(unsigned i=cluster*64;i<end&&face_count<MAX_DRAW;i++){
   const DriveTriangle*t=&tile->world[i];
   if(!visible_clusters[k].inside_range){
   float minx=fminf(t->p[0].x,fminf(t->p[1].x,t->p[2].x)),maxx=fmaxf(t->p[0].x,fmaxf(t->p[1].x,t->p[2].x));
   float miny=fminf(t->p[0].y,fminf(t->p[1].y,t->p[2].y)),maxy=fmaxf(t->p[0].y,fmaxf(t->p[1].y,t->p[2].y));
   float dx=fmaxf(minx-camera.x,fmaxf(0,camera.x-maxx)),dy=fmaxf(miny-camera.y,fmaxf(0,camera.y-maxy));
   if(dx*dx+dy*dy>VIEW_DISTANCE*VIEW_DISTANCE)continue;
   }
   const DriveUV*uv=tile->uv?&tile->uv[i]:NULL;
   if(uv&&world_textures_event_barrier(uv->material))continue;
   DriveTriangle moved;props_transform(t,uv?uv->material:0,&moved);t=&moved;
   if(uv&&uv->material)submit_uv(t->p[0],t->p[1],t->p[2],t->color,uv->uv,uv->material+1);else submit(t->p[0],t->p[1],t->p[2],t->color);
  }
  }
  if(face_count==MAX_DRAW)profile_capped++;
  uint64_t phase_project=sceKernelGetProcessTimeWide();
  world_textures_begin();for(unsigned i=0;i<face_count;i++)if(faces[i].textured>1&&faces[i].textured<4098)world_textures_request(faces[i].textured-1,faces[i].depth);
  if(loading_first_frame){
   /* Same 256-texture cache and 8-load batches as normal rendering; no full-map preload. */
   for(unsigned batch=0;batch<32;batch++){world_textures_prepare();if(batch%8==7)renderer_loading("Preparando texturas cercanas",24+batch/8,28);}
   renderer_loading_end();platform_log_memory("first-view-loaded");last=sceKernelGetProcessTimeWide();accumulator=0;loading_first_frame=0;
  }
  world_textures_prepare_budget(2);for(unsigned i=0;i<face_count;i++)if(faces[i].textured>1&&faces[i].textured<4098&&!world_textures_get(faces[i].textured-1))faces[i].textured=0;
  uint64_t phase_textures=sceKernelGetProcessTimeWide();
  qsort(faces,face_count,sizeof(*faces),render_order);
  vita2d_start_drawing();if(gfx_guard_error()){vita2d_end_drawing();next_event=-1;break;}depth_state(0,1);vita2d_clear_screen();depth_state(0,0);vita2d_draw_rectangle(0,0,960,544,RGBA8(130,137,120,255));
  float saved_matrix[16];memcpy(saved_matrix,_vita2d_ortho_matrix,sizeof(saved_matrix));drive_perspective_matrix(_vita2d_ortho_matrix);
  for(unsigned first=0;first<face_count;){
   unsigned end=first+1;int textured=faces[first].textured,opaque=face_opaque(&faces[first]);
   depth_state(1,opaque);
   while(end<face_count&&end-first<21845&&faces[end].textured==textured&&face_opaque(&faces[end])==opaque)end++;
   unsigned count=end-first,bytes=count*3*(textured?sizeof(vita2d_texture_vertex):sizeof(vita2d_color_vertex));
   if(vita2d_pool_free_space()<bytes+65536)break;
   void*buffer=vita2d_pool_memalign(bytes,4);if(!buffer)break;
   if(textured){vita2d_texture_vertex*v=buffer;for(unsigned i=0;i<count;i++)for(int j=0;j<3;j++){Face*f=&faces[first+i];DrivePoint vertex=f->p[j];v[i*3+j]=(vita2d_texture_vertex){vertex.x,vertex.y,vertex.z,f->uv[j*2],f->uv[j*2+1]};}vita2d_draw_array_textured(textured==1?cars[0].texture:textured>=4098?cars[textured-4097].texture:world_textures_get(textured-1),SCE_GXM_PRIMITIVE_TRIANGLES,v,count*3,0xffffffff);}
   else{vita2d_color_vertex*v=buffer;for(unsigned i=0;i<count;i++)for(int j=0;j<3;j++){Face*f=&faces[first+i];DrivePoint vertex=f->p[j];v[i*3+j]=(vita2d_color_vertex){vertex.x,vertex.y,vertex.z,f->color};}vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,v,count*3);}
   first=end;
  }
  memcpy(_vita2d_ortho_matrix,saved_matrix,sizeof(saved_matrix));depth_state(0,1);
  if(elapsed>0)fps_average=fps_average==0?1/elapsed:fps_average+(1/elapsed-fps_average)*fminf(1,elapsed*2);
  if(show_mirror){rear_age+=elapsed;if(rear_age>=.25f){rear_build(&s,&d,&traffic);rear_age=0;}rear_draw();}
  else vita2d_pgf_draw_text(font,370,30,0xffffffff,.6f,"Toca para activar retrovisor");
  if(quick_active)d.race_active=2;
  drive_hud_draw(font,&d,show_fps?fps_average:-1,show_help,stream_error);
  vita2d_draw_rectangle(704,12,246,58,RGBA8(0,0,0,165));
  vita2d_pgf_draw_text(font,718,35,0xffffffff,.7f,drive_radio_enabled()?"<      RADIO ON      >":"<      RADIO OFF     >");
  char song[48];snprintf(song,sizeof(song),"%.38s",drive_radio_label());vita2d_pgf_draw_text(font,713,58,0xffffffff,.48f,song);
  if(first_event)drive_hud_race(font,&race,&d);
  if(!quick_active&&race.finished&&event<3)vita2d_pgf_draw_text(font,245,313,0xffffffff,.7f,result_saved==0?"Resultado local guardado":"No se pudo guardar el resultado local");
  if(!quick_active&&event==1&&race.finished&&result_saved==0)vita2d_pgf_draw_text(font,245,380,0xffffffff,.7f,"Triangulo: seguir con Ronnie");
  if(!quick_active&&event==2&&race.finished&&race.place==1)vita2d_pgf_draw_text(font,245,380,0xffffffff,.7f,"Triangulo: continuar con Bull");
  if(!quick_active&&event==3&&race.finished)vita2d_pgf_draw_text(font,245,380,0xffffffff,.7f,"El circuito de Rog aun no esta disponible");
  if(story.called&&story.call_seconds<11.5f){vita2d_draw_rectangle(330,175,300,42,RGBA8(0,0,0,190));vita2d_pgf_draw_text(font,352,204,0xffffffff,.85f,"MIA - llamada entrante");}
  if(story.broken)vita2d_pgf_draw_text(font,355,245,RGBA8(255,170,40,255),1.f,"AVERIA DEL MOTOR");
  if(!first_event)drive_hud_roam(font,&roam,&d);
  if(paused&&!race.finished)drive_hud_pause(font,pause_choice,camera_mode,show_help,show_fps,confirm);
  vita2d_end_drawing();vita2d_swap_buffers();vita2d_wait_rendering_done();
  if(gfx_guard_error()){platform_log("Drive aborted cleanly after GXM failure; see preceding code");next_event=-1;break;}
  uint64_t phase_done=sceKernelGetProcessTimeWide();profile_sim+=phase_sim-phase_start;profile_project+=phase_project-phase_sim;profile_textures+=phase_textures-phase_project;profile_draw+=phase_done-phase_textures;
  if(phase_done-phase_start>250000&&phase_done-last_slow_log>2000000){char slow[200];snprintf(slow,sizeof(slow),"Slow work ms: simulation+stream %.1f geometry %.1f textures %.1f draw+mirror+wait %.1f",(phase_sim-phase_start)/1000.f,(phase_project-phase_sim)/1000.f,(phase_textures-phase_project)/1000.f,(phase_done-phase_textures)/1000.f);platform_log(slow);last_slow_log=phase_done;}
  if(++profile_frames==30){char report[240];snprintf(report,sizeof(report),"Drive CPU/GPU wait ms avg: sim+stream %.2f project %.2f textures %.2f sort+draw+wait %.2f; faces %u; clusters skipped %u/%u; capped frames %u",profile_sim/30000,profile_project/30000,profile_textures/30000,profile_draw/30000,face_count,profile_skipped,profile_clusters,profile_capped);platform_log(report);profile_frames=profile_clusters=profile_skipped=profile_capped=0;profile_sim=profile_project=profile_textures=profile_draw=0;}
  /* The vita2d display queue already synchronizes swaps to vblank.
     Keep GXM completion for shared buffers, without a second CPU vblank wait. */
 }
 options.value[OPT_CAMERA]=camera_mode;options.value[OPT_HELP]=show_help;options.value[OPT_FPS]=show_fps;options.value[OPT_MIRROR]=show_mirror;
 if(settings_save(SETTINGS_ROOT,&options)<0)platform_log("Could not save driving options");
 if(!first_event&&roam_save("ux0:data/nfsmw/runtime/save/free-roam.dat",&d.player)<0)platform_log("Free roam exit position could not be saved");
 roam_close(&roam);
 if(touch_ready)sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,previous_touch);
 traffic_close(&traffic);drive_audio_stop();menu_audio_start();vita2d_wait_rendering_done();drive_hud_free();first_race_free(&race);world_textures_close();free_car();free(faces);faces=NULL;vita2d_free_pgf(font);drive_scene_free(&s);platform_log("Drive scene closed; prototype event state preserved");return next_event;
}

int drive_run_quick(const QuickRace*q){if(!quick_race_valid(q))return -1;quick_config=*q;quick_active=1;int result=drive_run(q->event);quick_active=0;return result;}
