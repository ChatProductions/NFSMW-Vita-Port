#include "port/GeometryFile.h"
#include "port/RawWorld.h"
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

static const char *STREAM="ux0:data/nfsmw/TRACKS/STREAML2RA.BUN";
static const char *L2RA="ux0:data/nfsmw/TRACKS/L2RA.BUN";
static const char *CAR_GEO="ux0:data/nfsmw/CARS/COBALTSS/GEOMETRY.BIN";
static const char *CAR_TEX="ux0:data/nfsmw/CARS/COBALTSS/TEXTURES.BIN";

struct CarMesh{GeometryMesh m;float minz=0;bool ok=false;};
struct DTri{float x[3],y[3],d;unsigned color;};

static bool exists(const char*p){
 FILE*f=fopen(p,"rb");if(!f)return false;fclose(f);return true;
}
static void logline(const char*s){
 FILE*f=fopen("ux0:data/nfsmw/logs/m11.log","a");if(f){fprintf(f,"%s\n",s);fclose(f);}
}
static void logf(const char*fmt,...){
 FILE*f=fopen("ux0:data/nfsmw/logs/m11.log","a");if(!f)return;
 va_list ap;va_start(ap,fmt);vfprintf(f,fmt,ap);va_end(ap);fputc('\n',f);fclose(f);
}
static void txt(vita2d_pgf*f,int x,int y,float s,unsigned c,const char*t){
 vita2d_pgf_draw_text(f,x,y,c,s,t);
}
static void fatal(vita2d_pgf*font,const char*why){
 logf("FATAL: %s",why);unsigned prev=0;
 for(;;){SceCtrlData p={};sceCtrlPeekBufferPositive(0,&p,1);unsigned press=p.buttons&~prev;prev=p.buttons;
  vita2d_start_drawing();vita2d_clear_screen();
  txt(font,36,65,1.0f,0xffffffff,"NFSMW Vita M11");
  txt(font,36,125,.78f,RGBA8(255,120,100,255),"Cannot start direct driving runtime:");
  txt(font,36,175,.70f,0xffffffff,why);
  txt(font,36,500,.55f,RGBA8(190,190,190,255),"START: exit   See ux0:data/nfsmw/logs/m11.log");
  vita2d_end_drawing();vita2d_swap_buffers();
  if(press&SCE_CTRL_START)break;
 }
}
static CarMesh load_car(){
 CarMesh out;GeometryIndex idx=ReadGeometryIndex(CAR_GEO);
 logf("COBALTSS geometry index: %s objects=%u",idx.valid?"VALID":"INVALID",idx.object_count);
 if(!idx.valid)return out;
 unsigned best=0;bool preferred=false;
 for(size_t i=0;i<idx.displayed_objects;i++){
  GeometryMesh m=LoadGeometryObject(CAR_GEO,idx,i);if(!m.valid)continue;
  bool pref=strstr(m.name,"BODY")||strstr(m.name,"BASE");
  unsigned score=m.num_tris+(pref?1000000u:0u);
  if(!out.ok||score>best){out.m=std::move(m);best=score;preferred=pref;out.ok=true;}
  if(preferred&&out.m.num_tris>1800)break;
 }
 if(!out.ok)return out;
 out.minz=1e9f;for(auto&v:out.m.vertices)out.minz=std::min(out.minz,v.z);
 logf("COBALTSS selected solid: %s tris=%u verts=%u",out.m.name,out.m.num_tris,(unsigned)out.m.vertices.size());
 return out;
}
static bool viewp(const M11Vec3&p,const M11Vec3&cam,float yaw,float pitch,float&sx,float&sy,float&z){
 float dx=p.x-cam.x,dy=p.y-cam.y,dz=p.z-cam.z,cs=cosf(yaw),sn=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 float x=dx*cs-dy*sn,d=dx*sn+dy*cs,y=d*sp+dz*cp;z=d*cp-dz*sp;
 if(z<.5f)return false;sx=480.f+x*520.f/z;sy=272.f-y*520.f/z;return isfinite(sx)&&isfinite(sy);
}
static void add_tri(std::vector<DTri>&out,const M11Triangle&t,const M11Vec3&cam,float yaw,float pitch,float maxd){
 float cx=(t.p[0].x+t.p[1].x+t.p[2].x)/3,cy=(t.p[0].y+t.p[1].y+t.p[2].y)/3;
 float dx=cx-cam.x,dy=cy-cam.y;if(dx*dx+dy*dy>maxd*maxd)return;
 DTri q{};float z[3];for(int i=0;i<3;i++)if(!viewp(t.p[i],cam,yaw,pitch,q.x[i],q.y[i],z[i]))return;
 bool l=1,r=1,u=1,d=1;for(int i=0;i<3;i++){l&=q.x[i]<-80;r&=q.x[i]>1040;u&=q.y[i]<-80;d&=q.y[i]>624;}if(l||r||u||d)return;
 q.d=(z[0]+z[1]+z[2])/3;q.color=t.color;if(out.size()<18000)out.push_back(q);
}
static void add_car(std::vector<DTri>&out,const CarMesh&car,M11Vec3 pos,float yaw,const M11Vec3&cam,float cyaw,float pitch){
 float co=cosf(yaw),si=sinf(yaw);size_t n=car.m.indices.size()/3;
 for(size_t t=0;t<n&&out.size()<18000;t++){M11Triangle tri{};bool ok=1;
  for(int j=0;j<3;j++){uint32_t k=car.m.indices[t*3+j];if(k>=car.m.vertices.size()){ok=0;break;}auto&v=car.m.vertices[k];
   float lx=-v.y,ly=v.x,lz=v.z-car.minz;
   tri.p[j]={pos.x+lx*co+ly*si,pos.y-lx*si+ly*co,pos.z+lz+.05f};
  }if(!ok)continue;tri.color=RGBA8(48,95,165,255);add_tri(out,tri,cam,cyaw,pitch,80);}
}
static void draw_scene(std::vector<DTri>&tris){
 std::sort(tris.begin(),tris.end(),[](const DTri&a,const DTri&b){return a.d>b.d;});
 size_t cap=vita2d_pool_free_space()/sizeof(vita2d_color_vertex)/3;if(cap>tris.size())cap=tris.size();if(!cap)return;
 vita2d_color_vertex*v=(vita2d_color_vertex*)vita2d_pool_memalign(cap*3*sizeof(*v),4);if(!v)return;
 for(size_t i=0;i<cap;i++)for(int j=0;j<3;j++)v[i*3+j]={tris[i].x[j],tris[i].y[j],.5f,tris[i].color};
 vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES,v,cap*3);
}
int main(){
 sceIoMkdir("ux0:data/nfsmw",0777);sceIoMkdir("ux0:data/nfsmw/logs",0777);
 FILE*lf=fopen("ux0:data/nfsmw/logs/m11.log","w");if(lf){fprintf(lf,"NFSMW Vita Port M11 direct retail runtime\n");fclose(lf);}
 vita2d_init_advanced(3*1024*1024);vita2d_set_clear_color(RGBA8(115,125,115,255));vita2d_pgf*font=vita2d_load_default_pgf();sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
 struct Req{const char*p;const char*n;} req[]={{"ux0:data/nfsmw/GLOBALA.BUN","GLOBALA.BUN"},{"ux0:data/nfsmw/GlobalB.lzc","GlobalB.lzc"},{L2RA,"TRACKS/L2RA.BUN"},{STREAM,"TRACKS/STREAML2RA.BUN"},{CAR_GEO,"COBALTSS/GEOMETRY.BIN"},{CAR_TEX,"COBALTSS/TEXTURES.BIN"}};
 for(auto&r:req){bool ok=exists(r.p);logf("%s: %s",r.n,ok?"OK":"MISSING");if(!ok){char e[180];snprintf(e,sizeof(e),"Missing %s",r.n);fatal(font,e);goto done;}}
 logline("Frontend/media/original bootstrap: DISABLED");
 {
  M11World world=LoadM11RockportSection(STREAM,101);if(!world.valid){fatal(font,world.error.c_str());goto done;}
  logf("Rockport section 101: world=%u road=%u instances=%u objects=%u",(unsigned)world.world.size(),(unsigned)world.road.size(),(unsigned)world.instance_count,(unsigned)world.object_count);
  CarMesh car=load_car();if(!car.ok){fatal(font,"Could not decode a usable COBALTSS body solid");goto done;}
  M11Vec3 player{0,0,0};float gz=0;if(M11Ground(world,0,0,0,&gz))player.z=gz;float yaw=0,speed=0;unsigned prev=0;uint64_t last=sceKernelGetProcessTimeWide();bool run=1;std::vector<DTri>draw;draw.reserve(18000);
  logline("M11 entered direct Rockport driving loop");
  while(run){
   SceCtrlData p={};sceCtrlPeekBufferPositive(0,&p,1);unsigned press=p.buttons&~prev;prev=p.buttons;if(press&SCE_CTRL_START)run=0;
   if(press&SCE_CTRL_TRIANGLE){player={0,0,0};speed=0;yaw=0;if(M11Ground(world,0,0,0,&gz))player.z=gz;}
   uint64_t now=sceKernelGetProcessTimeWide();float dt=(now-last)/1000000.f;last=now;if(dt>.05f)dt=.05f;
   float steer=((int)p.lx-128)/127.f;if(fabsf(steer)<.18f)steer=0;if(p.buttons&SCE_CTRL_LEFT)steer=-1;if(p.buttons&SCE_CTRL_RIGHT)steer=1;
   if(p.buttons&SCE_CTRL_CROSS)speed+=18*dt;else speed*=powf(.55f,dt);
   if(p.buttons&SCE_CTRL_SQUARE)speed-=25*dt;speed=std::max(-7.f,std::min(48.f,speed));
   yaw+=steer*speed*.035f*dt;float nx=player.x+sinf(yaw)*speed*dt,ny=player.y+cosf(yaw)*speed*dt,nz;
   if(M11Ground(world,nx,ny,player.z,&nz)){player={nx,ny,nz};}else speed*=.2f;
   float pitch=.22f;M11Vec3 cam{player.x-sinf(yaw)*9.f,player.y-cosf(yaw)*9.f,player.z+3.2f};
   draw.clear();for(auto&t:world.world)add_tri(draw,t,cam,yaw,pitch,180);add_car(draw,car,player,yaw,cam,yaw,pitch);
   vita2d_start_drawing();vita2d_clear_screen();draw_scene(draw);
   vita2d_draw_rectangle(0,0,960,45,RGBA8(0,0,0,145));char line[160];snprintf(line,sizeof(line),"M11 DIRECT ROCKPORT   %.0f km/h   tris %u",(double)fabsf(speed)*3.6,(unsigned)draw.size());
   txt(font,18,30,.62f,0xffffffff,line);txt(font,18,531,.48f,0xffffffff,"X throttle  SQUARE brake/reverse  LEFT STICK steer  TRIANGLE reset  START exit");
   vita2d_end_drawing();vita2d_swap_buffers();
  }logline("Clean M11 exit");
 }
done:
 vita2d_wait_rendering_done();if(font)vita2d_free_pgf(font);vita2d_fini();sceKernelExitProcess(0);return 0;
}
