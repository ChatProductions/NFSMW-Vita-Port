#include "asset_store.h"
#include "platform.h"
#include "renderer.h"
#include "frontend_scene.h"
#include "video_player.h"
#include "splash_audio.h"
#include "menu_scene.h"
#include "settings.h"
#include "quick_race.h"
#include "career_ui.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
static int intro(void) {
    const char *paths[]={"ux0:data/nfsmw/media/ea-logo.mp4",
                         "ux0:data/nfsmw/media/psa.mp4",
                         "ux0:data/nfsmw/media/intro.mp4"};
    int result=0;
    for (unsigned i=0;i<3;i++) {
        renderer_video_wait();
        result=video_play(paths[i]);
        /* Skip applies to this movie. Missing media does not block later clips. */
    }
    return result;
}
static void status(unsigned presses, int result) {
    unsigned char header[16]; size_t count;
    AssetResult r = asset_read(NFSMW_ASSET_ROOT,"GLOBAL/GLOBALB.LZC",0,header,sizeof(header),&count);
    renderer_status(r, presses, result);
}
int main(void) {
    if (platform_init() < 0) { platform_exit(); return 1; }
    if (renderer_init() < 0) { platform_log("DISPLAY INIT FAILED"); platform_exit(); return 1; }
    platform_log("NFSMW-Vita 00.43 staged streaming / wall sliding / camera obstruction / rival recovery started");
    GameSettings options;settings_load(SETTINGS_ROOT,&options);settings_apply_audio(&options);
    /* Holding Circle on launch skips playback for recovery. */
    unsigned presses = 0;
    int result = 2;
    if (!(platform_buttons() & SCE_CTRL_CIRCLE)) result = intro();
    int menu = 0;
    int splash = frontend_load() == 0;
    if (splash) splash_audio_start();
    if (splash) frontend_draw(); else status(presses, result);
    uint64_t idle_since=sceKernelGetProcessTimeWide();
    uint32_t previous = platform_buttons();
    for (;;) {
        uint32_t buttons = platform_buttons(), pressed = buttons & ~previous;
        if (menu) {
            int action=menu_input(pressed);
            if(action>=10&&action<=16){settings_ui_category(action-10);buttons=platform_buttons();}
            else if(action==20||action==21){quick_race_ui(action==21);buttons=platform_buttons();}
            else if(action==5){settings_ui_run();buttons=platform_buttons();}
            else if(action>=2) { career_ui_run(action); buttons=platform_buttons(); }
            if (action==1) {
                splash_audio_stop();
                menu_unload(); menu=0;
                splash=frontend_load()==0;
                if (splash) splash_audio_start(); else status(presses,result);
                idle_since=sceKernelGetProcessTimeWide();
            }
            if (menu) menu_draw(); else if (splash) frontend_draw();
            previous=buttons;platform_wait_frame();continue;
        }
        if (splash) {
            if (buttons) idle_since=sceKernelGetProcessTimeWide();
            if (pressed & SCE_CTRL_START) {
                splash=0;
                splash_audio_stop();
                frontend_unload();
                menu=menu_load()==0;
                if (menu) { menu_audio_start(); menu_draw(); } else status(presses,result);
            }
            if (splash && sceKernelGetProcessTimeWide()-idle_since>=30000000ULL) {
                platform_log("Attract: 30 seconds idle");
                splash_audio_stop();frontend_unload();
                result=video_play("ux0:data/nfsmw/media/intro.mp4");
                splash=frontend_load()==0;
                if (splash) splash_audio_start(); else status(presses,result);
                idle_since=sceKernelGetProcessTimeWide();
                buttons=platform_buttons();
            }
            if (splash) frontend_draw();
            previous=buttons;platform_wait_frame();continue;
        }
        if (pressed & SCE_CTRL_START) break;
        if (pressed & SCE_CTRL_CROSS) {
            ++presses;
            platform_log("CROSS detected: replay requested");
            result = intro();
            splash=frontend_load()==0;
            if (splash) splash_audio_start();
            if (splash) frontend_draw(); else status(presses, result);
            idle_since=sceKernelGetProcessTimeWide();
            buttons = platform_buttons();
        }
        previous = buttons;
        platform_wait_frame();
    }
    platform_log("Clean exit"); splash_audio_stop(); frontend_unload(); menu_unload(); renderer_shutdown(); platform_exit(); return 0;
}
