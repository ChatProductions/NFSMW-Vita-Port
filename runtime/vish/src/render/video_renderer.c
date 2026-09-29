#include "renderer.h"
#include "loading_data.h"
#include "platform.h"
#include <vita2d.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
static vita2d_pgf *font;
static LoadingQuad loading_quads[256];static unsigned loading_ids[16],loading_count,loading_textures;
static vita2d_texture *loading_images[16];
void renderer_loading_end(void){vita2d_wait_rendering_done();for(unsigned i=0;i<16;i++){if(loading_images[i])vita2d_free_texture(loading_images[i]);loading_images[i]=NULL;}loading_count=loading_textures=0;}
static void loading_open(void){
 renderer_loading_end();FILE*f=fopen("ux0:data/nfsmw/loading/transition.nfl","rb");if(!f)return;
 unsigned h[4];int ok=fread(h,4,4,f)==4&&!memcmp(h,"NFLD",4)&&h[1]==1&&h[2]>0&&h[2]<=16&&h[3]>0&&h[3]<=256;
 if(ok){loading_textures=h[2];loading_count=h[3];ok=fread(loading_ids,4,loading_textures,f)==loading_textures&&fread(loading_quads,sizeof(LoadingQuad),loading_count,f)==loading_count&&fgetc(f)==EOF;}
 fclose(f);
 for(unsigned i=0;i<loading_count&&ok;i++)ok=loading_quad_valid(&loading_quads[i],loading_ids,loading_textures);
 for(unsigned i=0;i<loading_textures&&ok;i++){char path[128];snprintf(path,sizeof(path),"ux0:data/nfsmw/loading/%08x.png",loading_ids[i]);loading_images[i]=vita2d_load_PNG_file(path);if(!loading_images[i])ok=0;else{sceGxmTextureSetUAddrMode(&loading_images[i]->gxm_tex,SCE_GXM_TEXTURE_ADDR_REPEAT);sceGxmTextureSetVAddrMode(&loading_images[i]->gxm_tex,SCE_GXM_TEXTURE_ADDR_REPEAT);}}
 if(!ok){platform_log("Loading artwork rejected: invalid layout or missing texture");renderer_loading_end();}else platform_log("Original police-light loading artwork ready");
}
static void loading_draw(void){
 const unsigned order[6]={0,1,2,2,1,3};
 for(unsigned i=0;i<loading_count;i++){LoadingQuad*q=&loading_quads[i];unsigned texture=0;while(texture<loading_textures&&loading_ids[texture]!=q->texture)texture++;if(texture==loading_textures)continue;
  vita2d_texture_vertex*v=vita2d_pool_memalign(6*sizeof(*v),4);if(!v)break;
  for(int k=0;k<6;k++){unsigned j=order[k];v[k]=(vita2d_texture_vertex){q->xy[j*2]*1.5f,q->xy[j*2+1]*544.f/480.f,.5f,q->uv[j*2],q->uv[j*2+1]};}
  vita2d_draw_array_textured(loading_images[texture],SCE_GXM_PRIMITIVE_TRIANGLES,v,6,q->color);
 }
}
static void text(int x, int y, const char *s) {
    vita2d_pgf_draw_text(font, x, y, RGBA8(237,228,194,255), 1.0f, s);
}
int renderer_init(void) {
    if (vita2d_init_advanced(3*1024*1024) < 0) return -1;
    vita2d_set_clear_color(RGBA8(12,15,18,255));
    font = vita2d_load_default_pgf();
    if (!font) { vita2d_fini(); return -1; }
    return 0;
}
static void present(void) {
    vita2d_end_drawing(); vita2d_swap_buffers(); vita2d_wait_rendering_done();
}
void renderer_loading(const char *stage, unsigned step, unsigned total) {
    (void)total;platform_log(stage);
    if(step==0)loading_open();
    vita2d_start_drawing(); vita2d_clear_screen();
    vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,255));
    if(loading_count)loading_draw();else text(48,410,"CARGANDO...");
    vita2d_pgf_draw_text(font,48,525,RGBA8(220,220,220,255),.75f,"Creado por Karl Matvey");
    present();
}
void renderer_video_wait(void) {
    vita2d_start_drawing();vita2d_clear_screen();
    vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,255));
    present();
}
void renderer_status(AssetResult result, unsigned checks, int video_result) {
    char line[160];snprintf(line,sizeof(line),"Frontend unavailable: asset=%d video=%d retries=%u",result,video_result,checks);platform_log(line);
    vita2d_start_drawing();vita2d_clear_screen();
    text(40,150,"No se pudo abrir el menu.");
    text(40,205,"Comprueba que los recursos del juego esten copiados.");
    text(40,300,"X: volver a intentar    START: salir");
    present();
}
void renderer_shutdown(void) {
    renderer_loading_end();
    vita2d_wait_rendering_done();
    if (font) vita2d_free_pgf(font);
    vita2d_fini();
}

void renderer_overlay_transition(const char *const *lines, unsigned count,float alpha,float offset) {
    alpha=fmaxf(0,fminf(1,alpha));
    vita2d_draw_rectangle(50,100+offset,860,330,RGBA8(0,0,0,(unsigned)(235*alpha)));
    for (unsigned i=0;i<count && i<7;i++) vita2d_pgf_draw_text(font,75,140+40*i+offset,RGBA8(237,228,194,(unsigned)(255*alpha)),1,lines[i]);
}
void renderer_overlay(const char *const *lines, unsigned count) {renderer_overlay_transition(lines,count,1,0);}

void renderer_hint(const char*message){vita2d_pgf_draw_text(font,30,525,RGBA8(240,220,170,255),.6f,message);}

/* Details sheets share the existing background, without the diagnostic modal box. */
void renderer_form(const char*title,const char*const*labels,const char*const*values,unsigned count,unsigned selected,const char*hint){
 vita2d_draw_rectangle(300,55,640,390,RGBA8(10,12,10,180));
 vita2d_pgf_draw_text(font,328,103,RGBA8(235,231,212,255),1.3f,title);
 for(unsigned i=0;i<count&&i<5;i++){float y=157+i*54;unsigned color=i==selected?RGBA8(255,216,105,255):RGBA8(229,229,220,255);
  if(i==selected){vita2d_draw_rectangle(317,y-25,600,39,RGBA8(112,90,34,190));vita2d_draw_rectangle(309,y-25,5,39,color);}
  vita2d_pgf_draw_text(font,335,y,color,.8f,labels[i]);if(values)vita2d_pgf_draw_text(font,725,y,color,.8f,values[i]);
 }
 vita2d_draw_rectangle(20,477,920,57,RGBA8(0,0,0,200));vita2d_pgf_draw_text(font,35,510,RGBA8(240,223,177,255),.62f,hint);
}
