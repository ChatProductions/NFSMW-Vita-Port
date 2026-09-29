#include "drive_hud.h"
#include <math.h>
#include <stdio.h>
#define WHITE 0xffffffff
#define GOLD RGBA8(245,210,135,255)
static vita2d_texture *meter,*nos,*icons[5];
static DrivePoint trail[160];static unsigned trail_count,trail_head;
void drive_hud_free(void){if(meter)vita2d_free_texture(meter);if(nos)vita2d_free_texture(nos);for(int i=0;i<5;i++){if(icons[i])vita2d_free_texture(icons[i]);icons[i]=NULL;}meter=nos=NULL;trail_count=trail_head=0;}
void drive_hud_load(void){
 meter=vita2d_load_PNG_file("ux0:data/nfsmw/runtime/hud/meter.png");nos=vita2d_load_PNG_file("ux0:data/nfsmw/runtime/hud/nos.png");
 const char*names[]={"resume","restart","camera","controls","exit"};char path[128];
 for(int i=0;i<5;i++){snprintf(path,sizeof(path),"ux0:data/nfsmw/runtime/hud/%s.png",names[i]);icons[i]=vita2d_load_PNG_file(path);}
}
static void txt(vita2d_pgf*f,int x,int y,float size,unsigned color,const char*t){vita2d_pgf_draw_text(f,x,y,color,size,t);}
static void digit(int n,float x,float y){
 const unsigned masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};unsigned m=masks[n%10];
 const float r[7][4]={{3,0,18,3},{21,3,3,15},{21,21,3,15},{3,36,18,3},{0,21,3,15},{0,3,3,15},{3,18,18,3}};
 for(int i=0;i<7;i++)vita2d_draw_rectangle(x+r[i][0],y+r[i][1],r[i][2],r[i][3],m&(1u<<i)?GOLD:RGBA8(65,60,45,160));
}
void drive_hud_draw(vita2d_pgf*f,const DriveState*d,float fps,int help,int stream_error){
 char text[160];float x=843,y=418,r=90;
 vita2d_draw_fill_circle(x,y,r+7,RGBA8(5,8,9,180));
 if(meter)vita2d_draw_texture_scale(meter,x-r,y-r,180.f/256,180.f/256);
 for(int i=0;i<=40;i++){
  float a=(150+i*6)*.01745329252f;float inner=r-(i%4?5:12);unsigned c=i>=32?RGBA8(230,65,40,255):WHITE;
  vita2d_draw_line(x+cosf(a)*inner,y+sinf(a)*inner,x+cosf(a)*r,y+sinf(a)*r,c);
  if(i%4==0){snprintf(text,sizeof(text),"%d",i/4);txt(f,x+cosf(a)*(r-24)-4,y+sinf(a)*(r-24)+5,.65f,c,text);}
 }
 float a=(150+fminf(1,d->rpm/10000)*240)*.01745329252f;
 vita2d_draw_line(x,y,x+cosf(a)*69,y+sinf(a)*69,GOLD);vita2d_draw_fill_circle(x,y,4,GOLD);
 unsigned speed=(unsigned)fminf(999,fabsf(d->player.speed)*3.6f+.5f);digit(speed/100,x-41,y+10);digit(speed/10%10,x-13,y+10);digit(speed%10,x+15,y+10);
 txt(f,x-20,y+68,.6f,WHITE,"KM/H");snprintf(text,sizeof(text),d->gear<0?"R":"%d",d->gear);txt(f,x-6,y-22,1.1f,GOLD,text);
 if(nos)vita2d_draw_texture_scale(nos,752,510,.20f,.20f);
 vita2d_draw_rectangle(783,517,142,8,RGBA8(45,50,45,230));vita2d_draw_rectangle(783,517,142*d->nitro,8,d->boosting?WHITE:RGBA8(105,240,120,255));
 txt(f,24,30,.8f,WHITE,d->race_active==2?"CARRERA RAPIDA":d->race_active?"MODO CARRERA":"ROCKPORT / MODO LIBRE");snprintf(text,sizeof(text),"%02u:%02u    %.2f km",(unsigned)d->seconds/60,(unsigned)d->seconds%60,d->travel/1000);txt(f,24,53,.7f,GOLD,text);
 if(fps>=0){snprintf(text,sizeof(text),"%.0f FPS",fps);txt(f,865,28,.7f,WHITE,text);}
 if(stream_error)txt(f,330,35,.8f,GOLD,"FALTAN SECTORES DEL MAPA");
 /* Local driven trail, not an invented GPS street network. */
 if(d->seconds<.05f)trail_count=trail_head=0;
 if(!trail_count||hypotf(d->player.p.x-trail[(trail_head+159)%160].x,d->player.p.y-trail[(trail_head+159)%160].y)>2){trail[trail_head]=d->player.p;trail_head=(trail_head+1)%160;if(trail_count<160)trail_count++;}
 vita2d_draw_fill_circle(100,436,76,RGBA8(0,0,0,155));txt(f,69,530,.55f,GOLD,d->race_active?"RUTA":"RECORRIDO");txt(f,95,376,.6f,WHITE,"N");
 for(unsigned i=0;i<trail_count;i++){DrivePoint p=trail[i];float dx=(p.x-d->player.p.x)*.5f,dy=-(p.y-d->player.p.y)*.5f;if(dx*dx+dy*dy<4900)vita2d_draw_rectangle(100+dx,436+dy,2,2,GOLD);}
 for(int i=0;i<d->rival_count;i++){float dx=(d->rivals[i].p.x-d->player.p.x)*.5f,dy=-(d->rivals[i].p.y-d->player.p.y)*.5f;if(dx*dx+dy*dy<4900)vita2d_draw_fill_circle(100+dx,436+dy,3,RGBA8(240,100,60,255));}
 float sn=sinf(d->player.yaw),cs=cosf(d->player.yaw);vita2d_draw_line(100-sn*4,436+cs*4,100+sn*10,436-cs*10,WHITE);vita2d_draw_fill_circle(100,436,3,WHITE);
 if(help){vita2d_draw_rectangle(220,487,504,50,RGBA8(0,0,0,150));txt(f,230,506,.58f,WHITE,"X gas / Cuadrado freno / O freno de mano / R nitro");txt(f,230,525,.58f,WHITE,"L mirar atras / SELECT mapa / START pausa");}
}
void drive_hud_pause(vita2d_pgf*f,int choice,int camera_mode,int help,int fps,int confirm){
 vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,160));vita2d_draw_rectangle(220,65,520,418,RGBA8(15,17,15,235));
 txt(f,270,110,1.4f,GOLD,confirm?"VOLVER AL MENU?":"PAUSA");
 if(confirm){txt(f,270,200,.9f,WHITE,"Finaliza el recorrido actual.");txt(f,270,270,.9f,GOLD,"X confirmar / O cancelar");return;}
 char camera[80],helpers[80],counter[80];snprintf(camera,sizeof(camera),"Camara: %s",camera_mode==0?"cercana":camera_mode==1?"lejana":"parachoques");snprintf(helpers,sizeof(helpers),"Ayuda: %s",help?"visible":"oculta");snprintf(counter,sizeof(counter),"Contador FPS: %s",fps?"visible":"oculto");
 const char*items[]={"Continuar","Recolocar coche",camera,helpers,"Opciones","Volver al menu"};int mapping[]={0,1,2,3,-1,4};
 for(int i=0;i<6;i++){int y=158+i*45;if(i==choice)vita2d_draw_rectangle(244,y-25,470,37,RGBA8(95,80,45,200));if(mapping[i]>=0&&icons[mapping[i]])vita2d_draw_texture_scale(icons[mapping[i]],255,y-27,.13f,.13f);txt(f,300,y,.92f,i==choice?GOLD:WHITE,items[i]);}
 txt(f,260,457,.65f,WHITE,"Arriba/abajo elegir / X aceptar / O continuar");
}

