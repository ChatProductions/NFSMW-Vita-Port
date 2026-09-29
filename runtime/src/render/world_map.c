#include "world_map.h"
#include "gfx_guard.h"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIZE 1024
#define GOLD RGBA8(239,197,92,255)
#define WHITE 0xffffffff
/* Rasterize the network once per opening, rather than submit thousands of roads every frame. */
static void raster(uint32_t *pixels,unsigned stride,int x,int y,int a,int b,unsigned color){
 int dx=abs(a-x),sx=x<a?1:-1,dy=-abs(b-y),sy=y<b?1:-1,e=dx+dy;
 for(;;){if(x>=0&&x<SIZE&&y>=0&&y<SIZE)pixels[y*stride+x]=color;if(x==a&&y==b)break;int twice=e*2;if(twice>=dy){e+=dy;x+=sx;}if(twice<=dx){e+=dx;y+=sy;}}
}
static float clamp(float x,float lo,float hi){return fmaxf(lo,fminf(hi,x));}
static void text(vita2d_pgf*f,int x,int y,float size,unsigned color,const char*s){vita2d_pgf_draw_text(f,x,y,color,size,s);}
static void marker(float px,float py,float cx,float cy,float zoom,unsigned color){float x=480+(px-cx)*zoom,y=272+(py-cy)*zoom;if(x>=24&&x<=936&&y>=72&&y<=470)vita2d_draw_fill_circle(x,y,4,color);}
void world_map_run(vita2d_pgf*f,const FreeRoam*roads,const DriveState*d,const FirstRace*r){
 float minx=d->player.p.x-10,maxx=d->player.p.x+10,miny=d->player.p.y-10,maxy=d->player.p.y+10;
 for(unsigned i=0;i<roads->count;i++){DrivePoint p[2]={roads->roads[i].a,roads->roads[i].b};for(int k=0;k<2;k++){minx=fminf(minx,p[k].x);maxx=fmaxf(maxx,p[k].x);miny=fminf(miny,p[k].y);maxy=fmaxf(maxy,p[k].y);}}
 if(r)for(unsigned i=0;i<r->count;i++){minx=fminf(minx,r->points[i].x);maxx=fmaxf(maxx,r->points[i].x);miny=fminf(miny,r->points[i].y);maxy=fmaxf(maxy,r->points[i].y);}
 float extent=fmaxf(maxx-minx,maxy-miny)+80,unit=1000/extent;
 float ox=(minx+maxx-extent)*.5f,oy=(miny+maxy+extent)*.5f;
 vita2d_texture*map=vita2d_create_empty_texture(SIZE,SIZE);uint32_t*pixels=map?vita2d_texture_get_datap(map):NULL;
 if(map&&pixels){unsigned stride=vita2d_texture_get_stride(map)/4;memset(pixels,0,stride*SIZE*4);for(unsigned i=0;i<roads->count;i++){RoamRoad a=roads->roads[i];raster(pixels,stride,(a.a.x-ox)*unit,(oy-a.a.y)*unit,(a.b.x-ox)*unit,(oy-a.b.y)*unit,RGBA8(161,164,144,255));}}
 float cx=500,cy=500,zoom=.39f,playerx=(d->player.p.x-ox)*unit,playery=(oy-d->player.p.y)*unit;
 unsigned previous=0;SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);previous=pad.buttons;
 SceTouchPanelInfo info={0};SceTouchSamplingState old=SCE_TOUCH_SAMPLING_STATE_STOP;int ready=sceTouchGetPanelInfo(0,&info)>=0&&info.maxAaX>info.minAaX&&info.maxAaY>info.minAaY;int held=1;float tx=0,ty=0;
 if(ready){sceTouchGetSamplingState(0,&old);ready=sceTouchSetSamplingState(0,SCE_TOUCH_SAMPLING_STATE_START)>=0;}
 uint64_t last=sceKernelGetProcessTimeWide();int route=1;
 for(;;){sceCtrlPeekBufferPositive(0,&pad,1);unsigned pressed=pad.buttons&~previous;previous=pad.buttons;
  if(pressed&(SCE_CTRL_SELECT|SCE_CTRL_CIRCLE|SCE_CTRL_START))break;
  uint64_t now=sceKernelGetProcessTimeWide();float dt=fminf(.05f,(now-last)/1000000.f);last=now;
  float x=((int)pad.lx-128)/127.f,y=((int)pad.ly-128)/127.f;
  if(pad.buttons&SCE_CTRL_LEFT)x=-1;
  if(pad.buttons&SCE_CTRL_RIGHT)x=1;
  if(pad.buttons&SCE_CTRL_UP)y=-1;
  if(pad.buttons&SCE_CTRL_DOWN)y=1;
  if(fabsf(x)>.18f)cx+=x*300*dt/zoom;
  if(fabsf(y)>.18f)cy+=y*300*dt/zoom;
  if(pad.buttons&SCE_CTRL_RTRIGGER)zoom*=1+dt;
  if(pad.buttons&SCE_CTRL_LTRIGGER)zoom/=1+dt;zoom=clamp(zoom,.39f,4);
  if(pressed&SCE_CTRL_TRIANGLE){cx=playerx;cy=playery;}
  if(pressed&SCE_CTRL_SQUARE){cx=cy=500;zoom=.39f;}
  if(pressed&SCE_CTRL_CROSS)route=!route;
  if(ready){SceTouchData touch={0};if(sceTouchPeek(0,&touch,1)>0){if(touch.reportNum){float nx=(touch.report[0].x-info.minAaX)*960.f/(info.maxAaX-info.minAaX),ny=(touch.report[0].y-info.minAaY)*544.f/(info.maxAaY-info.minAaY);
    if(!held){if(nx>875&&ny<65)break;if(nx>875&&ny>410&&ny<475)zoom=clamp(zoom*1.4f,.39f,4);else if(nx<85&&ny>410&&ny<475)zoom=clamp(zoom/1.4f,.39f,4);}else if(tx>=24&&tx<=936&&ty>=72&&ty<=470&&ny>=72&&ny<=470){cx-=(nx-tx)/zoom;cy-=(ny-ty)/zoom;}tx=nx;ty=ny;}held=touch.reportNum>0;}}
  cx=clamp(cx,0,1024);cy=clamp(cy,0,1024);
  vita2d_start_drawing();vita2d_clear_screen();vita2d_draw_rectangle(0,0,960,544,RGBA8(15,20,17,255));
  /* Crop by source rectangle; no out-of-range texture coordinates or expensive world rendering. */
  float left=fmaxf(0,cx-456/zoom),top=fmaxf(0,cy-198/zoom),right=fminf(1024,cx+456/zoom),bottom=fminf(1024,cy+198/zoom);
  if(map&&pixels&&right>left&&bottom>top)vita2d_draw_texture_part_scale(map,480+(left-cx)*zoom,272+(top-cy)*zoom,left,top,right-left,bottom-top,zoom,zoom);
  if(route&&r)for(unsigned i=1;i<r->count;i++){float x1=480+((r->points[i-1].x-ox)*unit-cx)*zoom,y1=272+((oy-r->points[i-1].y)*unit-cy)*zoom,x2=480+((r->points[i].x-ox)*unit-cx)*zoom,y2=272+((oy-r->points[i].y)*unit-cy)*zoom;
   if(x1>=24&&x1<=936&&x2>=24&&x2<=936&&y1>=72&&y1<=470&&y2>=72&&y2<=470)vita2d_draw_line(x1,y1,x2,y2,GOLD);}
  marker(playerx,playery,cx,cy,zoom,WHITE);float sx=480+(playerx-cx)*zoom,sy=272+(playery-cy)*zoom;if(sx>=36&&sx<=924&&sy>=84&&sy<=458)vita2d_draw_line(sx,sy,sx+sinf(d->player.yaw)*12,sy-cosf(d->player.yaw)*12,WHITE);
  for(int i=0;i<d->rival_count;i++)marker((d->rivals[i].p.x-ox)*unit,(oy-d->rivals[i].p.y)*unit,cx,cy,zoom,RGBA8(244,89,47,255));
  if(route&&r&&r->count)marker((r->points[r->count-1].x-ox)*unit,(oy-r->points[r->count-1].y)*unit,cx,cy,zoom,GOLD);
  text(f,30,42,1.2f,GOLD,"ROCKPORT / MAPA");text(f,890,42,.9f,WHITE,"O");text(f,30,455,1.4f,WHITE,"-");text(f,900,455,1.4f,WHITE,"+");text(f,35,94,.65f,WHITE,"N ^");
  text(f,30,498,.62f,WHITE,"Stick / arrastrar: mover   L/R: zoom   Triangulo: coche   Cuadrado: ciudad");
  text(f,30,525,.62f,GOLD,"SELECT / O: volver   X: ruta   Blanco: tu coche   Rojo: rival   Oro: ruta/meta");
  if(!map||!pixels)text(f,230,250,.8f,WHITE,"No hay memoria para dibujar las calles");else if(!roads->count)text(f,230,250,.8f,WHITE,"Falta world/roam.nfm: solo ruta disponible");
  vita2d_end_drawing();vita2d_swap_buffers();vita2d_wait_rendering_done();if(gfx_guard_error())break;
 }
 vita2d_wait_rendering_done();if(map)vita2d_free_texture(map);if(ready)sceTouchSetSamplingState(0,old);
}
