#include "frontend_scene.h"
#include "platform.h"
#include <vita2d.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ROOT "ux0:data/nfsmw/frontend/"
#define MAX_QUADS 4096
#define MAX_TEXTURES 16
/* Generated little-endian stream, ARM and host both IEEE754. */
typedef struct { uint32_t texture, color; float x,y,w,h,u0,v0,u1,v1; } Quad;
_Static_assert(sizeof(Quad)==40,"quad layout");
static Quad *quads, *light_frames;
static vita2d_texture *light_texture;
static uint32_t frames, lights, step_ms, prompt_first, prompt_count;
static uint64_t started;
static int load_animation(void);
static uint32_t count, texture_count;
static struct { uint32_t id; vita2d_texture *texture; } textures[MAX_TEXTURES];
static uint32_t big32(const unsigned char *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
void frontend_unload(void) {
    vita2d_wait_rendering_done();
    for (uint32_t i=0;i<texture_count;i++) vita2d_free_texture(textures[i].texture);
    texture_count=0; free(quads); quads=NULL; count=0;
    free(light_frames);light_frames=NULL;frames=lights=0;
    if (light_texture) vita2d_free_texture(light_texture);
    light_texture=NULL;
}
int frontend_load(void) {
    frontend_unload();
    FILE *file=fopen(ROOT "splash.nfm","rb");
    if (!file) { platform_log("Frontend scene missing"); return -1; }
    unsigned char magic[4]; uint32_t version=0;
    int valid=fread(magic,1,4,file)==4 && memcmp(magic,"NFMF",4)==0 &&
        fread(&version,4,1,file)==1 && version==1 && fread(&count,4,1,file)==1 && count>0 && count<=MAX_QUADS;
    if (valid) {
        quads=calloc(count,sizeof(Quad));
        valid=quads && fread(quads,sizeof(Quad),count,file)==count && fgetc(file)==EOF;
    }
    fclose(file);
    if (!valid) goto fail;
    size_t memory=0;
    for (uint32_t i=0;i<count;i++) {
        Quad *q=&quads[i];
        if (!isfinite(q->x)||!isfinite(q->y)||!isfinite(q->w)||!isfinite(q->h)||
            !isfinite(q->u0)||!isfinite(q->v0)||!isfinite(q->u1)||!isfinite(q->v1)||
            fabsf(q->x)>4096||fabsf(q->y)>4096||q->w<=0||q->h<=0||q->w>4096||q->h>4096||
            q->u0<0||q->v0<0||q->u1>1||q->v1>1||q->u1<=q->u0||q->v1<=q->v0) goto fail;
        uint32_t index=0;
        while (index<texture_count && textures[index].id!=q->texture) index++;
        if (index==texture_count) {
            if (texture_count==MAX_TEXTURES) goto fail;
            char path[128];snprintf(path,sizeof(path),ROOT "%08x.png",(unsigned)q->texture);
            file=fopen(path,"rb"); if (!file) goto fail;
            unsigned char h[24];valid=fread(h,1,sizeof(h),file)==sizeof(h);fclose(file);
            if (!valid || memcmp(h,"\x89PNG\r\n\x1a\n",8) || memcmp(h+12,"IHDR",4)) goto fail;
            uint32_t w=big32(h+16),height=big32(h+20);
            if (!w||!height||w>2048||height>2048) goto fail;
            memory+=(size_t)w*height*4;
            if (memory>32*1024*1024) goto fail;
            vita2d_texture *t=vita2d_load_PNG_file(path); if (!t) goto fail;
            textures[texture_count].id=q->texture;textures[texture_count++].texture=t;
        }
        q->texture=index;
    }
    if (load_animation()<0) goto fail;
    started=sceKernelGetProcessTimeWide();
    platform_log("Original light loops loaded; fullscreen splash"); return 0;
fail:
    platform_log("Frontend scene invalid or texture unavailable");frontend_unload();return -1;
}
static int load_animation(void) {
    FILE *f=fopen(ROOT "lights.nfa","rb");
    if (!f) return -1;
    char magic[4];uint32_t h[5];
    int ok=fread(magic,1,4,f)==4 && !memcmp(magic,"NFA1",4) && fread(h,4,5,f)==5;
    if (ok) {
        frames=h[0];lights=h[1];step_ms=h[2];prompt_first=h[3];prompt_count=h[4];
        ok=frames>0&&frames<=16384&&lights>0&&lights<=8&&step_ms>=10&&step_ms<=1000&&
           prompt_first<count&&prompt_count<=count-prompt_first;
    }
    if (ok) {
        size_t n=(size_t)frames*lights;
        light_frames=calloc(n,sizeof(Quad));
        ok=light_frames && fread(light_frames,sizeof(Quad),n,f)==n && fgetc(f)==EOF;
        for (size_t i=0;ok&&i<n;i++) {
            Quad *q=&light_frames[i];
            ok=q->texture==0x3394fe62 && isfinite(q->x)&&isfinite(q->y)&&isfinite(q->w)&&isfinite(q->h)&&
               fabsf(q->x)<4096&&fabsf(q->y)<4096&&q->w>0&&q->h>0&&q->w<2048&&q->h<2048&&
               q->u0==0&&q->v0==0&&q->u1==1&&q->v1==1;
        }
    }
    fclose(f);if (!ok) return -1;
    f=fopen(ROOT "3394fe62.png","rb");if (!f) return -1;
    unsigned char ph[24];ok=fread(ph,1,24,f)==24;fclose(f);
    if (!ok||memcmp(ph,"\x89PNG\r\n\x1a\n",8)||big32(ph+16)>512||big32(ph+20)>512) return -1;
    light_texture=vita2d_load_PNG_file(ROOT "3394fe62.png");return light_texture?0:-1;
}
static uint32_t blend_color(uint32_t a,uint32_t b,float f) {
    uint32_t result=0;
    for (int i=0;i<4;i++) {
        float x=(a>>(8*i))&255,y=(b>>(8*i))&255;
        result|=(uint32_t)(x+(y-x)*f)<<(8*i);
    }
    return result;
}
static void draw_quad(const Quad *q,vita2d_texture *t) {
    const float sx=960.0f/640.0f,sy=544.0f/480.0f;
    float tw=vita2d_texture_get_width(t),th=vita2d_texture_get_height(t);
    float sw=(q->u1-q->u0)*tw,sh=(q->v1-q->v0)*th;
    vita2d_draw_texture_tint_part_scale(t,q->x*sx,q->y*sy,q->u0*tw,q->v0*th,sw,sh,q->w*sx/sw,q->h*sy/sh,q->color);
}
void frontend_draw(void) {
    uint64_t ms=(sceKernelGetProcessTimeWide()-started)/1000;
    uint32_t frame=(ms/step_ms)%frames,next=(frame+1)%frames;
    float f=(float)(ms%step_ms)/step_ms;
    vita2d_start_drawing();vita2d_clear_screen();
    for (uint32_t i=0;i<count;i++) {
        if (i==1) {
            vita2d_set_blend_mode_add(1);
            for (uint32_t j=0;j<lights;j++) {
                Quad *a=&light_frames[frame*lights+j],*b=&light_frames[next*lights+j],q=*a;
                q.x+=(b->x-a->x)*f;q.y+=(b->y-a->y)*f;q.w+=(b->w-a->w)*f;q.h+=(b->h-a->h)*f;
                q.color=blend_color(a->color,b->color,f);draw_quad(&q,light_texture);
            }
            vita2d_set_blend_mode_add(0);
        }
        Quad q=quads[i];
        if (i>=prompt_first && i<prompt_first+prompt_count) {
            float opacity=0.25f+0.75f*(0.5f+0.5f*cosf((float)(ms%2400)*6.2831853f/2400.0f));
            uint32_t alpha=(uint32_t)((q.color>>24)*opacity);
            q.color=(q.color&0xffffff)|(alpha<<24);
        }
        draw_quad(&q,textures[q.texture].texture);
    }
    vita2d_end_drawing();vita2d_swap_buffers();vita2d_wait_rendering_done();
}