void drive_hud_race(vita2d_pgf*f,const FirstRace*r,const DriveState*d){
 char text[160];snprintf(text,sizeof(text),"POS %d/2   %.0f%%   %s",r->place,fminf(100,100*(r->start_fraction+(1-r->start_fraction)*r->progress/r->total)),r->opponent_name?r->opponent_name:"RIVAL");vita2d_draw_rectangle(20,66,300,36,RGBA8(0,0,0,170));txt(f,30,91,.8f,GOLD,text);
 if(r->countdown>0){snprintf(text,sizeof(text),"%.0f",ceilf(r->countdown));vita2d_draw_rectangle(420,200,120,100,RGBA8(0,0,0,180));txt(f,466,268,2.5f,GOLD,text);}
 else if(r->initial_countdown>0&&r->elapsed<1)txt(f,445,268,1.8f,GOLD,"YA!");
 const RaceGate*g=first_race_target(r);
 if(g){
  unsigned guide=r->segment+8;if(guide>=r->count)guide=r->count-1;
  float x=r->points[guide].x-d->player.p.x,y=r->points[guide].y-d->player.p.y,angle=atan2f(x,y)-d->player.yaw;angle=atan2f(sinf(angle),cosf(angle));
  if(r->progress>g->distance+25){x=g->p.x-d->player.p.x;y=g->p.y-d->player.p.y;angle=atan2f(sinf(atan2f(x,y)-d->player.yaw),cosf(atan2f(x,y)-d->player.yaw));}
  float sn=sinf(angle),cs=cosf(angle);vita2d_draw_fill_circle(480,75,32,RGBA8(0,0,0,180));
  vita2d_draw_line(480-sn*15,75+cs*15,480+sn*23,75-cs*23,GOLD);
  vita2d_draw_line(480+sn*23,75-cs*23,480+sn*8+cs*10,75-cs*8+sn*10,GOLD);
  vita2d_draw_line(480+sn*23,75-cs*23,480+sn*8-cs*10,75-cs*8-sn*10,GOLD);
  snprintf(text,sizeof(text),"%s %.0f m",r->next+1==r->gate_count?"META":"RUTA",fmaxf(0,g->distance-r->progress));txt(f,429,127,.7f,WHITE,text);
  if(fabsf(angle)>2.2f&&d->player.speed>2)txt(f,355,162,.8f,GOLD,"REVISA LA DIRECCION");
 }
 for(unsigned i=1;i<r->count;i++){
  float x=(r->points[i-1].x-d->player.p.x)*.5f,y=-(r->points[i-1].y-d->player.p.y)*.5f;
  float a=(r->points[i].x-d->player.p.x)*.5f,b=-(r->points[i].y-d->player.p.y)*.5f;
  if(x*x+y*y<4900&&a*a+b*b<4900)vita2d_draw_line(100+x,436+y,100+a,436+b,GOLD);
 }
 if(r->finished){
  vita2d_draw_rectangle(190,140,580,265,RGBA8(0,0,0,230));txt(f,245,192,1.3f,GOLD,"TRAMO COMPLETADO");
  snprintf(text,sizeof(text),"Posicion %d/2    Tiempo del tramo %.2f s",r->place,r->elapsed);txt(f,245,240,.85f,WHITE,text);
  txt(f,245,281,.75f,WHITE,"Prologo parcial: la historia continua pendiente.");
  txt(f,245,345,.85f,GOLD,"X repetir / O volver al menu");
 }
}

