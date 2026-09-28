#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"
#include "port/TpkMetadata.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include <cstdio>
#include <cstring>

static constexpr unsigned int kExpected = 0x66F9D7D0u;
static constexpr const char *kProbe = "NFSMW-Vita-Port";
static constexpr const char *kBundlePath = "ux0:data/nfsmw/GLOBALA.BUN";

static void Draw(vita2d_pgf *font, int x, int y, unsigned int color,
                 float scale, const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

int main() {
    const unsigned int crc =
        bCalculateCrc32(kProbe, static_cast<int>(std::strlen(kProbe)), 0);
    const bool crc_pass = crc == kExpected;

    const BundleProbeResult bundle = ProbeNfsmwBundle(kBundlePath);
    WriteBundleProbeLog(kBundlePath, bundle);

    const TpkMetadata tpk = ReadTpkMetadata(kBundlePath);
    WriteTpkMetadataLog(kBundlePath, tpk);

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

        Draw(font, 32, 42, white, 1.05f,
             "NFSMW Vita Port - Milestone 2");

        char crc_line[96];
        std::snprintf(crc_line, sizeof(crc_line),
                      "VishDec CRC: %s", crc_pass ? "PASS" : "FAIL");
        Draw(font, 32, 75, crc_pass ? green : red, 0.72f, crc_line);

        if (!bundle.found) {
            Draw(font, 32, 130, amber, 0.9f,
                 "GLOBALA.BUN not found");
            Draw(font, 32, 165, white, 0.72f,
                 "Expected: ux0:data/nfsmw/GLOBALA.BUN");
        } else if (!bundle.valid) {
            Draw(font, 32, 130, red, 0.9f,
                 "GLOBALA.BUN chunk tree INVALID");
        } else if (!tpk.valid) {
            Draw(font, 32, 130, red, 0.9f,
                 "TPK metadata parser could not validate this pack");
        } else {
            char line[160];

            std::snprintf(line, sizeof(line),
                          "Pack: %s   version %u   textures %u",
                          tpk.pack_name, tpk.version, tpk.texture_count);
            Draw(font, 32, 112, green, 0.72f, line);

            std::snprintf(line, sizeof(line),
                          "Source: %s", tpk.source_path);
            Draw(font, 32, 139, white, 0.62f, line);

            int y = 180;
            for (std::size_t i = 0; i < tpk.displayed_textures && y <= 445; ++i) {
                const TpkTextureMetadata &t = tpk.textures[i];
                char fmt[16];
                std::snprintf(line, sizeof(line),
                              "%u. %-20s %ux%u  %s",
                              static_cast<unsigned>(i + 1),
                              t.name,
                              static_cast<unsigned>(t.width),
                              static_cast<unsigned>(t.height),
                              DescribeTpkFormat(t.format, fmt, sizeof(fmt)));
                Draw(font, 42, y, white, 0.70f, line);
                y += 42;
            }
        }

        Draw(font, 32, 510, white, 0.70f,
             "START: exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    } while (!(pad.buttons & SCE_CTRL_START));

    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    vita2d_fini();

    const bool ok = crc_pass && (!bundle.found || (bundle.valid && tpk.valid));
    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
