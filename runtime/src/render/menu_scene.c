#include "menu_scene.h"
#include "platform.h"
#include "renderer.h"
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define ROOT "ux0:data/nfsmw/runtime/menu/"
#define MAX_PAGES 16
#define MAX_VIEWS 128
#define MAX_TEXTURES 64
#define MAX_QUADS 2048
/* Position and UV corners: top-left, top-right, bottom-left, bottom-right. */
typedef struct { uint32_t texture,color; float xy[8],uv[8]; uint32_t key,role; } Quad;
_Static_assert(sizeof(Quad)==80,"menu quad layout");
typedef struct { uint32_t count,first; int32_t parent; } Page;
typedef struct { int32_t target; uint32_t count; Quad *quads; } View;
static Page pages[MAX_PAGES];
static View views[MAX_VIEWS];
static struct {uint32_t id;vita2d_texture *texture;} textures[MAX_TEXTURES];
static uint32_t page_count,view_count,texture_count,page,selected[MAX_PAGES];
static int dialog;
#define MAX_ANIM_QUADS (MAX_QUADS*2)
static Quad current[MAX_ANIM_QUADS],origin[MAX_ANIM_QUADS];
static unsigned current_count,origin_count;
static uint64_t transition_start,last_present;
static float dialog_alpha;
static uint32_t overlay_signature;static uint64_t overlay_start;
static double clock_seconds(void){return sceKernelGetProcessTimeWide()/1000000.0;}
static void begin_transition(void){
 origin_count=current_count;memcpy(origin,current,current_count*sizeof(Quad));
 transition_start=sceKernelGetProcessTimeWide();
}

