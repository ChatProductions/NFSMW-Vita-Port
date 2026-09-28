#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include <cstdio>
#include <cstring>

static constexpr unsigned int kExpected = 0x66F9D7D0u;
static constexpr const char *kProbe = "NFSMW-Vita-Port";
static constexpr const char *kBundlePath = "ux0:data/nfsmw/GLOBALA.BUN";

static void DrawText(vita2d_pgf *font, int x, int y, unsigned int color,
                     float scale, const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

int main() {
    const unsigned int crc =
        bCalculateCrc32(kProbe, static_cast<int>(std::strlen(kProbe)), 0);
    const bool crc_pass = crc == kExpected;

    BundleProbeResult bundle = ProbeNfsmwBundle(kBundlePath);
    WriteBundleProbeLog(kBundlePath, bundle);

    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 18, 18, 255));
    vita2d_pgf *font = vita2d_load_default_pgf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceCtrlData pad{};
    do {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        vita2d_start_drawing();
        vita2d_clear_screen();

        const unsigned int white = RGBA8(255, 255, 255, 255);
        const unsigned int green = RGBA8(80, 220, 120, 255);
        const unsigned int amber = RGBA8(245, 190, 70, 255);
        const unsigned int red = RGBA8(240, 80, 80, 255);

        DrawText(font, 36, 50, white, 1.1f,
                 "NFSMW Vita Port - Milestone 1");

        char crc_line[96];
        std::snprintf(crc_line, sizeof(crc_line),
                      "VishDec CRC core: %s (%08X)",
                      crc_pass ? "PASS" : "FAIL", crc);
        DrawText(font, 36, 88, crc_pass ? green : red, 0.85f, crc_line);

        if (!bundle.found) {
            DrawText(font, 36, 145, amber, 1.0f,
                     "Waiting for real NFSMW PC game data");
            DrawText(font, 36, 190, white, 0.78f,
                     "Copy PC GLOBAL\\GLOBALA.BUN to:");
            DrawText(font, 36, 222, white, 0.78f,
                     "ux0:data/nfsmw/GLOBALA.BUN");
            DrawText(font, 36, 280, white, 0.78f,
                     "Then relaunch this app.");
        } else {
            char status[128];
            std::snprintf(status, sizeof(status),
                          "GLOBALA.BUN: %u bytes - %s - %u chunks",
                          bundle.file_size,
                          bundle.valid ? "VALID" : "INVALID",
                          bundle.parsed_chunks);
            DrawText(font, 36, 132, bundle.valid ? green : red, 0.78f, status);

            int y = 174;
            for (std::size_t i = 0; i < bundle.displayed_chunks && y < 448; ++i) {
                const BundleChunkInfo &c = bundle.chunks[i];
                char line[128];
                std::snprintf(line, sizeof(line),
                              "%c d%u  off %08X  id %08X  size %d",
                              c.nested ? 'N' : 'D',
                              static_cast<unsigned>(c.depth),
                              c.offset,
                              c.id,
                              c.size);
                DrawText(font, 44, y, white, 0.67f, line);
                y += 23;
            }
        }

        DrawText(font, 36, 510, white, 0.72f,
                 "START: exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    } while (!(pad.buttons & SCE_CTRL_START));

    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    vita2d_fini();

    sceKernelExitProcess((crc_pass && (!bundle.found || bundle.valid)) ? 0 : 1);
    return 0;
}
