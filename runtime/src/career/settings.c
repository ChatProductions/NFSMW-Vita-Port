#include "settings.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
static const unsigned defaults[]={0,1,0,0,1,15,100,0,100,100,100};
void settings_defaults(GameSettings*s){memcpy(s->value,defaults,sizeof(defaults));}
int settings_valid(const GameSettings*s){return s->value[0]<3&&s->value[1]<2&&s->value[2]<2&&s->value[3]<2&&s->value[4]<3&&s->value[5]>=5&&s->value[5]<=30&&s->value[6]>=50&&s->value[6]<=150&&s->value[7]<2&&s->value[8]<=100&&s->value[9]<=100&&s->value[10]<=100;}
static uint32_t hash(const uint32_t*p){uint32_t h=2166136261u;for(unsigned i=0;i<14;i++)h=(h^p[i])*16777619u;return h;}
static int readslot(const char*root,unsigned slot,GameSettings*s,uint32_t*seq){char path[320];if(snprintf(path,sizeof(path),"%s/settings.%u",root,slot)>=(int)sizeof(path))return 0;FILE*f=fopen(path,"rb");if(!f)return 0;uint32_t b[15];int ok=fread(b,sizeof(b),1,f)==1&&fgetc(f)==EOF;fclose(f);if(!ok||b[0]!=0x5346574e||b[1]!=1||b[14]!=hash(b))return 0;memcpy(s->value,b+3,sizeof(s->value));*seq=b[2];return settings_valid(s);}
int settings_load(const char*root,GameSettings*s){GameSettings a[2];uint32_t seq[2]={0};int ok[2]={readslot(root,0,&a[0],&seq[0]),readslot(root,1,&a[1],&seq[1])};if(!ok[0]&&!ok[1]){settings_defaults(s);return 1;}int n=ok[1]&&(!ok[0]||(int32_t)(seq[1]-seq[0])>0);*s=a[n];return 0;}
int settings_save(const char*root,const GameSettings*s){if(!settings_valid(s))return -1;if(mkdir(root,0777)<0&&errno!=EEXIST)return -1;GameSettings a[2];uint32_t seq[2]={0};int ok[2]={readslot(root,0,&a[0],&seq[0]),readslot(root,1,&a[1],&seq[1])};int newest=ok[1]&&(!ok[0]||(int32_t)(seq[1]-seq[0])>0),slot=ok[newest]?1-newest:0;uint32_t b[15]={0x5346574e,1,ok[newest]?seq[newest]+1:1};memcpy(b+3,s->value,sizeof(s->value));b[14]=hash(b);char path[320];if(snprintf(path,sizeof(path),"%s/settings.%d",root,slot)>=(int)sizeof(path))return -1;FILE*f=fopen(path,"wb");if(!f)return -1;int good=fwrite(b,sizeof(b),1,f)==1;if(fclose(f))good=0;GameSettings check;uint32_t n;return good&&readslot(root,slot,&check,&n)&&n==b[2]?0:-1;}
