#include "world_textures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define LIMIT 4096
#define CACHE 256
#define CACHE_BYTES (16u*1024u*1024u)
#define LOADS_PER_FRAME 8
static FILE*pack;
static struct {unsigned offset,size,width,height,opaque;} entries[LIMIT+1];
static vita2d_texture*images[LIMIT+1];
static unsigned char wanted[LIMIT+1],failed[LIMIT+1],event_barriers[LIMIT+1];
static float priority[LIMIT+1];
static unsigned count,resident,version,resident_bytes;
void world_textures_close(void){for(unsigned i=1;i<=count;i++){if(images[i])vita2d_free_texture(images[i]);images[i]=NULL;}if(pack)fclose(pack);pack=NULL;count=resident=version=resident_bytes=0;memset(failed,0,sizeof(failed));}
static int dimension(unsigned n){return n>=8&&n<=256&&(n&(n-1))==0;}
int world_textures_open(const char*root){
 world_textures_close();memset(event_barriers,0,sizeof(event_barriers));char path[320];snprintf(path,sizeof(path),"%s/textures.nfi",root);FILE*f=fopen(path,"rb");if(!f)return -1;
 unsigned h[3];int ok=fread(h,4,3,f)==3&&!memcmp(h,"NFTP",4)&&(h[1]==1||h[1]==2)&&h[2]>0&&h[2]<=LIMIT;
 if(ok){version=h[1];count=h[2];for(unsigned i=1;i<=count&&ok;i++){memset(&entries[i],0,sizeof(entries[i]));ok=fread(&entries[i],4,version==2?5:2,f)==(version==2?5u:2u);if(version==1)entries[i].width=entries[i].height=64;}if(ok)ok=fgetc(f)==EOF;}fclose(f);
 unsigned total=0;
 if(ok)for(unsigned i=1;i<=count;i++){if(entries[i].offset!=total||entries[i].size<33||entries[i].size>262144||entries[i].opaque>1||!dimension(entries[i].width)||!dimension(entries[i].height)||(version==2&&entries[i].size!=entries[i].width*entries[i].height*4)){ok=0;break;}total+=entries[i].size;}
 if(ok){snprintf(path,sizeof(path),"%s/textures.nft",root);pack=fopen(path,"rb");ok=pack&&fseek(pack,0,SEEK_END)==0&&ftell(pack)==(long)total;}
 if(!ok){world_textures_close();return -1;}
 snprintf(path,sizeof(path),"%s/event-barriers.bin",root);f=fopen(path,"rb");
 if(f){unsigned h[3];int valid=fread(h,4,3,f)==3&&!memcmp(h,"NFEB",4)&&h[1]==1&&h[2]<=count;for(unsigned i=0;valid&&i<h[2];i++){unsigned id;valid=fread(&id,4,1,f)==1&&id>0&&id<=count;if(valid)event_barriers[id]=1;}if(!valid||fgetc(f)!=EOF)memset(event_barriers,0,sizeof(event_barriers));fclose(f);}
 /* Separate compatibility mask for mispositioned panorama surfaces in existing packs. */
 snprintf(path,sizeof(path),"%s/hidden-scenery.bin",root);f=fopen(path,"rb");
 if(f){unsigned h[3],ids[LIMIT];int valid=fread(h,4,3,f)==3&&!memcmp(h,"NFHS",4)&&h[1]==1&&h[2]<=count;for(unsigned i=0;valid&&i<h[2];i++)valid=fread(&ids[i],4,1,f)==1&&ids[i]>0&&ids[i]<=count;if(valid&&fgetc(f)==EOF)for(unsigned i=0;i<h[2];i++)event_barriers[ids[i]]=1;fclose(f);}
 return 0;
}
void world_textures_begin(void){memset(wanted,0,sizeof(wanted));for(unsigned i=1;i<=count;i++)priority[i]=1e30f;}
void world_textures_request(unsigned id,float distance){if(id>0&&id<=count&&isfinite(distance)){wanted[id]=1;if(distance<priority[id])priority[id]=distance;}}
vita2d_texture*world_textures_get(unsigned id){return id>0&&id<=count?images[id]:NULL;}
int world_textures_opaque(unsigned id){return id>0&&id<=count&&images[id]&&entries[id].opaque;}
void world_textures_prepare_budget(unsigned budget){
 if(budget>LOADS_PER_FRAME)budget=LOADS_PER_FRAME;
 if(!pack)return;
 for(unsigned loads=0;loads<budget;loads++){
  unsigned i=0;for(unsigned j=1;j<=count;j++)if(wanted[j]&&!images[j]&&!failed[j]&&(!i||priority[j]<priority[i]))i=j;if(!i)break;
  unsigned needed=entries[i].width*entries[i].height*4;
  while(resident>=CACHE||resident_bytes+needed>CACHE_BYTES){unsigned victim=0;for(unsigned j=1;j<=count;j++)if(images[j]&&!wanted[j]){victim=j;break;}if(!victim){for(unsigned j=1;j<=count;j++)if(images[j]&&priority[j]>priority[i]+20&&(!victim||priority[j]>priority[victim]))victim=j;}
   if(!victim)break;
   resident_bytes-=entries[victim].width*entries[victim].height*4;vita2d_free_texture(images[victim]);images[victim]=NULL;resident--;}
  if(resident>=CACHE||resident_bytes+needed>CACHE_BYTES)break;
  unsigned size=entries[i].size;int ok=fseek(pack,entries[i].offset,SEEK_SET)==0;
  if(version==2){
   images[i]=vita2d_create_empty_texture(entries[i].width,entries[i].height);
   if(!images[i])break;
   unsigned stride=vita2d_texture_get_stride(images[i]);unsigned char*data=vita2d_texture_get_datap(images[i]);
   ok=ok&&data&&stride>=entries[i].width*4;
   if(ok&&stride==entries[i].width*4)ok=fread(data,1,size,pack)==size;
   else for(unsigned y=0;y<entries[i].height&&ok;y++)ok=fread(data+y*stride,1,entries[i].width*4,pack)==entries[i].width*4;
   if(!ok){vita2d_free_texture(images[i]);images[i]=NULL;}
  }else{
   unsigned char*data=malloc(size);if(!data)break;ok=ok&&fread(data,1,size,pack)==size;
   static const unsigned char signature[8]={137,80,78,71,13,10,26,10};
   if(ok)ok=!memcmp(data,signature,8)&&!memcmp(data+12,"IHDR",4)&&data[16]==0&&data[17]==0&&data[18]==0&&data[19]==64&&data[20]==0&&data[21]==0&&data[22]==0&&data[23]==64;
   if(ok)images[i]=vita2d_load_PNG_buffer(data);
   free(data);
  }
  if(!images[i]){failed[i]=1;continue;}
  sceGxmTextureSetUAddrMode(&images[i]->gxm_tex,SCE_GXM_TEXTURE_ADDR_REPEAT);sceGxmTextureSetVAddrMode(&images[i]->gxm_tex,SCE_GXM_TEXTURE_ADDR_REPEAT);
  vita2d_texture_set_filters(images[i],SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);resident++;resident_bytes+=needed;
 }
}

void world_textures_prepare(void){world_textures_prepare_budget(LOADS_PER_FRAME);}

int world_textures_event_barrier(unsigned id){return id>0&&id<=count&&event_barriers[id];}

int world_textures_collidable(unsigned id){return id==0||(id<=count&&entries[id].opaque&&!event_barriers[id]);}
