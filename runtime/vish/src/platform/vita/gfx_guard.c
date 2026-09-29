/* libvita2d's void drawing API ignores failed GXM uniform reservations.
   Interpose the imported calls so an unset buffer is never dereferenced. */
#include "gfx_guard.h"
#include "platform.h"
#include <psp2/gxm.h>
#include <stdio.h>
static int active,frame_error,last_error;
static unsigned reports;
static void *vertex_buffer,*fragment_buffer;
void gfx_guard_reset(void){last_error=0;}
int gfx_guard_error(void){return last_error;}
static int fail(const char*where,int rc){
 if(!frame_error&&reports<12){char msg[160];snprintf(msg,sizeof(msg),"GXM guarded failure: %s rc=0x%08x active=%d",where,(unsigned)rc,active);platform_log(msg);reports++;}
 frame_error=last_error=rc<0?rc:(int)SCE_GXM_ERROR_INVALID_POINTER;return frame_error;
}
int __real_sceGxmBeginScene(SceGxmContext*,unsigned,const SceGxmRenderTarget*,const SceGxmValidRegion*,SceGxmSyncObject*,SceGxmSyncObject*,const SceGxmColorSurface*,const SceGxmDepthStencilSurface*);
int __wrap_sceGxmBeginScene(SceGxmContext*c,unsigned flags,const SceGxmRenderTarget*t,const SceGxmValidRegion*r,SceGxmSyncObject*v,SceGxmSyncObject*f,const SceGxmColorSurface*s,const SceGxmDepthStencilSurface*d){
 frame_error=0;vertex_buffer=fragment_buffer=NULL;
 int rc=__real_sceGxmBeginScene(c,flags,t,r,v,f,s,d);active=rc>=0;
 return rc<0?fail("BeginScene",rc):rc;
}
int __real_sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext*,void**);
int __real_sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext*,void**);
static int reserve(SceGxmContext*c,void**out,int vertex){
 if(!out)return fail("uniform output",SCE_GXM_ERROR_INVALID_POINTER);
 *out=NULL;void**known=vertex?&vertex_buffer:&fragment_buffer;*known=NULL;
 if(!active||frame_error)return fail("uniform outside valid scene",frame_error?frame_error:(int)SCE_GXM_ERROR_INVALID_POINTER);
 int rc=vertex?__real_sceGxmReserveVertexDefaultUniformBuffer(c,out):__real_sceGxmReserveFragmentDefaultUniformBuffer(c,out);
 if(rc<0||!*out){*out=NULL;return fail(vertex?"ReserveVertexUniform":"ReserveFragmentUniform",rc);}
 *known=*out;return rc;
}
int __wrap_sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext*c,void**out){return reserve(c,out,1);}
int __wrap_sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext*c,void**out){return reserve(c,out,0);}
int __real_sceGxmSetUniformDataF(void*,const SceGxmProgramParameter*,unsigned,unsigned,const float*);
int __wrap_sceGxmSetUniformDataF(void*b,const SceGxmProgramParameter*p,unsigned offset,unsigned count,const float*data){
 if(frame_error||!active||!b||!p||!data||(b!=vertex_buffer&&b!=fragment_buffer))return fail("invalid uniform upload",frame_error?frame_error:(int)SCE_GXM_ERROR_INVALID_POINTER);
 int rc=__real_sceGxmSetUniformDataF(b,p,offset,count,data);return rc<0?fail("SetUniformDataF",rc):rc;
}
int __real_sceGxmDraw(SceGxmContext*,SceGxmPrimitiveType,SceGxmIndexFormat,const void*,unsigned);
int __wrap_sceGxmDraw(SceGxmContext*c,SceGxmPrimitiveType p,SceGxmIndexFormat f,const void*i,unsigned n){
 if(frame_error||!active)return frame_error?frame_error:(int)SCE_GXM_ERROR_INVALID_POINTER;
 int rc=__real_sceGxmDraw(c,p,f,i,n);return rc<0?fail("Draw",rc):rc;
}
int __real_sceGxmEndScene(SceGxmContext*,const SceGxmNotification*,const SceGxmNotification*);
int __wrap_sceGxmEndScene(SceGxmContext*c,const SceGxmNotification*v,const SceGxmNotification*f){
 if(!active)return frame_error?frame_error:(int)SCE_GXM_ERROR_INVALID_POINTER;
 int rc=__real_sceGxmEndScene(c,v,f);active=0;vertex_buffer=fragment_buffer=NULL;return rc<0?fail("EndScene",rc):rc;
}