void drive_hud_roam(vita2d_pgf*f,FreeRoam*r,const DriveState*d){
 roam_nearby(r,d->player.p);
 for(unsigned i=0;i<r->near_count;i++){
  RoamRoad t=r->roads[r->nearby[i]];
  float x=(t.a.x-d->player.p.x)*.5f,y=-(t.a.y-d->player.p.y)*.5f,a=(t.b.x-d->player.p.x)*.5f,b=-(t.b.y-d->player.p.y)*.5f;
  float dx=a-x,dy=b-y,A=dx*dx+dy*dy,B=2*(x*dx+y*dy),C=x*x+y*y-4900;
  if(A<.0001f)continue;
  float disc=B*B-4*A*C;if(disc<0)continue;float root=sqrtf(disc),lo=fmaxf(0,(-B-root)/(2*A)),hi=fminf(1,(-B+root)/(2*A));if(hi<=lo)continue;
  unsigned color=fabsf((t.a.z+t.b.z)*.5f-d->player.p.z)<8?RGBA8(190,190,160,220):RGBA8(90,100,95,190);
  vita2d_draw_line(100+x+dx*lo,436+y+dy*lo,100+x+dx*hi,436+y+dy*hi,color);
 }
 float sn=sinf(d->player.yaw),cs=cosf(d->player.yaw);vita2d_draw_line(100-sn*4,436+cs*4,100+sn*10,436-cs*10,WHITE);vita2d_draw_fill_circle(100,436,3,WHITE);
 if(r->save_failed)txt(f,24,77,.6f,GOLD,"NO SE PUDO GUARDAR LA POSICION");
}
