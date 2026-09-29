#include "splash_audio.h"
#include "platform.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

/* Private, prepared s16le stereo 48 kHz data. Never packaged in the VPK.
   Two buffers keep the last submitted block intact while filling the next. */
static SceUID thread = -1;
static atomic_int running;
static atomic_int volume=100;
void splash_audio_volume(unsigned v){atomic_store(&volume,v>100?100:v);}
static FILE *source;
static int16_t buffers[2][2048];

static int audio_main(SceSize size, void *arg) {
    (void)size; (void)arg;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024,
                                   48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { platform_log("Music audio port failed"); return 0; }
    unsigned next = 0;
    while (atomic_load(&running)) {
        unsigned char *buf = (unsigned char *)buffers[next];
        size_t filled = 0;
        while (filled < sizeof(buffers[0]) && atomic_load(&running)) {
            size_t n = fread(buf + filled, 1, sizeof(buffers[0]) - filled, source);
            filled += n;
            if (ferror(source)) { platform_log("Music audio read failed"); goto done; }
            if (feof(source)) {
                if (fseek(source, 0, SEEK_SET) != 0) goto done;
                /* An empty/removed source must not spin indefinitely. */
                if (!n) {
                    int c = fgetc(source);
                    if (c == EOF || ungetc(c, source) == EOF) goto done;
                }
            }
        }
        if (!atomic_load(&running)) break;
        int vol=atomic_load(&volume);for(unsigned i=0;i<2048;i++)buffers[next][i]=(int16_t)(buffers[next][i]*vol/100);
        if (sceAudioOutOutput(port, buf) < 0) {
            platform_log("Music audio output failed"); break;
        }
        next ^= 1;
    }
done:
    sceAudioOutOutput(port, NULL);
    sceAudioOutReleasePort(port);
    return 0;
}

void splash_audio_stop(void) {
    atomic_store(&running, 0);
    if (thread >= 0) {
        sceKernelWaitThreadEnd(thread, NULL, NULL);
        sceKernelDeleteThread(thread);
        thread = -1;
    }
    if (source) { fclose(source); source = NULL; }
}

static int music_start(const char *path) {
    splash_audio_stop();
    source = fopen(path, "rb");
    platform_log(path);
    if (!source) { platform_log("Music missing; continuing silently"); return -1; }
    if (fseek(source, 0, SEEK_END) != 0) goto fail;
    long bytes = ftell(source);
    if (bytes < 4 || bytes > 64 * 1024 * 1024 || bytes % 4 ||
        fseek(source, 0, SEEK_SET) != 0) goto fail;
    thread = sceKernelCreateThread("nfsmw-splash-audio", audio_main,
                                   0x10000100 - 10, 0x10000, 0, 0, NULL);
    if (thread < 0) goto fail;
    atomic_store(&running, 1);
    if (sceKernelStartThread(thread, 0, NULL) < 0) {
        sceKernelDeleteThread(thread); thread = -1; goto fail;
    }
    platform_log("Music loop started (48 kHz stereo PCM)");
    return 0;
fail:
    platform_log("Music initialization failed");
    splash_audio_stop();
    return -1;
}

int splash_audio_start(void) { return music_start("ux0:data/nfsmw/media/splash.pcm"); }
int menu_audio_start(void) { return music_start("ux0:data/nfsmw/media/menu.pcm"); }
