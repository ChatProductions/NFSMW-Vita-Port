#include "quick_race.h"
#include "menu_scene.h"
#include "platform.h"
#include "renderer.h"
#include <psp2/ctrl.h>
void quick_race_ui(int custom){QuickRace q={2,1,1};unsigned choice=0,previous=platform_buttons();
 if(custom)for(;;){unsigned b=platform_buttons(),p=b&~previous;previous=b;if(p&SCE_CTRL_CIRCLE)return;if(p&SCE_CTRL_UP)choice=(choice+3)%4;if(p&SCE_CTRL_DOWN)choice=(choice+1)%4;
  int dir=p&SCE_CTRL_LEFT?-1:p&(SCE_CTRL_RIGHT|SCE_CTRL_CROSS)?1:0;
  if(dir){if(choice==0)q.event=q.event==2?3:2;else if(choice==1)q.difficulty=(q.difficulty+dir+3)%3;else if(choice==2)q.traffic=!q.traffic;else if(p&SCE_CTRL_CROSS)break;}
  if(p&SCE_CTRL_START)break;
  const char*labels[]={"Recorrido sprint","Dificultad","Trafico","Comenzar"};const char*values[]={q.event==2?"Ronnie":"Bull",q.difficulty==0?"Facil":q.difficulty==1?"Normal":"Dificil",q.traffic?"Si":"No","BMW M3 GTR"};
  menu_form("CARRERA PERSONALIZADA",labels,values,4,choice,"Izq/der cambiar / START correr / O volver (un rival)");platform_wait_frame();
 }
 int result=drive_run_quick(&q);renderer_loading_end();if(result<0){unsigned previous=platform_buttons();for(;;){unsigned b=platform_buttons();if((b&~previous)&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS))break;previous=b;const char*lines[]={"No se pudo abrir la carrera.","Comprueba los recursos de world y race."};menu_form("CARRERA RAPIDA",lines,0,2,2,"X / O volver");platform_wait_frame();}}
}
