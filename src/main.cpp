#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"
#include "port/JdlzFile.h"
#include "port/TpkInventory.h"
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

static void Draw(vita2d_pgf *font,
                 int x,
                 int y,
                 unsigned int color,
                 float scale,
                 const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

static float FitScale(unsigned width,
                      unsigned height,
                      float max_w,
                      float max_h,
                      float max_scale) {
    if (width == 0 || height == 0)
        return 1.0f;

    const float sx = max_w / static_cast<float>(width);
    const float sy = max_h / static_cast<float>(height);

    return std::min(max_scale, std::min(sx, sy));
}

int main() {
    const unsigned int crc =
        bCalculateCrc32(
            kProbe,
            static_cast<int>(std::strlen(kProbe)),
            0);

    const bool crc_pass = crc == kExpected;

    const BundleProbeResult global_a =
        ProbeNfsmwBundle(kGlobalAPath);

    const TpkMetadata tpk =
        ReadTpkMetadata(kGlobalAPath);

    WriteTpkMetadataLog(kGlobalAPath, tpk);

    JdlzMemoryResult global_b_jdlz =
        DecompressJdlzFileToMemory(kGlobalBPath);

    BundleProbeResult global_b{};
    TpkInventory global_b_tpk{};

    if (global_b_jdlz.valid) {
        global_b =
            ProbeNfsmwBundleMemory(
                global_b_jdlz.data,
                global_b_jdlz.decompressed_size);

        if (global_b.valid) {
            global_b_tpk =
                ScanTpkPacksMemory(
                    global_b_jdlz.data,
                    global_b_jdlz.decompressed_size);
        }
    }

    std::size_t global_b_pack_index = 0;
    std::size_t global_b_texture_index = 0;

    TpkMetadata global_b_meta{};

    if (global_b_tpk.valid &&
        global_b_tpk.displayed_packs > 0) {
        const TpkPackSummary &pack =
            global_b_tpk.packs[0];

        global_b_meta =
            ReadTpkMetadataMemory(
                global_b_jdlz.data,
                global_b_jdlz.decompressed_size,
                pack.container_offset,
                pack.container_size);
    }

    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 18, 18, 255));

    vita2d_pgf *font =
        vita2d_load_default_pgf();

    vita2d_texture *global_a_textures[
        TpkMetadata::kMaxTextures]{};

    TpkTextureLoadResult global_a_results[
        TpkMetadata::kMaxTextures]{};

    std::size_t global_a_loaded_count = 0;

    if (tpk.valid) {
        for (std::size_t i = 0;
             i < tpk.displayed_textures;
             ++i) {
            global_a_textures[i] =
                LoadTpkTextureBaseLevel(
                    kGlobalAPath,
                    tpk,
                    i,
                    &global_a_results[i]);

            if (global_a_textures[i])
                ++global_a_loaded_count;
        }
    }

    vita2d_texture *global_b_texture = nullptr;
    TpkTextureLoadResult global_b_texture_result =
        TpkTextureLoadResult::InvalidMetadata;

    if (global_b_meta.valid &&
        global_b_meta.displayed_textures > 0) {
        global_b_texture =
            LoadTpkTextureBaseLevelMemory(
                global_b_jdlz.data,
                global_b_jdlz.decompressed_size,
                global_b_meta,
                0,
                &global_b_texture_result);
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    std::size_t global_a_selected = 0;
    std::size_t pack_scroll = 0;
    unsigned int previous_buttons = 0;
    unsigned int page = 0;
    bool running = true;

    auto reload_global_b_texture = [&]() {
        vita2d_wait_rendering_done();

        if (global_b_texture) {
            vita2d_free_texture(global_b_texture);
            global_b_texture = nullptr;
        }

        global_b_texture_result =
            TpkTextureLoadResult::InvalidMetadata;

        if (!global_b_tpk.valid ||
            global_b_pack_index >=
                global_b_tpk.displayed_packs) {
            global_b_meta = {};
            return;
        }

        const TpkPackSummary &pack =
            global_b_tpk.packs[global_b_pack_index];

        global_b_meta =
            ReadTpkMetadataMemory(
                global_b_jdlz.data,
                global_b_jdlz.decompressed_size,
                pack.container_offset,
                pack.container_size);

        if (!global_b_meta.valid ||
            global_b_meta.displayed_textures == 0) {
            global_b_texture_index = 0;
            return;
        }

        if (global_b_texture_index >=
            global_b_meta.displayed_textures) {
            global_b_texture_index = 0;
        }

        global_b_texture =
            LoadTpkTextureBaseLevelMemory(
                global_b_jdlz.data,
                global_b_jdlz.decompressed_size,
                global_b_meta,
                global_b_texture_index,
                &global_b_texture_result);
    };

    while (running) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);

        const unsigned int pressed =
            pad.buttons & ~previous_buttons;

        previous_buttons = pad.buttons;

        if (pressed & SCE_CTRL_START)
            running = false;

        if (pressed & SCE_CTRL_TRIANGLE)
            page = (page + 1u) % 4u;

        if (page == 0 &&
            tpk.displayed_textures > 0) {
            if (pressed &
                (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER)) {
                global_a_selected =
                    (global_a_selected + 1) %
                    tpk.displayed_textures;
            }

            if (pressed &
                (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) {
                global_a_selected =
                    (global_a_selected +
                     tpk.displayed_textures - 1) %
                    tpk.displayed_textures;
            }
        }

        if (page == 2 &&
            global_b_tpk.displayed_packs > 0) {
            constexpr std::size_t kRows = 9;

            if (pressed &
                (SCE_CTRL_DOWN | SCE_CTRL_RTRIGGER)) {
                if (pack_scroll + kRows <
                    global_b_tpk.displayed_packs) {
                    ++pack_scroll;
                }
            }

            if (pressed &
                (SCE_CTRL_UP | SCE_CTRL_LTRIGGER)) {
                if (pack_scroll > 0)
                    --pack_scroll;
            }
        }

        if (page == 3 &&
            global_b_tpk.valid &&
            global_b_tpk.displayed_packs > 0) {
            bool reload = false;

            if (pressed & SCE_CTRL_DOWN) {
                global_b_pack_index =
                    (global_b_pack_index + 1) %
                    global_b_tpk.displayed_packs;
                global_b_texture_index = 0;
                reload = true;
            }

            if (pressed & SCE_CTRL_UP) {
                global_b_pack_index =
                    (global_b_pack_index +
                     global_b_tpk.displayed_packs - 1) %
                    global_b_tpk.displayed_packs;
                global_b_texture_index = 0;
                reload = true;
            }

            if (!reload &&
                global_b_meta.valid &&
                global_b_meta.displayed_textures > 0) {
                if (pressed &
                    (SCE_CTRL_RIGHT |
                     SCE_CTRL_RTRIGGER)) {
                    global_b_texture_index =
                        (global_b_texture_index + 1) %
                        global_b_meta.displayed_textures;
                    reload = true;
                }

                if (pressed &
                    (SCE_CTRL_LEFT |
                     SCE_CTRL_LTRIGGER)) {
                    global_b_texture_index =
                        (global_b_texture_index +
                         global_b_meta.displayed_textures - 1) %
                        global_b_meta.displayed_textures;
                    reload = true;
                }
            }

            if (reload)
                reload_global_b_texture();
        }

        vita2d_start_drawing();
        vita2d_clear_screen();

        const unsigned int white =
            RGBA8(255, 255, 255, 255);
        const unsigned int green =
            RGBA8(80, 220, 120, 255);
        const unsigned int amber =
            RGBA8(245, 190, 70, 255);
        const unsigned int red =
            RGBA8(240, 80, 80, 255);
        const unsigned int gray =
            RGBA8(185, 185, 185, 255);

        Draw(font, 28, 38, white, 1.0f,
             "NFSMW Vita Port - Milestone 7");

        char line[224];

        if (page == 0) {
            Draw(font, 720, 38, gray, 0.58f,
                 "1/4 GLOBALA textures");

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
                const TpkTextureMetadata &meta =
                    tpk.textures[global_a_selected];

                char fmt[16];

                std::snprintf(
                    line,
                    sizeof(line),
                    "%u/%u  %s  %ux%u  %s",
                    static_cast<unsigned>(
                        global_a_selected + 1),
                    static_cast<unsigned>(
                        tpk.displayed_textures),
                    meta.name,
                    static_cast<unsigned>(meta.width),
                    static_cast<unsigned>(meta.height),
                    DescribeTpkFormat(
                        meta.format,
                        fmt,
                        sizeof(fmt)));

                Draw(
                    font,
                    28,
                    76,
                    global_a_textures[global_a_selected]
                        ? green
                        : red,
                    0.76f,
                    line);

                if (global_a_textures[global_a_selected]) {
                    const float scale =
                        FitScale(
                            meta.width,
                            meta.height,
                            850.0f,
                            300.0f,
                            4.0f);

                    const float draw_w =
                        meta.width * scale;
                    const float draw_h =
                        meta.height * scale;

                    const float x =
                        (960.0f - draw_w) * 0.5f;
                    const float y =
                        135.0f +
                        (300.0f - draw_h) * 0.5f;

                    vita2d_draw_texture_scale(
                        global_a_textures[
                            global_a_selected],
                        x,
                        y,
                        scale,
                        scale);
                } else {
                    std::snprintf(
                        line,
                        sizeof(line),
                        "Texture decode failed: %s",
                        DescribeTpkTextureLoadResult(
                            global_a_results[
                                global_a_selected]));

                    Draw(font, 28, 220, red, 0.75f, line);
                }

                std::snprintf(
                    line,
                    sizeof(line),
                    "Pack %s | decoded %u/%u",
                    tpk.pack_name,
                    static_cast<unsigned>(
                        global_a_loaded_count),
                    static_cast<unsigned>(
                        tpk.displayed_textures));

                Draw(font, 28, 468, white, 0.62f, line);
            }
        } else if (page == 1) {
            Draw(font, 720, 38, gray, 0.58f,
                 "2/4 GlobalB JDLZ");

            if (!global_b_jdlz.found) {
                Draw(font, 28, 92, amber, 0.85f,
                     "GlobalB.lzc not found");
            } else if (!global_b_jdlz.valid) {
                std::snprintf(
                    line,
                    sizeof(line),
                    "JDLZ failed: %s",
                    global_b_jdlz.error);

                Draw(font, 28, 92, red, 0.80f, line);
            } else {
                std::snprintf(
                    line,
                    sizeof(line),
                    "JDLZ v%u: %u bytes -> %u bytes",
                    static_cast<unsigned>(
                        global_b_jdlz.version),
                    global_b_jdlz.compressed_size,
                    global_b_jdlz.decompressed_size);

                Draw(font, 28, 76, green, 0.76f, line);

                Draw(font, 28, 106, gray, 0.58f,
                     "Decompressed in RAM; no .raw file required");

                if (!global_b.found || !global_b.valid) {
                    Draw(font, 28, 150, red, 0.82f,
                         "Decompressed GlobalB tree INVALID");
                } else {
                    std::snprintf(
                        line,
                        sizeof(line),
                        "EAGL tree VALID | %u chunks | %u bytes",
                        global_b.parsed_chunks,
                        global_b.file_size);

                    Draw(font, 28, 145, green, 0.70f, line);

                    int y = 185;

                    for (std::size_t i = 0;
                         i < global_b.displayed_chunks &&
                         y <= 445;
                         ++i) {
                        const BundleChunkInfo &c =
                            global_b.chunks[i];

                        std::snprintf(
                            line,
                            sizeof(line),
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
        } else if (page == 2) {
            Draw(font, 720, 38, gray, 0.58f,
                 "3/4 GlobalB TPK index");

            if (!global_b_tpk.valid) {
                Draw(font, 28, 92, red, 0.82f,
                     "GlobalB TPK inventory unavailable");
            } else {
                std::snprintf(
                    line,
                    sizeof(line),
                    "Found %u TPK container(s)",
                    global_b_tpk.pack_count_total);

                Draw(font, 28, 76, green, 0.72f, line);

                int y = 118;

                constexpr std::size_t kRows = 9;

                const std::size_t end =
                    std::min(
                        global_b_tpk.displayed_packs,
                        pack_scroll + kRows);

                for (std::size_t i = pack_scroll;
                     i < end;
                     ++i) {
                    const TpkPackSummary &pack =
                        global_b_tpk.packs[i];

                    std::snprintf(
                        line,
                        sizeof(line),
                        "%u. %s",
                        static_cast<unsigned>(i + 1),
                        pack.source_path[0]
                            ? pack.source_path
                            : pack.name);

                    Draw(font, 36, y, white, 0.62f, line);

                    std::snprintf(
                        line,
                        sizeof(line),
                        "   %u textures | v%u | %s",
                        pack.texture_count,
                        pack.version,
                        pack.compressed_entries
                            ? "compressed entries"
                            : "standard");

                    Draw(font, 36, y + 22, gray, 0.55f, line);
                    y += 54;
                }
            }
        } else {
            Draw(font, 720, 38, gray, 0.58f,
                 "4/4 GlobalB texture browser");

            if (!global_b_tpk.valid ||
                global_b_tpk.displayed_packs == 0 ||
                !global_b_meta.valid) {
                Draw(font, 28, 95, red, 0.82f,
                     "Embedded TPK metadata unavailable");
            } else {
                const TpkPackSummary &pack =
                    global_b_tpk.packs[
                        global_b_pack_index];

                const TpkTextureMetadata &meta =
                    global_b_meta.textures[
                        global_b_texture_index];

                std::snprintf(
                    line,
                    sizeof(line),
                    "Pack %u/%u: %s",
                    static_cast<unsigned>(
                        global_b_pack_index + 1),
                    static_cast<unsigned>(
                        global_b_tpk.displayed_packs),
                    pack.source_path);

                Draw(font, 28, 73, green, 0.65f, line);

                char fmt[16];

                std::snprintf(
                    line,
                    sizeof(line),
                    "Texture %u/%u: %s | %ux%u | %s",
                    static_cast<unsigned>(
                        global_b_texture_index + 1),
                    static_cast<unsigned>(
                        global_b_meta.displayed_textures),
                    meta.name,
                    static_cast<unsigned>(meta.width),
                    static_cast<unsigned>(meta.height),
                    DescribeTpkFormat(
                        meta.format,
                        fmt,
                        sizeof(fmt)));

                Draw(
                    font,
                    28,
                    104,
                    global_b_texture ? green : amber,
                    0.70f,
                    line);

                if (global_b_texture) {
                    const float scale =
                        FitScale(
                            meta.width,
                            meta.height,
                            840.0f,
                            300.0f,
                            4.0f);

                    const float draw_w =
                        meta.width * scale;
                    const float draw_h =
                        meta.height * scale;

                    const float x =
                        (960.0f - draw_w) * 0.5f;
                    const float y =
                        145.0f +
                        (300.0f - draw_h) * 0.5f;

                    vita2d_draw_texture_scale(
                        global_b_texture,
                        x,
                        y,
                        scale,
                        scale);
                } else {
                    std::snprintf(
                        line,
                        sizeof(line),
                        "Not rendered: %s",
                        DescribeTpkTextureLoadResult(
                            global_b_texture_result));

                    Draw(font, 28, 220, amber, 0.74f, line);
                }

                std::snprintf(
                    line,
                    sizeof(line),
                    "data off 0x%08X | base %u B | mips %u",
                    meta.data_offset,
                    meta.base_size,
                    static_cast<unsigned>(meta.mip_count));

                Draw(font, 28, 465, white, 0.58f, line);

                Draw(font, 28, 490, gray, 0.55f,
                     "UP/DOWN pack | LEFT/RIGHT or L/R texture");
            }
        }

        std::snprintf(
            line,
            sizeof(line),
            "CRC %s | GLOBALA %s | GlobalB %s | TPK %s",
            crc_pass ? "PASS" : "FAIL",
            global_a.valid ? "VALID" : "INVALID",
            global_b.valid ? "VALID" :
                (global_b_jdlz.found ? "INVALID" : "N/A"),
            global_b_tpk.valid ? "VALID" : "N/A");

        Draw(
            font,
            28,
            514,
            crc_pass ? green : red,
            0.53f,
            line);

        Draw(font, 690, 534, white, 0.52f,
             "TRIANGLE: page  START: exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_wait_rendering_done();

    if (global_b_texture) {
        vita2d_free_texture(global_b_texture);
        global_b_texture = nullptr;
    }

    for (std::size_t i = 0;
         i < TpkMetadata::kMaxTextures;
         ++i) {
        if (global_a_textures[i])
            vita2d_free_texture(global_a_textures[i]);
    }

    vita2d_free_pgf(font);
    vita2d_fini();

    const bool base_ok =
        crc_pass &&
        global_a.found &&
        global_a.valid &&
        tpk.valid &&
        global_a_loaded_count ==
            tpk.displayed_textures &&
        global_a_loaded_count > 0;

    const bool global_b_ok =
        !global_b_jdlz.found ||
        (global_b_jdlz.valid &&
         global_b.found &&
         global_b.valid &&
         global_b_tpk.valid &&
         global_b_tpk.displayed_packs > 0 &&
         global_b_meta.valid);

    FreeJdlzMemory(global_b_jdlz);

    const bool ok = base_ok && global_b_ok;

    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
