#include "port/RetailVideo.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static void Log(const char *fmt, ...) {
    FILE *f = std::fopen("ux0:data/nfsmw/logs/m12.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

static bool SkipPressed(unsigned &previous) {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const unsigned pressed = pad.buttons & ~previous;
    previous = pad.buttons;
    return (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) != 0;
}

static void DrawFrame(vita2d_texture *texture, int width, int height) {
    if (!texture || width <= 0 || height <= 0) return;
    const float sx = 960.0f / static_cast<float>(width);
    const float sy = 544.0f / static_cast<float>(height);
    const float scale = std::min(sx, sy);
    const float x = (960.0f - width * scale) * 0.5f;
    const float y = (544.0f - height * scale) * 0.5f;

    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_texture_scale(texture, x, y, scale, scale);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

} // namespace

int RetailVideoPlay(const char *path) {
    if (!path) return -1;

    FILE *probe = std::fopen(path, "rb");
    if (!probe) {
        Log("VP6 missing: %s", path);
        return -2;
    }
    std::fclose(probe);

    Log("VP6 open: %s", path);

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        Log("VP6 avformat_open_input failed: %s", path);
        return -3;
    }

    int result = -4;
    AVCodecContext *codec = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *sws = nullptr;
    vita2d_texture *texture = nullptr;

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        Log("VP6 stream info failed");
        goto cleanup;
    }

    const int stream_index =
        av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        Log("VP6 video stream missing");
        goto cleanup;
    }

    AVStream *stream = fmt->streams[stream_index];
    const AVCodec *decoder =
        avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        Log("VP6 decoder missing codec_id=%d", stream->codecpar->codec_id);
        goto cleanup;
    }

    codec = avcodec_alloc_context3(decoder);
    if (!codec ||
        avcodec_parameters_to_context(codec, stream->codecpar) < 0 ||
        avcodec_open2(codec, decoder, nullptr) < 0) {
        Log("VP6 decoder init failed");
        goto cleanup;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        Log("VP6 packet/frame allocation failed");
        goto cleanup;
    }

    const int width = codec->width;
    const int height = codec->height;
    if (width <= 0 || height <= 0 || width > 1280 || height > 720) {
        Log("VP6 dimensions rejected: %dx%d", width, height);
        goto cleanup;
    }

    texture = vita2d_create_empty_texture(width, height);
    if (!texture) {
        Log("VP6 Vita texture allocation failed");
        goto cleanup;
    }

    sws = sws_getContext(
        width, height, codec->pix_fmt,
        width, height, AV_PIX_FMT_RGBA,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        Log("VP6 swscale init failed");
        goto cleanup;
    }

    AVRational rate = av_guess_frame_rate(fmt, stream, nullptr);
    double fps = 30.0;
    if (rate.num > 0 && rate.den > 0) {
        fps = av_q2d(rate);
        if (!std::isfinite(fps) || fps < 5.0 || fps > 120.0)
            fps = 30.0;
    }
    const unsigned frame_delay_us =
        static_cast<unsigned>(1000000.0 / fps);

    Log("VP6 decoder=%s %dx%d %.2f fps",
        decoder->name ? decoder->name : "?", width, height, fps);

    unsigned previous = 0;
    bool eof = false;
    bool skip = false;

    while (!skip) {
        int read_rc = av_read_frame(fmt, packet);
        if (read_rc < 0) {
            eof = true;
            avcodec_send_packet(codec, nullptr);
        } else if (packet->stream_index == stream_index) {
            avcodec_send_packet(codec, packet);
        }
        av_packet_unref(packet);

        for (;;) {
            const int rc = avcodec_receive_frame(codec, frame);
            if (rc == AVERROR(EAGAIN))
                break;
            if (rc == AVERROR_EOF) {
                result = 0;
                goto cleanup;
            }
            if (rc < 0) {
                Log("VP6 decode error=%d", rc);
                result = -5;
                goto cleanup;
            }

            auto *dst =
                static_cast<unsigned char *>(vita2d_texture_get_datap(texture));
            const int stride =
                static_cast<int>(vita2d_texture_get_stride(texture));
            unsigned char *dst_data[4] = {dst, nullptr, nullptr, nullptr};
            int dst_linesize[4] = {stride, 0, 0, 0};

            sws_scale(
                sws,
                frame->data,
                frame->linesize,
                0,
                height,
                dst_data,
                dst_linesize);

            DrawFrame(texture, width, height);

            if (SkipPressed(previous)) {
                Log("VP6 skipped by user");
                result = 1;
                goto cleanup;
            }

            sceKernelDelayThread(frame_delay_us);
        }

        if (eof) {
            result = 0;
            break;
        }

        if (SkipPressed(previous)) {
            Log("VP6 skipped by user");
            result = 1;
            break;
        }
    }

cleanup:
    if (texture) vita2d_free_texture(texture);
    if (sws) sws_freeContext(sws);
    if (packet) av_packet_free(&packet);
    if (frame) av_frame_free(&frame);
    if (codec) avcodec_free_context(&codec);
    if (fmt) avformat_close_input(&fmt);
    Log("VP6 result=%d: %s", result, path);
    return result;
}
