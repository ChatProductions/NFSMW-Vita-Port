#include "platform.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <psp2/kernel/sysmem.h>
int platform_init(void) {
    sceIoMkdir("ux0:data/nfsmw", 0777);
    sceIoMkdir("ux0:data/nfsmw/logs", 0777);
    sceIoMkdir("ux0:data/nfsmw/original", 0777);
    return sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
}
uint32_t platform_buttons(void) {
    SceCtrlData pad = {0};
    return sceCtrlPeekBufferPositive(0, &pad, 1) > 0 ? pad.buttons : 0;
}
void platform_wait_frame(void) { sceDisplayWaitVblankStart(); }
void platform_log(const char *message) {
    FILE *f = fopen("ux0:data/nfsmw/logs/boot.log", "a");
    if (f) { fprintf(f, "%s\n", message); fclose(f); }
}
void platform_exit(void) { sceKernelExitProcess(0); }

void platform_log_memory(const char *stage) {
    SceKernelFreeMemorySizeInfo info={0};info.size=sizeof(info);
    int result=sceKernelGetFreeMemorySize(&info);char line[200];
    if(result<0)snprintf(line,sizeof(line),"Memory %s unavailable: 0x%08x",stage,(unsigned)result);
    else snprintf(line,sizeof(line),"Memory %s free bytes: user=%d cdram=%d phycont=%d; map cache=16MiB sectors=96MiB",stage,info.size_user,info.size_cdram,info.size_phycont);
    platform_log(line);
}