#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"
#include "port/TpkMetadata.h"
#include "port/TpkTexture.h"

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
    vita2d_texture *mw_logo =
        tpk.valid ? LoadTpkArgb32BaseLevel(kBundlePath, tpk, 0) : nullptr;

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
             "NFSMW Vita Port - Milestone 3");

        if (!bundle.found) {
            Draw(font, 32, 110, amber, 0.9f,
                 "GLOBALA.BUN not found");
        } else if (!bundle.valid || !tpk.valid) {
            Draw(font, 32, 110, red, 0.9f,
                 "GLOBALA.BUN / TPK validation failed");
        } else if (!mw_logo) {
            Draw(font, 32, 110, red, 0.9f,
                 "MW_LOGO texture load failed");
        } else {
            Draw(font, 32, 86, green, 0.72f,
                 "Real NFSMW asset decoded from GLOBALA.BUN");

            vita2d_draw_texture(mw_logo, 224.0f, 145.0f);

            char line[160];
            const TpkTextureMetadata &logo = tpk.textures[0];
            std::snprintf(line, sizeof(line),
                          "%s  %ux%u  ARGB32  %u bytes",
                          logo.name,
                          static_cast<unsigned>(logo.width),
                          static_cast<unsigned>(logo.height),
                          logo.base_size);
            Draw(font, 32, 330, white, 0.72f, line);

            std::snprintf(line, sizeof(line),
                          "Pack: %s  |  source: %s",
                          tpk.pack_name, tpk.source_path);
            Draw(font, 32, 365, white, 0.60f, line);

            std::snprintf(line, sizeof(line),
                          "VishDec CRC: %s  |  chunk tree: %s",
                          crc_pass ? "PASS" : "FAIL",
                          bundle.valid ? "VALID" : "INVALID");
            Draw(font, 32, 405, crc_pass ? green : red, 0.62f, line);
        }

        Draw(font, 32, 510, white, 0.70f,
             "START: exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    } while (!(pad.buttons & SCE_CTRL_START));

    // Drain all commands before freeing sampled textures or font atlases.
    vita2d_wait_rendering_done();
    if (mw_logo)
        vita2d_free_texture(mw_logo);
    vita2d_free_pgf(font);
    vita2d_fini();

    const bool ok = crc_pass && bundle.found && bundle.valid && tpk.valid && mw_logo;
    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
