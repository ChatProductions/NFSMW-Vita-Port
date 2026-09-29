#include "career_ui.h"
#include "renderer.h"
#include "career_store.h"
#include "drive_scene.h"
#include "menu_scene.h"
#include "platform.h"
#include "video_player.h"
#include "splash_audio.h"
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <string.h>
#define SAVES "ux0:data/nfsmw/save"
/* These informational panels are prototype UI, not reconstructed FEng dialogs. */
static int panel(const char *const *lines, unsigned count, int confirm) {
    uint32_t previous=platform_buttons();
    for (;;) {
        uint32_t buttons=platform_buttons(), pressed=buttons&~previous;
        if(pressed&SCE_CTRL_CIRCLE)return 0;
        if(pressed&SCE_CTRL_CROSS)return confirm?1:0;
        if(confirm==2&&(pressed&SCE_CTRL_SQUARE))return 2;
        if(confirm==2&&(pressed&SCE_CTRL_TRIANGLE))return 3;
        menu_present(lines,count,0);platform_wait_frame();previous=buttons;
    }
}
static void notice(const char *message) {
    const char *lines[]={"MODO CARRERA",message,"X / O  Volver"};panel(lines,3,0);
}
static int name_input(char alias[32]) {
    static int loaded;
    if(!loaded) {
        int result=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if(result<0){platform_log("Career keyboard module failed");return -1;}
        loaded=1; /* Keep resident, as with AVPlayer. */
    }
    SceWChar16 title[]={'N','o','m','b','r','e',' ','d','e','l',' ','p','i','l','o','t','o',0};
    SceWChar16 initial[]={'P','I','L','O','T','O',0}, output[32]={0};
    SceImeDialogParam param;sceImeDialogParamInit(&param);
    param.title=title;param.initialText=initial;param.inputTextBuffer=output;
    param.maxTextLength=31;param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    if(sceImeDialogInit(&param)<0)return -1;
    while(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING) {
        menu_present(NULL,0,1);platform_wait_frame();
    }
    SceImeDialogResult result;memset(&result,0,sizeof(result));
    int ok=sceImeDialogGetResult(&result);sceImeDialogTerm();
    if(ok<0||result.result<0)return -1;
    if(result.button!=SCE_IME_DIALOG_BUTTON_ENTER)return 0;
    unsigned i=0;
    for(;i<31&&output[i];i++) {
        if(output[i]<32||output[i]>126)return -2;
        alias[i]=(char)output[i];
    }
    alias[i]=0;
    /* Avoid invisible aliases; keep the on-disk format bounded and portable. */
    while(i&&alias[i-1]==' ')alias[--i]=0;
    return i?1:-2;
}
static int select_scene(const CareerSave*save){
 const char*names[]={"Adelanto contra Razor","Sprint contra Ronnie","Sprint contra Bull","Razor: prueba de la averia"};
 unsigned previous=platform_buttons();int selected=0;
 for(;;){
  unsigned buttons=platform_buttons(),pressed=buttons&~previous;previous=buttons;
  if(pressed&SCE_CTRL_CIRCLE)return -1;
  if(pressed&SCE_CTRL_DOWN)selected=(selected+1)%4;
  if(pressed&SCE_CTRL_UP)selected=(selected+3)%4;
  if(pressed&SCE_CTRL_CROSS){
   if(selected==1&&!save->first_finishes){notice("Completa primero el adelanto de Razor.");continue;}
   if(selected==2&&!save->ronnie_finishes){notice("Completa primero el sprint de Ronnie.");continue;}
   return selected+1;
  }
  char text[4][80];const char*lines[7]={"ESCENAS DE CARRERA",NULL,NULL,NULL,NULL,"Arriba/abajo elegir / X aceptar / O volver","El circuito de Rog sigue pendiente"};
  for(int i=0;i<4;i++){snprintf(text[i],sizeof(text[i]),"%s %s",i==selected?">":" ",names[i]);lines[i+1]=text[i];}
  menu_present(lines,7,0);platform_wait_frame();
 }
}
void career_ui_run(int action) {
    CareerSave save;int state=career_load(SAVES,&save);
    if(state<0) {
        notice("No se pudo leer el guardado. Se conservan los archivos.");return;
    }
    if(action==3) {
        if(state==0) {
            const char *lines[]={"NUEVA CARRERA","Ya existe una carrera guardada.",
              "Crear otra sustituye la carrera actual.","X  Crear otra     O  Cancelar"};
            if(!panel(lines,4,1))return;
        }
        char alias[32];int result=name_input(alias);
        if(result==0)return;
        if(result<0) {
            notice(result==-2?"Usa de 1 a 31 letras sin tildes, numeros o signos.":
              "No se pudo abrir el teclado. Intenta de nuevo.");return;
        }
        if(career_save(SAVES,alias,&save)<0) {
            notice("No se pudo guardar. Comprueba el espacio disponible.");return;
        }
        platform_log("Career profile saved; launching opening prologue");
    } else if(state==1) {
        notice("No hay una carrera guardada. Elige Nueva carrera.");return;
    } else platform_log("Career profile loaded and validated");
    /* Continue directly to the available saved milestone; no diagnostic confirmation panel. */
    int event_choice=action==6?0:action==7?select_scene(&save):action==3?1:save.ronnie_finishes?3:save.first_finishes?2:1;
    if(event_choice<0)return;
    for(;;){
      if(event_choice){
        splash_audio_stop();int video=video_play(event_choice==4?"ux0:data/nfsmw/race/razor/intro.mp4":event_choice==3?"ux0:data/nfsmw/race/bull/intro.mp4":event_choice==2?"ux0:data/nfsmw/race/ronnie/intro.mp4":"ux0:data/nfsmw/race/intro.mp4");menu_audio_start();
        if(video<0){notice("Falta el video del prologo o no se pudo reproducir.");return;}
      }
      int result=drive_run(event_choice);renderer_loading_end();
      if(result<0){notice("No se pudo cargar. Revisa world y race con sus recursos.");return;}
      if((event_choice==1&&result==2)||(event_choice==2&&result==3)){event_choice=result;continue;}
      if(event_choice==4&&result==5){splash_audio_stop();int video=video_play("ux0:data/nfsmw/race/razor/outro.mp4");menu_audio_start();if(video<0)notice("No se pudo reproducir la escena tras la averia.");}
      return;
    }
}
