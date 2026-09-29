#include "video_player.h"
#include "platform.h"
#include <vita2d.h>
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exported by VitaSDK's SceAvPlayer stub database but absent from its header.
   Signatures: Vita3K SceAvPlayer.cpp; explicit stream startup follows VMP. */
extern int32_t sceAvPlayerStreamCount(SceAvPlayerHandle handle);
extern int32_t sceAvPlayerEnableStream(SceAvPlayerHandle handle, uint32_t stream);

typedef struct {
    SceAvPlayerHandle handle;
    atomic_int running;
    atomic_int audio_error;
    atomic_int ready;
    atomic_int started;
} Playback;
static void player_event(void *ctx, int32_t event, int32_t source, void *data) {
    (void)data;
    Playback *s = ctx;
    char message[96];
    snprintf(message, sizeof(message), "AVPLAYER event: 0x%08x source=%d", (unsigned)event, source);
    platform_log(message);
    if (event == 2) atomic_store(&s->ready, 1); /* STATE_READY */
}
static void *cpu_alloc(void *ctx, uint32_t alignment, uint32_t size) {
    (void)ctx;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (!size || size > 32u * 1024u * 1024u) return NULL;
    void *ptr = memalign(alignment, size);
    if (!ptr) platform_log("CPU video allocation failed");
    return ptr;
}
static void cpu_free(void *ctx, void *ptr) { (void)ctx; free(ptr); }
static void *frame_alloc(void *ctx, uint32_t alignment, uint32_t size) {
    (void)ctx;
    char request[128];
    snprintf(request, sizeof(request), "Video frame request: bytes=%u alignment=%u", size, alignment);
    platform_log(request);
    /* This callback also allocates decoder workspace, not only visible frames.
       Hardware requests 9 MiB for the current 640x368 H.264 stream. */
    if (!size || size > 32u * 1024u * 1024u || alignment > 1024u * 1024u) {
        platform_log("Video frame request rejected by allocator limits");
        return NULL;
    }
    uint32_t rounded = (size + 0xFFFFF) & ~0xFFFFFu;
    SceUID block = sceKernelAllocMemBlock("nfsmw-video", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, rounded, NULL);
    char message[128];
    snprintf(message, sizeof(message), "Video frame allocation: bytes=%u block=0x%08x", rounded, (unsigned)block);
    platform_log(message);
    if (block < 0) return NULL;
    void *ptr = NULL;
    int rc = sceKernelGetMemBlockBase(block, &ptr);
    if (rc >= 0) rc = sceGxmMapMemory(ptr, rounded, SCE_GXM_MEMORY_ATTRIB_RW);
    snprintf(message, sizeof(message), "Video frame mapping: ptr=%p result=0x%08x", ptr, (unsigned)rc);
    platform_log(message);
    if (rc < 0) {
        sceKernelFreeMemBlock(block); return NULL;
    }
    return ptr;
}
static void frame_free(void *ctx, void *ptr) {
    (void)ctx;
    if (!ptr) return;
    /* Rendering is finished before requesting the next decoder frame/close. */
    SceUID block = sceKernelFindMemBlockByAddr(ptr, 0);
    sceGxmUnmapMemory(ptr);
    if (block >= 0) sceKernelFreeMemBlock(block);
}
static int audio_main(SceSize size, void *arg) {
    (void)size;
    Playback *s = *(Playback **)arg;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { atomic_store(&s->audio_error, port); return 0; }
    while (atomic_load(&s->running)) {
        if (!atomic_load(&s->started)) { sceKernelDelayThread(1000); continue; }
        SceAvPlayerFrameInfo f = {0};
        if (!sceAvPlayerGetAudioData(s->handle, &f)) {
            sceKernelDelayThread(1000); continue;
        }
        /* Prepared media is AAC-LC stereo 48kHz, 1024 samples per frame. */
        if (!f.pData || f.details.audio.channelCount != 2 ||
            f.details.audio.sampleRate != 48000 || f.details.audio.size != 4096) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Unexpected audio: channels=%u rate=%u bytes=%u",
                f.details.audio.channelCount, f.details.audio.sampleRate, f.details.audio.size);
            platform_log(msg);
            atomic_store(&s->audio_error, -1); break;
        }
        int rc = sceAudioOutOutput(port, f.pData);
        if (rc < 0) { atomic_store(&s->audio_error, rc); break; }
    }
    sceAudioOutOutput(port, NULL);
    sceAudioOutReleasePort(port);
    return 0;
}
static int module_loaded;
static void log_result(const char *stage, int value) {
    char message[128];
    snprintf(message, sizeof(message), "%s: 0x%08x", stage, (unsigned)value);
    platform_log(message);
}
int video_play(const char *path) {
    platform_log(path);
    FILE *file = fopen(path, "rb");
    if (!file) { platform_log("Video missing"); return -1; }
    fclose(file);
    int rc;
    if (!module_loaded) {
        platform_log("AVPLAYER loading");
        rc = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
        log_result("AVPLAYER load", rc);
        if (rc < 0) return -2;
        /* Keep the module resident until process exit, including failed opens. */
        module_loaded = 1;
    }
    Playback s;
    memset(&s, 0, sizeof(s));
    atomic_init(&s.running, 0); atomic_init(&s.audio_error, 0);
    atomic_init(&s.ready, 0); atomic_init(&s.started, 0);
    SceAvPlayerInitData init = {0};
    init.memoryReplacement.allocate = cpu_alloc;
    init.memoryReplacement.deallocate = cpu_free;
    init.memoryReplacement.allocateTexture = frame_alloc;
    init.memoryReplacement.deallocateTexture = frame_free;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 5;
    init.autoStart = SCE_FALSE;
    init.eventReplacement.objectPointer = &s;
    init.eventReplacement.eventCallback = player_event;
    platform_log("AVPLAYER initializing");
    s.handle = sceAvPlayerInit(&init);
    log_result("AVPLAYER handle", s.handle);
    /* This is an opaque pointer-sized handle, despite VitaSDK's signed typedef.
       Valid addresses such as 0x81225a80 have the sign bit set. */
    if (s.handle == 0) return -3;
    int result = -4;
    SceUID thread = -1;
    platform_log("AVPLAYER adding source");
    rc = sceAvPlayerAddSource(s.handle, path);
    log_result("AVPLAYER source", rc);
    if (rc < 0) goto cleanup;
    thread = sceKernelCreateThread("nfsmw-video-audio", audio_main, 0x10000100 - 10, 0x10000, 0, 0, NULL);
    log_result("Audio thread", thread);
    if (thread < 0) goto cleanup;
    atomic_store(&s.running, 1);
    Playback *arg = &s;
    rc = sceKernelStartThread(thread, sizeof(arg), &arg);
    log_result("Audio start", rc);
    if (rc < 0) {
        sceKernelDeleteThread(thread); thread = -1; goto cleanup;
    }
    uint64_t start = sceKernelGetProcessTimeWide(), last = start;
    uint32_t previous = platform_buttons();
    int decoded = 0;
    platform_log(path);
    for (;;) {
        uint32_t buttons = platform_buttons(), pressed = buttons & ~previous;
        previous = buttons;
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE)) { result = 1; break; }
        uint64_t now = sceKernelGetProcessTimeWide();
        if (now - start > 180000000ULL || now - last > (decoded ? 8000000ULL : 15000000ULL)) {
            result = -5; break;
        }
        if (atomic_load(&s.audio_error)) { result = -6; break; }
        if (!atomic_load(&s.started)) {
            if (!atomic_load(&s.ready)) { sceKernelDelayThread(1000); continue; }
            int streams = sceAvPlayerStreamCount(s.handle);
            log_result("AVPLAYER stream count", streams);
            if (streams < 1 || streams > 32) { result = -9; break; }
            int have_video = 0;
            for (int i = 0; i < streams; ++i) {
                SceAvPlayerStreamInfo info = {0};
                rc = sceAvPlayerGetStreamInfo(s.handle, i, &info);
                log_result("AVPLAYER stream info", rc);
                if (rc < 0) { result = -9; goto cleanup; }
                char stream_message[128];
                snprintf(stream_message, sizeof(stream_message), "Stream %d type=%u duration=%llu", i,
                    (unsigned)info.type, (unsigned long long)info.duration);
                platform_log(stream_message);
                if (info.type == SCE_AVPLAYER_VIDEO) {
                    snprintf(stream_message, sizeof(stream_message), "Video dimensions: %ux%u", info.details.video.width, info.details.video.height);
                    platform_log(stream_message);
                }
                if (info.type == SCE_AVPLAYER_VIDEO || info.type == SCE_AVPLAYER_AUDIO) {
                    rc = sceAvPlayerEnableStream(s.handle, i);
                    log_result("AVPLAYER enable stream", rc);
                    if (rc < 0) { result = -9; goto cleanup; }
                    if (info.type == SCE_AVPLAYER_VIDEO) have_video = 1;
                }
            }
            if (!have_video) { result = -9; break; }
            rc = sceAvPlayerStart(s.handle);
            log_result("AVPLAYER explicit start", rc);
            if (rc < 0) { result = -9; break; }
            atomic_store(&s.started, 1);
            last = now;
        }
        SceAvPlayerFrameInfo f = {0};
        if (sceAvPlayerGetVideoData(s.handle, &f)) {
            unsigned w = f.details.video.width, h = f.details.video.height;
            if (!f.pData || w != 640 || h != 368) { result = -7; break; }
            vita2d_texture texture;
            memset(&texture, 0, sizeof(texture));
            if (sceGxmTextureInitLinear(&texture.gxm_tex, f.pData,
                SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1, w, h, 0) < 0) { result = -8; break; }
            sceGxmTextureSetMinFilter(&texture.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
            sceGxmTextureSetMagFilter(&texture.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
            float scale = 544.0f / h;
            vita2d_start_drawing(); vita2d_clear_screen();
            vita2d_draw_texture_scale(&texture, (960.0f - w * scale) / 2.0f, 0, scale, scale);
            vita2d_end_drawing();
            vita2d_wait_rendering_done();
            vita2d_swap_buffers();
            if (!decoded) platform_log("First video frame rendered");
            decoded = 1; last = now;
        } else {
            if (decoded && !sceAvPlayerIsActive(s.handle)) { result = 0; break; }
            sceKernelDelayThread(1000);
        }
    }
cleanup:
    atomic_store(&s.running, 0);
    if (thread >= 0) {
        sceKernelWaitThreadEnd(thread, NULL, NULL);
        sceKernelDeleteThread(thread);
    }
    vita2d_wait_rendering_done();
    log_result("AVPLAYER stop", sceAvPlayerStop(s.handle));
    log_result("AVPLAYER close", sceAvPlayerClose(s.handle));
    char message[80]; snprintf(message, sizeof(message), "Video result: %d", result);
    platform_log(message);
    return result;
}
