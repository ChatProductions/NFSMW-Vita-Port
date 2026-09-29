#include "settings.h"
#include "menu_scene.h"
#include "platform.h"
#include "drive_audio.h"
#include "splash_audio.h"
#include <psp2/ctrl.h>
#include <stdio.h>
void settings_apply_audio(const GameSettings*s){drive_audio_volume(s->value[OPT_MUSIC],s->value[OPT_ENGINE],s->value[OPT_VOICE]);splash_audio_volume(s->value[OPT_MUSIC]);}
void settings_ui_category(unsigned category){
 static const int groups[7][5]={{OPT_MUSIC,OPT_ENGINE,OPT_VOICE,-1,-1},{OPT_VIEW,OPT_MIRROR,OPT_FPS,-1,-1},{OPT_CAMERA,OPT_HELP,-1,-1,-1},{OPT_CAMERA,OPT_HELP,-1,-1,-1},{OPT_DEADZONE,OPT_STEER,OPT_INVERT,-1,-1},{OPT_MUSIC,-1,-1,-1,-1},{-1,-1,-1,-1,-1}};
 static const char*titles[]={"SONIDO","GRAFICOS","OPCIONES DE JUEGO","JUGADOR","CONTROLES","MUSICA / EA TRAX","CREDITOS"};
 const char*names[]={"Camara","Retrovisor","Mostrar FPS","Ayuda de controles","Distancia de dibujo","Zona muerta","Sensibilidad","Invertir camara Y","Musica","Motor","Voz de Mia"};
 if(category>6)category=2;
 GameSettings s;settings_load(SETTINGS_ROOT,&s);unsigned selected=0,count=0,previous=platform_buttons();int error=0;while(count<5&&groups[category][count]>=0)count++;
 for(;;){unsigned buttons=platform_buttons(),pressed=buttons&~previous;previous=buttons;if(pressed&SCE_CTRL_CIRCLE)return;
  if(!count){const char*lines[]={"NFSMW-Vita","Karl Matvey","Colaboradores y bibliotecas:","ver THIRD_PARTY.md","Proyecto comunitario no oficial"};menu_form(titles[category],lines,NULL,5,5,"O volver");platform_wait_frame();continue;}
  if(pressed&SCE_CTRL_START){if(settings_save(SETTINGS_ROOT,&s)==0){settings_apply_audio(&s);return;}error=1;}
  if(pressed&SCE_CTRL_UP)selected=(selected+count-1)%count;if(pressed&SCE_CTRL_DOWN)selected=(selected+1)%count;
  if(pressed&SCE_CTRL_TRIANGLE){GameSettings initial;settings_defaults(&initial);for(unsigned i=0;i<count;i++)s.value[groups[category][i]]=initial.value[groups[category][i]];}
  int dir=pressed&SCE_CTRL_LEFT?-1:pressed&(SCE_CTRL_RIGHT|SCE_CTRL_CROSS)?1:0;int k=groups[category][selected];
  if(dir){int lo=k==OPT_DEADZONE?5:k==OPT_STEER?50:0,hi=k==OPT_DEADZONE?30:k==OPT_STEER?150:k>=OPT_MUSIC?100:(k==OPT_CAMERA||k==OPT_VIEW)?2:1,step=k==OPT_DEADZONE?5:k==OPT_STEER||k>=OPT_MUSIC?10:1,n=(int)s.value[k]+dir*step;s.value[k]=n<lo?hi:n>hi?lo:n;error=0;}
  char values[5][40];const char*labels[5],*value[5];
  for(unsigned i=0;i<count;i++){int k=groups[category][i];unsigned v=s.value[k];labels[i]=names[k];value[i]=values[i];if(k==OPT_CAMERA)snprintf(values[i],40,"%s",v==0?"Cercana":v==1?"Lejana":"Parachoques");else if(k==OPT_VIEW)snprintf(values[i],40,"%u m",90+v*30);else if(k==OPT_MIRROR||k==OPT_FPS||k==OPT_HELP||k==OPT_INVERT)snprintf(values[i],40,"%s",v?"Si":"No");else snprintf(values[i],40,"%u %%",v);}
  menu_form(titles[category],labels,value,count,selected,error?"No se pudo guardar. START reintenta / O cancela":category==5?"Volumen de musica; seleccion individual de canciones pendiente. START guardar / O cancelar":"Izq/der cambiar / Triangulo restablecer / START guardar / O cancelar");platform_wait_frame();
 }
}
void settings_ui_run(void){unsigned selected=0,previous=platform_buttons();const char*labels[]={"Sonido","Graficos","Opciones de juego","Jugador","Controles"};for(;;){unsigned b=platform_buttons(),p=b&~previous;previous=b;if(p&SCE_CTRL_CIRCLE)return;if(p&SCE_CTRL_UP)selected=(selected+4)%5;if(p&SCE_CTRL_DOWN)selected=(selected+1)%5;if(p&SCE_CTRL_CROSS){settings_ui_category(selected);previous=platform_buttons();}menu_form("OPCIONES",labels,NULL,5,selected,"Arriba/abajo elegir / X aceptar / O volver");platform_wait_frame();}}
