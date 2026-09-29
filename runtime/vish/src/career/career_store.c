#include "career_store.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#define BYTES 80
static uint32_t get32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char*p,uint32_t n){for(int i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static uint32_t crc(const unsigned char*p,size_t n){
 uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;
}
static int valid_alias(const char*s){
 size_t n=0;for(;n<32&&s[n];n++)if((unsigned char)s[n]<32||(unsigned char)s[n]>126)return 0;
 return n>0&&n<32;
}
static int path_for(char*out,size_t n,const char*dir,unsigned slot){
 int r=snprintf(out,n,"%s/career-%u.sav",dir,slot);return r>0&&(size_t)r<n;
}
static int read_slot(const char*dir,unsigned slot,CareerSave*s){
 char p[512];if(!path_for(p,sizeof(p),dir,slot))return -1;
 FILE*f=fopen(p,"rb");if(!f)return errno==ENOENT?1:-1;
 unsigned char b[BYTES]={0};size_t bytes=fread(b,1,BYTES,f);int ok=(bytes==64||bytes==80)&&fgetc(f)==EOF;fclose(f);
 unsigned version=get32(b+4),crc_offset=version==3?76:60;
 if((version==3&&bytes!=80)||(version!=3&&bytes!=64))return -1;
 /* Stage 1 is only a prepared new career. No simulated race completion. */
 if(!ok||memcmp(b,"NFCS",4)||(version!=1&&version!=2&&version!=3)||get32(b+8)==0||get32(b+12)!=1||
    get32(b+48)!=16||(get32(b+4)==1&&(get32(b+52)!=0||get32(b+56)!=0))||get32(b+52)>3600000||((get32(b+52)==0)!=(get32(b+56)==0))||get32(b+crc_offset)!=crc(b,crc_offset)||
    !memchr(b+16,0,32)||!valid_alias((const char*)b+16))return -1;
 if(version==3&&(get32(b+60)!=0||get32(b+72)!=0||get32(b+64)>3600000||((get32(b+64)==0)!=(get32(b+68)==0))))return -1;
 s->ronnie_best_ms=version==3?get32(b+64):0;s->ronnie_finishes=version==3?get32(b+68):0;
 s->first_best_ms=get32(b+52);s->first_finishes=get32(b+56);s->sequence=get32(b+8);memcpy(s->alias,b+16,32);return 0;
}
static int newest(const char*dir,CareerSave*s,int*slot){
 CareerSave a={0},b={0};int ra=read_slot(dir,0,&a),rb=read_slot(dir,1,&b);
 if(ra&&rb)return ra==1&&rb==1?1:-1;
 if(!ra&&(rb||a.sequence>=b.sequence)){*s=a;*slot=0;}else{*s=b;*slot=1;}return 0;
}
int career_load(const char*dir,CareerSave*s){int slot=0;memset(s,0,sizeof(*s));return newest(dir,s,&slot);}
static int write_save(const char*dir,const char*alias,uint32_t best,uint32_t finishes,uint32_t ronnie_best,uint32_t ronnie_finishes,CareerSave*out){
 if(!valid_alias(alias))return -1;
 if(mkdir(dir,0777)<0&&errno!=EEXIST)return -1;
 CareerSave old={0};int slot=1,found=newest(dir,&old,&slot);
 /* Preserve unrecognized/corrupt saves for diagnosis, never overwrite both. */
 if(found<0||old.sequence==UINT32_MAX)return -1;
 unsigned target=(unsigned)(1-slot);unsigned char b[BYTES]={0};
 memcpy(b,"NFCS",4);put32(b+4,3);put32(b+8,old.sequence+1);put32(b+12,1);
 memcpy(b+16,alias,strlen(alias));put32(b+48,16);put32(b+52,best);put32(b+56,finishes);put32(b+64,ronnie_best);put32(b+68,ronnie_finishes);put32(b+76,crc(b,76));
 char path[512];if(!path_for(path,sizeof(path),dir,target))return -1;
 FILE*f=fopen(path,"wb");if(!f)return -1;
 int ok=fwrite(b,1,BYTES,f)==BYTES;
 if(ok&&fflush(f)!=0)ok=0;
 if(ok&&fsync(fileno(f))!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 CareerSave check={0};
 if(!ok||read_slot(dir,target,&check)||check.sequence!=old.sequence+1||strcmp(check.alias,alias))return -1;
 *out=check;return 0;
}
int career_save(const char*dir,const char*alias,CareerSave*out){return write_save(dir,alias,0,0,0,0,out);}
int career_record_first(const char*dir,uint32_t ms){
 CareerSave old={0},result={0};if(!ms||ms>3600000||career_load(dir,&old)!=0||old.first_finishes==UINT32_MAX)return -1;
 uint32_t best=old.first_best_ms&&old.first_best_ms<ms?old.first_best_ms:ms;
 return write_save(dir,old.alias,best,old.first_finishes+1,old.ronnie_best_ms,old.ronnie_finishes,&result);
}
int career_record_ronnie(const char*dir,uint32_t ms){
 CareerSave old={0},result={0};if(!ms||ms>3600000||career_load(dir,&old)!=0||!old.first_finishes||old.ronnie_finishes==UINT32_MAX)return -1;
 uint32_t best=old.ronnie_best_ms&&old.ronnie_best_ms<ms?old.ronnie_best_ms:ms;
 return write_save(dir,old.alias,old.first_best_ms,old.first_finishes,best,old.ronnie_finishes+1,&result);
}
const char *career_first_event(void){return "16.1.0";}
const char *career_start_car(void){return "M3GTRCAREERSTART";}