static uint32_t big32(const unsigned char *p) {
 return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
void menu_unload(void) {
 vita2d_wait_rendering_done();
 for(uint32_t i=0;i<texture_count;i++)vita2d_free_texture(textures[i].texture);
 for(uint32_t i=0;i<MAX_VIEWS;i++){free(views[i].quads);views[i].quads=NULL;}
 current_count=origin_count=0;transition_start=last_present=0;dialog_alpha=0;overlay_signature=0;overlay_start=0;
 texture_count=page_count=view_count=page=0;dialog=0;memset(selected,0,sizeof(selected));
}
static int texture_index(uint32_t id,size_t *memory) {
 for(uint32_t i=0;i<texture_count;i++)if(textures[i].id==id)return (int)i;
 if(texture_count>=MAX_TEXTURES)return -1;
 char path[128];snprintf(path,sizeof(path),ROOT "%08x.png",(unsigned)id);
 FILE*f=fopen(path,"rb");if(!f)return -1;
 unsigned char h[24];int ok=fread(h,1,24,f)==24;fclose(f);
 if(!ok||memcmp(h,"\x89PNG\r\n\x1a\n",8)||memcmp(h+12,"IHDR",4))return -1;
 uint32_t w=big32(h+16),height=big32(h+20);
 if(!w||!height||w>2048||height>2048)return -1;
 *memory+=(size_t)w*height*4;if(*memory>32*1024*1024)return -1;
 vita2d_texture*t=vita2d_load_PNG_file(path);if(!t)return -1;
 textures[texture_count].id=id;textures[texture_count].texture=t;
 return (int)texture_count++;
}
int menu_load(void) {
 menu_unload();FILE*f=fopen(ROOT "menu.nfm","rb");
 if(!f){platform_log("Menu data missing");return -1;}
 char magic[4];uint32_t version;
 int ok=fread(magic,1,4,f)==4&&!memcmp(magic,"NFMM",4)&&fread(&version,4,1,f)==1&&(version==1||version==2)&&
  fread(&page_count,4,1,f)==1&&fread(&view_count,4,1,f)==1&&
  page_count>0&&page_count<=MAX_PAGES&&view_count>1&&view_count<=MAX_VIEWS;
 if(!ok)goto fail;
 if(fread(pages,sizeof(Page),page_count,f)!=page_count)goto fail;
 uint32_t expected_first=0;
 for(uint32_t i=0;i<page_count;i++){
  Page*p=&pages[i];
  if(!p->count||p->count>32||p->first!=expected_first||p->first>=view_count-1||
    p->count>view_count-1-p->first||p->parent< -1||p->parent>=(int32_t)i||
    (i==0?p->parent!=-1:p->parent<0))goto fail;
  expected_first+=p->count;
 }
 if(expected_first!=view_count-1)goto fail;
 size_t memory=0,total=0;
 for(uint32_t i=0;i<view_count;i++){
  View*v=&views[i];
  if(fread(&v->target,4,1,f)!=1||fread(&v->count,4,1,f)!=1||v->target< -4||
    v->target>=(int32_t)page_count||!v->count||v->count>MAX_QUADS)goto fail;
  total+=v->count;if(total>32768)goto fail;
  v->quads=calloc(v->count,sizeof(Quad));
  if(!v->quads)goto fail;
  for(unsigned j=0;j<v->count;j++){
   if(fread(&v->quads[j],version==2?80:72,1,f)!=1)goto fail;
   if(version==1)v->quads[j].key=j;
   if(v->quads[j].role>2)goto fail;
   for(unsigned k=0;k<j;k++)if(v->quads[k].key==v->quads[j].key)goto fail;
  }
  for(uint32_t j=0;j<v->count;j++){
   Quad*q=&v->quads[j];
   for(int k=0;k<8;k++)if(!isfinite(q->xy[k])||fabsf(q->xy[k])>4096||
      !isfinite(q->uv[k])||q->uv[k]<0||q->uv[k]>1)goto fail;
   int index=texture_index(q->texture,&memory);if(index<0)goto fail;q->texture=(uint32_t)index;
  }
 }
 if(fgetc(f)!=EOF)goto fail;
 fclose(f);begin_transition();platform_log("Original menu artwork loaded; animated 2D navigation");return 0;
fail:
 fclose(f);menu_unload();platform_log("Menu data invalid or texture unavailable");return -1;
}
int menu_input(uint32_t pressed) {
 if(!page_count)return 1;
 if(page==4&&(pressed&SCE_CTRL_SQUARE))return 6;
 if(page==4&&(pressed&SCE_CTRL_TRIANGLE))return 7;
 if(dialog){if(pressed&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS))dialog=0;return 0;}
 if(pressed&SCE_CTRL_CIRCLE){
  if(pages[page].parent<0)return 1;
  begin_transition();page=(uint32_t)pages[page].parent;return 0;
 }
 if(pressed&SCE_CTRL_LEFT){if(selected[page]){begin_transition();selected[page]--;}}
 else if(pressed&SCE_CTRL_RIGHT){if(selected[page]+1<pages[page].count){begin_transition();selected[page]++;}}
 else if(pressed&SCE_CTRL_CROSS){
  int target=views[pages[page].first+selected[page]].target;
  if(page==1)return 10+(int)selected[page];
  if(page==2&&selected[page]==0)return 20;
  if(page==3&&selected[page]==1)return 21;
  if(target<=-2)return -target;
  if(target<0){dialog=1;platform_log("Menu option selected: subsystem pending");}
  else {begin_transition();page=(uint32_t)target;platform_log("Menu subpage opened");}
 }
 return 0;
}
static uint32_t color_lerp(uint32_t a,uint32_t b,float t){
 uint32_t out=0;for(int k=0;k<4;k++){float x=(a>>(k*8))&255,y=(b>>(k*8))&255;out|=(uint32_t)lroundf(x+(y-x)*t)<<(k*8);}return out;
}
static uint32_t alpha_scale(uint32_t c,float a){return (c&0xffffffu)|((uint32_t)lroundf((c>>24)*a)<<24);}
static int same_quad(const Quad*a,const Quad*b){return a->key==b->key&&a->texture==b->texture&&!memcmp(a->uv,b->uv,sizeof(a->uv));}
static void compose(const View*v,float t){
 unsigned char used[MAX_ANIM_QUADS]={0};current_count=0;
 for(unsigned i=0;i<v->count;i++){
  Quad q=v->quads[i];int match=-1;
  for(unsigned j=0;j<origin_count;j++)if(!used[j]&&same_quad(&q,&origin[j])){match=(int)j;break;}
  if(match>=0){used[match]=1;for(int k=0;k<8;k++)q.xy[k]=origin[match].xy[k]+(q.xy[k]-origin[match].xy[k])*t;q.color=color_lerp(origin[match].color,q.color,t);}
  else{q.color=alpha_scale(q.color,q.role==1?fmaxf(0,2*t-1):t);if(q.role)for(int k=0;k<4;k++)q.xy[k*2]+=18*(1-t);}
  current[current_count++]=q;
 }
 if(t<1)for(unsigned i=0;i<origin_count&&current_count<MAX_ANIM_QUADS;i++)if(!used[i]){
  Quad q=origin[i];q.color=alpha_scale(q.color,q.role==1?fmaxf(0,1-2*t):1-t);
  if(q.role)for(int k=0;k<4;k++)q.xy[k*2]-=18*t;
  if(q.color>>24)current[current_count++]=q;
 }
}
static void draw_quads(const Quad*quads,unsigned count,float alpha,float offset,int idle){
 static const int order[6]={0,1,2,1,3,2};float pulse=1+.018f*sinf((float)fmod(clock_seconds(),2.4)*6.2831853f/2.4f);
 for(uint32_t i=0;i<count;i++){
  const Quad*q=&quads[i];uint32_t color=alpha_scale(q->color,alpha);if(!(color>>24))continue;
  vita2d_texture_vertex *verts=vita2d_pool_memalign(6*sizeof(*verts),sizeof(float));
  if(!verts){platform_log("Menu vertex pool exhausted");return;}
  float cx=(q->xy[0]+q->xy[6])*.5f,cy=(q->xy[1]+q->xy[7])*.5f;
  float scale=idle&&q->role==2&&fabsf(cx-456)<1?pulse:1;
  for(int j=0;j<6;j++){
   int k=order[j];verts[j].x=(cx+(q->xy[k*2]-cx)*scale)*1.5f;verts[j].y=(cy+(q->xy[k*2+1]-cy)*scale+offset)*(544.0f/480);
   verts[j].z=0.5f;verts[j].u=q->uv[k*2];verts[j].v=q->uv[k*2+1];
  }
  vita2d_draw_array_textured(textures[q->texture].texture,SCE_GXM_PRIMITIVE_TRIANGLES,verts,6,color);
 }
}
void menu_present(const char *const *lines, unsigned count, int keyboard) {
 if(!page_count)return;
 vita2d_start_drawing();vita2d_clear_screen();
 uint64_t now=sceKernelGetProcessTimeWide();
 float dt=last_present?fminf(.05f,(now-last_present)/1000000.f):0;last_present=now;
 float t=fminf(1,(now-transition_start)/260000.f);t=t*t*(3-2*t);
 compose(&views[pages[page].first+selected[page]],t);draw_quads(current,current_count,1,0,t>=1);
 dialog_alpha+=((dialog?1.f:0)-dialog_alpha)*fminf(1,dt*16);
 if(dialog_alpha>.001f)draw_quads(views[view_count-1].quads,views[view_count-1].count,dialog_alpha,10*(1-dialog_alpha),0);
 if(lines){
  uint32_t signature=2166136261u;for(unsigned i=0;i<count&&i<7;i++){for(const unsigned char*p=(const unsigned char*)lines[i];*p;p++)signature=(signature^*p)*16777619u;signature=(signature^255)*16777619u;}
  if(signature!=overlay_signature){overlay_signature=signature;overlay_start=now;}
  float amount=fminf(1,(now-overlay_start)/160000.f);amount=amount*amount*(3-2*amount);
  renderer_overlay_transition(lines,count,amount,12*(1-amount));
 }else overlay_signature=0;
 if(!lines&&page==4)renderer_hint("X aceptar / Cuadrado: modo libre / Triangulo: escenas");
 vita2d_end_drawing();
 if(keyboard)vita2d_common_dialog_update();
 vita2d_swap_buffers();vita2d_wait_rendering_done();
}

void menu_draw(void) { menu_present(NULL,0,0); }

void menu_form(const char*title,const char*const*labels,const char*const*values,unsigned count,unsigned choice,const char*hint){
 if(!page_count)return;
 vita2d_start_drawing();vita2d_clear_screen();
 View*v=&views[pages[page].first+selected[page]];
 /* Preserve the original prepared backdrop and artwork; hide category labels and scroller under details. */
 for(unsigned i=0;i<v->count;i++)if(v->quads[i].role==0)draw_quads(&v->quads[i],1,1,0,0);
 renderer_form(title,labels,values,count,choice,hint);
 vita2d_end_drawing();vita2d_swap_buffers();vita2d_wait_rendering_done();
}
