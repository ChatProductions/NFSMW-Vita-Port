#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"
#include "port/JdlzFile.h"
#include "port/TpkMetadata.h"
#include "port/TpkTexture.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr unsigned int kExpected = 0x66F9D7D0u;
static constexpr const char *kProbe = "NFSMW-Vita-Port";
static constexpr const char *kGlobalAPath = "ux0:data/nfsmw/GLOBALA.BUN";
static constexpr const char *kGlobalBPath = "ux0:data/nfsmw/GlobalB.lzc";

static void Draw(vita2d_pgf *font, int x, int y, unsigned int color,
                 float scale, const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

int main() {
    const unsigned int crc =
        bCalculateCrc32(kProbe, static_cast<int>(std::strlen(kProbe)), 0);
    const bool crc_pass = crc == kExpected;

    const BundleProbeResult global_a = ProbeNfsmwBundle(kGlobalAPath);
    const TpkMetadata tpk = ReadTpkMetadata(kGlobalAPath);
    WriteTpkMetadataLog(kGlobalAPath, tpk);

    JdlzMemoryResult global_b_jdlz =
        DecompressJdlzFileToMemory(kGlobalBPath);

    BundleProbeResult global_b{};
    if (global_b_jdlz.valid) {
        global_b = ProbeNfsmwBundleMemory(
            global_b_jdlz.data,
            global_b_jdlz.decompressed_size);
        // The probe copies the metadata it needs. Release the 2.8 MB
        // decompressed bundle before initializing the renderer.
        FreeJdlzMemory(global_b_jdlz);
    }

    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 18, 18, 255));
    vita2d_pgf *font = vita2d_load_default_pgf();

    vita2d_texture *textures[TpkMetadata::kMaxTextures]{};
    TpkTextureLoadResult load_results[TpkMetadata::kMaxTextures]{};

    std::size_t loaded_count = 0;
    if (tpk.valid) {
        for (std::size_t i = 0; i < tpk.displayed_textures; ++i) {
            textures[i] =
                LoadTpkTextureBaseLevel(kGlobalAPath, tpk, i, &load_results[i]);
            if (textures[i])
                ++loaded_count;
        }
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    std::size_t selected = 0;
    unsigned int previous_buttons = 0;
    bool show_global_b = false;
    bool running = true;

    while (running) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);

        const unsigned int pressed = pad.buttons & ~previous_buttons;
        previous_buttons = pad.buttons;

        if (pressed & SCE_CTRL_START)
            running = false;

        if (pressed & SCE_CTRL_TRIANGLE)
            show_global_b = !show_global_b;

        if (!show_global_b && tpk.displayed_textures > 0) {
            if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER))
                selected = (selected + 1) % tpk.displayed_textures;

            if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER))
                selected = (selected + tpk.displayed_textures - 1) %
                           tpk.displayed_textures;
        }

        vita2d_start_drawing();
        vita2d_clear_screen();

        const unsigned int white = RGBA8(255, 255, 255, 255);
        const unsigned int green = RGBA8(80, 220, 120, 255);
        const unsigned int amber = RGBA8(245, 190, 70, 255);
        const unsigned int red = RGBA8(240, 80, 80, 255);
        const unsigned int gray = RGBA8(185, 185, 185, 255);

        Draw(font, 28, 38, white, 1.0f,
             "NFSMW Vita Port - Milestone 5");

        char line[192];

        if (!show_global_b) {
            Draw(font, 720, 38, gray, 0.58f, "Page: GLOBALA textures");

            if (!global_a.found) {
                Draw(font, 28, 95, amber, 0.88f,
                     "GLOBALA.BUN not found");
            } else if (!global_a.valid || !tpk.valid) {
                Draw(font, 28, 95, red, 0.88f,
                     "GLOBALA.BUN / TPK validation failed");
            } else if (tpk.displayed_textures == 0) {
                Draw(font, 28, 95, red, 0.88f,
                     "No texture metadata found");
            } else {
                const TpkTextureMetadata &meta = tpk.textures[selected];

                char fmt[16];
                std::snprintf(line, sizeof(line),
                              "%u/%u  %s  %ux%u  %s",
                              static_cast<unsigned>(selected + 1),
                              static_cast<unsigned>(tpk.displayed_textures),
                              meta.name,
                              static_cast<unsigned>(meta.width),
                              static_cast<unsigned>(meta.height),
                              DescribeTpkFormat(meta.format, fmt, sizeof(fmt)));
                Draw(font, 28, 76, textures[selected] ? green : red, 0.76f, line);

                std::snprintf(line, sizeof(line),
                              "offset 0x%08X  base %u B  mips %u",
                              meta.data_offset,
                              meta.base_size,
                              static_cast<unsigned>(meta.mip_count));
                Draw(font, 28, 105, gray, 0.60f, line);

                if (textures[selected]) {
                    const float max_w = 850.0f;
                    const float max_h = 300.0f;
                    const float sx = max_w / static_cast<float>(meta.width);
                    const float sy = max_h / static_cast<float>(meta.height);
                    const float scale = std::min(4.0f, std::min(sx, sy));

                    const float draw_w = meta.width * scale;
                    const float draw_h = meta.height * scale;
                    const float x = (960.0f - draw_w) * 0.5f;
                    const float y = 135.0f + (300.0f - draw_h) * 0.5f;

                    vita2d_draw_texture_scale(textures[selected], x, y, scale, scale);
                } else {
                    std::snprintf(line, sizeof(line),
                                  "Texture decode failed: %s",
                                  DescribeTpkTextureLoadResult(load_results[selected]));
                    Draw(font, 28, 220, red, 0.80f, line);
                }

                std::snprintf(line, sizeof(line),
                              "Pack %s | decoded %u/%u",
                              tpk.pack_name,
                              static_cast<unsigned>(loaded_count),
                              static_cast<unsigned>(tpk.displayed_textures));
                Draw(font, 28, 468, white, 0.62f, line);
            }
        } else {
            Draw(font, 720, 38, gray, 0.58f, "Page: GlobalB JDLZ");

            if (!global_b_jdlz.found) {
                Draw(font, 28, 92, amber, 0.85f,
                     "GlobalB.lzc not found");
                Draw(font, 28, 132, white, 0.66f,
                     "Copy PC GLOBAL/GlobalB.lzc to:");
                Draw(font, 28, 162, white, 0.66f,
                     "ux0:data/nfsmw/GlobalB.lzc");
            } else if (!global_b_jdlz.valid) {
                std::snprintf(line, sizeof(line),
                              "JDLZ failed: %s", global_b_jdlz.error);
                Draw(font, 28, 92, red, 0.80f, line);
            } else {
                std::snprintf(line, sizeof(line),
                              "JDLZ v%u: %u bytes -> %u bytes",
                              static_cast<unsigned>(global_b_jdlz.version),
                              global_b_jdlz.compressed_size,
                              global_b_jdlz.decompressed_size);
                Draw(font, 28, 76, green, 0.76f, line);

                Draw(font, 28, 106, gray, 0.58f,
                     "Decompressed in RAM only (no .raw cache file)");

                if (!global_b.found || !global_b.valid) {
                    Draw(font, 28, 150, red, 0.82f,
                         "Decompressed GlobalB chunk tree INVALID");
                } else {
                    std::snprintf(line, sizeof(line),
                                  "EAGL chunk tree: VALID | %u chunks | %u bytes",
                                  global_b.parsed_chunks,
                                  global_b.file_size);
                    Draw(font, 28, 145, green, 0.70f, line);

                    int y = 185;
                    for (std::size_t i = 0;
                         i < global_b.displayed_chunks && y <= 445;
                         ++i) {
                        const BundleChunkInfo &c = global_b.chunks[i];
                        std::snprintf(line, sizeof(line),
                                      "%c d%u off %08X id %08X size %d",
                                      c.nested ? 'N' : 'D',
                                      static_cast<unsigned>(c.depth),
                                      c.offset,
                                      c.id,
                                      c.size);
                        Draw(font, 42, y, white, 0.61f, line);
                        y += 24;
                    }
                }
            }
        }

        std::snprintf(line, sizeof(line),
                      "VishDec CRC %s | GLOBALA %s",
                      crc_pass ? "PASS" : "FAIL",
                      global_a.valid ? "VALID" : "INVALID");
        Draw(font, 28, 495, crc_pass ? green : red, 0.58f, line);

        Draw(font, 500, 520, white, 0.60f,
             "TRIANGLE: page   L/R: texture   START: exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_wait_rendering_done();

    for (std::size_t i = 0; i < TpkMetadata::kMaxTextures; ++i) {
        if (textures[i])
            vita2d_free_texture(textures[i]);
    }

    vita2d_free_pgf(font);
    vita2d_fini();

    const bool base_ok =
        crc_pass &&
        global_a.found &&
        global_a.valid &&
        tpk.valid &&
        loaded_count == tpk.displayed_textures &&
        loaded_count > 0;

    const bool global_b_ok =
        !global_b_jdlz.found ||
        (global_b_jdlz.valid && global_b.found && global_b.valid);

    const bool ok = base_ok && global_b_ok;

    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
