#include "drive_audio.h"
#include "platform.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#define BANDS 8
#define FRAMES 4800
static SceUID thread=-1;
static atomic_int running,rpm_value,throttle_value,paused_value,engine_value,command,call_done;
#define RADIO_ROOT "ux0:data/nfsmw/radio/"
static atomic_int music_volume=100,engine_volume=100,voice_volume=100;
void drive_audio_volume(unsigned m,unsigned e,unsigned v){atomic_store(&music_volume,m>100?100:m);atomic_store(&engine_volume,e>100?100:e);atomic_store(&voice_volume,v>100?100:v);}
static const char *radio_root=RADIO_ROOT;
#define RADIO_MAX 32
static char radio_names[RADIO_MAX][160];static unsigned radio_count;
static atomic_int radio_command,radio_index,radio_on;
static float reference_rpm[BANDS];
static int16_t *engine,*voice;
static unsigned voice_frames;
static int16_t buffers[2][2048];
static int audio_main(SceSize size,void*arg){
 (void)size;(void)arg;unsigned voice_pos=0,next=0;int speaking=0;float gain=0,filtered=0,smooth_rpm=1200,phases[BANDS]={0};
 FILE*music=NULL;int track=-1;int16_t radio_pcm[2048];
 int port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,1024,48000,SCE_AUDIO_OUT_MODE_STEREO);
 if(port<0){platform_log("Drive audio port unavailable");atomic_store(&call_done,1);return 0;}
 while(atomic_load(&running)){
  int cmd=atomic_exchange(&command,0);if(cmd){voice_pos=0;speaking=cmd==1&&voice_frames;atomic_store(&call_done,!speaking);}
  int paused=atomic_load(&paused_value);
  smooth_rpm+=(atomic_load(&rpm_value)-smooth_rpm)*.18f;
  unsigned b=0;while(b+1<BANDS&&smooth_rpm>reference_rpm[b+1])b++;unsigned b2=b+1<BANDS?b+1:b;
  float blend=b2==b?0:fmaxf(0,fminf(1,(smooth_rpm-reference_rpm[b])/(reference_rpm[b2]-reference_rpm[b])));
  int advance=atomic_exchange(&radio_command,0);memset(radio_pcm,0,sizeof(radio_pcm));
  if(radio_count&&(track<0||advance)){
   if(music)fclose(music);
   track=track<0?0:(track+advance%(int)radio_count+(int)radio_count)%(int)radio_count;
   char file[256];snprintf(file,sizeof(file),"%s%02d.pcm",radio_root,track);music=fopen(file,"rb");atomic_store(&radio_index,track);if(!music)atomic_fetch_add(&radio_command,1);
  }
  if(music&&!paused&&atomic_load(&radio_on)){
   size_t got=fread(radio_pcm,4,1024,music);
   if(got<1024){if(ferror(music))platform_log("Radio read failed; advancing track");atomic_fetch_add(&radio_command,1);}
  }
  float mv=atomic_load(&music_volume)/100.f,ev=atomic_load(&engine_volume)/100.f,vv=atomic_load(&voice_volume)/100.f;
  float target=atomic_load(&engine_value)?(.18f+.18f*atomic_load(&throttle_value)/1000):0;
  for(unsigned i=0;i<1024;i++){
   float sample=0;
   if(!paused){
    gain+=(target-gain)*.001f;
    if(engine){
     float pair[2];unsigned bands[2]={b,b2};
     for(int j=0;j<2;j++){unsigned k=bands[j],frame=(unsigned)phases[k],after=(frame+1)%FRAMES;float frac=phases[k]-frame;pair[j]=engine[k*FRAMES+frame]*(1-frac)+engine[k*FRAMES+after]*frac;}
     float raw=(pair[0]*(1-blend)+pair[1]*blend)*gain;filtered+=(raw-filtered)*.24f;sample=filtered*(speaking?.25f:1.f)*ev;
    }
    for(int j=0;j<BANDS;j++){phases[j]+=fmaxf(.25f,fminf(1.f,smooth_rpm/reference_rpm[j]*.5f));if(phases[j]>=FRAMES)phases[j]-=FRAMES;}
    if(speaking){sample+=voice[voice_pos++]*.9f*vv;if(voice_pos>=voice_frames){speaking=0;atomic_store(&call_done,1);}}
   }
   float radio_gain=speaking?.08f:.24f;
   for(int ch=0;ch<2;ch++)buffers[next][i*2+ch]=(int16_t)fmaxf(-32768,fminf(32767,sample+radio_pcm[i*2+ch]*radio_gain*mv));
  }
  if(sceAudioOutOutput(port,buffers[next])<0)break;
  next^=1;
 }
 if(music)fclose(music);
 sceAudioOutOutput(port,NULL);sceAudioOutReleasePort(port);atomic_store(&call_done,1);return 0;
}
void drive_audio_stop(void){
 atomic_store(&running,0);if(thread>=0){sceKernelWaitThreadEnd(thread,NULL,NULL);sceKernelDeleteThread(thread);thread=-1;}
 free(engine);free(voice);engine=voice=NULL;voice_frames=0;atomic_store(&call_done,1);
}
int drive_audio_start(const char*root){
 drive_audio_stop();char path[320];snprintf(path,sizeof(path),"%s/engine.nfe",root);FILE*f=fopen(path,"rb");
 for(int i=0;i<BANDS;i++)reference_rpm[i]=1200+i*7300.f/7;
 if(f){unsigned h[5];if(fread(h,4,5,f)==5&&!memcmp(h,"NFEN",4)&&(h[1]==1||h[1]==2)&&h[2]==BANDS&&h[3]==FRAMES&&h[4]==24000){int valid=1;if(h[1]==2){valid=fread(reference_rpm,4,BANDS,f)==BANDS;for(int i=0;i<BANDS;i++)if(!isfinite(reference_rpm[i])||reference_rpm[i]<500||reference_rpm[i]>12000||(i&&reference_rpm[i]<=reference_rpm[i-1]))valid=0;}engine=valid?malloc(BANDS*FRAMES*2):NULL;if(engine&&(fread(engine,2,BANDS*FRAMES,f)!=BANDS*FRAMES||fgetc(f)!=EOF)){free(engine);engine=NULL;}}fclose(f);}
 snprintf(path,sizeof(path),"%s/mia.pcm",root);f=fopen(path,"rb");
 if(f){if(fseek(f,0,SEEK_END)==0){long n=ftell(f);if(n>=2&&n<=48000*2*60&&n%2==0&&fseek(f,0,SEEK_SET)==0){voice=malloc((size_t)n);if(voice&&fread(voice,1,n,f)==(size_t)n)voice_frames=(unsigned)n/2;else{free(voice);voice=NULL;}}}fclose(f);}
 if(!engine)for(int i=0;i<BANDS;i++)reference_rpm[i]=1200+i*7300.f/7;
 radio_count=0;snprintf(path,sizeof(path),"%splaylist.txt",radio_root);f=fopen(path,"r");if(f){while(radio_count<RADIO_MAX&&fgets(radio_names[radio_count],160,f)){radio_names[radio_count][strcspn(radio_names[radio_count],"\r\n")]=0;if(radio_names[radio_count][0])radio_count++;}fclose(f);}
 atomic_store(&radio_index,0);atomic_store(&radio_on,1);atomic_store(&radio_command,0);
 if(!engine&&!voice&&!radio_count)return -1;
 atomic_store(&rpm_value,1200);atomic_store(&throttle_value,0);atomic_store(&paused_value,0);atomic_store(&engine_value,1);atomic_store(&command,0);atomic_store(&call_done,1);
 thread=sceKernelCreateThread("nfsmw-drive-audio",audio_main,0x10000100-10,0x10000,0,0,NULL);
 if(thread<0){drive_audio_stop();return -1;}atomic_store(&running,1);
 if(sceKernelStartThread(thread,0,NULL)<0){sceKernelDeleteThread(thread);thread=-1;drive_audio_stop();return -1;}
 platform_log("Drive engine and Spanish prologue speech loaded");return 0;
}
void drive_audio_update(float rpm,float throttle,int paused,int engine_on){
 atomic_store(&rpm_value,(int)fmaxf(0,fminf(10000,isfinite(rpm)?rpm:1200)));atomic_store(&throttle_value,(int)(fmaxf(0,fminf(1,isfinite(throttle)?throttle:0))*1000));atomic_store(&paused_value,!!paused);atomic_store(&engine_value,!!engine_on);
}
void drive_audio_call(void){atomic_store(&call_done,0);atomic_store(&command,1);}
void drive_audio_reset_call(void){atomic_store(&command,2);atomic_store(&call_done,1);}
int drive_audio_call_done(void){return atomic_load(&call_done);}

void drive_radio_change(int direction){if(radio_count)atomic_fetch_add(&radio_command,direction>0?1:-1);}
void drive_radio_toggle(void){atomic_store(&radio_on,!atomic_load(&radio_on));}
const char*drive_radio_label(void){return radio_count?radio_names[atomic_load(&radio_index)]:"Sin canciones";}
int drive_radio_enabled(void){return radio_count&&atomic_load(&radio_on);}
