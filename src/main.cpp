#include "decomp/bCrc32.h"
#include "port/BundleProbe.h"
#include "port/JdlzFile.h"
#include "port/GeometryFile.h"
#include "port/TpkInventory.h"
#include "port/TpkMetadata.h"
#include "port/TpkTexture.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vita2d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr unsigned int kExpected = 0x66F9D7D0u;
static constexpr const char *kProbe = "NFSMW-Vita-Port";
static constexpr const char *kGlobalAPath = "ux0:data/nfsmw/GLOBALA.BUN";
static constexpr const char *kGlobalBPath = "ux0:data/nfsmw/GlobalB.lzc";
static constexpr const char *kGeometryPath =
    "ux0:data/nfsmw/CARS/COBALTSS/GEOMETRY.BIN";

static void Draw(vita2d_pgf *font,
                 int x,
                 int y,
                 unsigned int color,
                 float scale,
                 const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}


static void WriteGlobalBDiagnosticLog(
    const TpkInventory &inventory,
    const JdlzMemoryResult &jdlz,
    const void *global_b_data,
    std::uint32_t global_b_size) {

    FILE *f = std::fopen("ux0:data/nfsmw/logs/globalb-m8.log", "w");
    if (!f)
        return;

    std::fprintf(f, "NFSMW Vita Port - Milestone 8 GlobalB diagnostics\n");
    std::fprintf(f, "GlobalB JDLZ: %s\n", jdlz.valid ? "VALID" : "INVALID");
    std::fprintf(f, "TPK inventory: %s\n", inventory.valid ? "VALID" : "INVALID");
    std::fprintf(f, "Packs: %u\n\n", inventory.pack_count_total);

    unsigned total_textures = 0;
    unsigned p8_textures = 0;
    unsigned p8_palettes_ready = 0;
    unsigned p8_palettes_missing = 0;
    unsigned self_contained_textures = 0;
    unsigned metadata_failures = 0;

    if (inventory.valid && jdlz.valid && global_b_data) {
        for (std::size_t p = 0; p < inventory.displayed_packs; ++p) {
            const TpkPackSummary &pack = inventory.packs[p];
            const TpkMetadata meta = ReadTpkMetadataMemory(
                global_b_data,
                global_b_size,
                pack.container_offset,
                pack.container_size);

            std::fprintf(f,
                         "PACK %u/%u: %s\n",
                         static_cast<unsigned>(p + 1),
                         static_cast<unsigned>(inventory.displayed_packs),
                         pack.source_path[0] ? pack.source_path : pack.name);

            if (!meta.valid) {
                ++metadata_failures;
                std::fprintf(f, "  metadata INVALID\n\n");
                continue;
            }

            std::fprintf(f,
                         "  version=%u textures=%u\n",
                         meta.version,
                         meta.texture_count);

            for (std::size_t i = 0; i < meta.displayed_textures; ++i) {
                const TpkTextureMetadata &t = meta.textures[i];
                char fmt[16];
                const bool p8 = t.format == 0x29u;

                ++total_textures;

                const bool p8_palette_ready =
                    p8 &&
                    t.palette_size >= 256u * 4u &&
                    static_cast<std::uint64_t>(t.palette_offset) +
                            256u * 4u <=
                        meta.data_blob_size;

                if (p8) {
                    ++p8_textures;
                    if (p8_palette_ready)
                        ++p8_palettes_ready;
                    else
                        ++p8_palettes_missing;
                } else {
                    ++self_contained_textures;
                }

                std::fprintf(
                    f,
                    "  %3u/%3u %-24s %4ux%-4u %-8s data=0x%08X base=%u total=%u pal=0x%08X/%u mips=%u %s\n",
                    static_cast<unsigned>(i + 1),
                    static_cast<unsigned>(meta.displayed_textures),
                    t.name,
                    static_cast<unsigned>(t.width),
                    static_cast<unsigned>(t.height),
                    DescribeTpkFormat(t.format, fmt, sizeof(fmt)),
                    t.data_offset,
                    t.base_size,
                    t.total_size,
                    t.palette_offset,
                    t.palette_size,
                    static_cast<unsigned>(t.mip_count),
                    p8
                        ? (p8_palette_ready
                               ? "P8_PALETTE_READY"
                               : "P8_PALETTE_MISSING")
                        : "SELF_CONTAINED");
            }

            std::fprintf(f, "\n");
        }
    }

    std::fprintf(
        f,
        "SUMMARY total=%u self_contained=%u p8=%u p8_ready=%u p8_missing=%u metadata_failures=%u\n",
        total_textures,
        self_contained_textures,
        p8_textures,
        p8_palettes_ready,
        p8_palettes_missing,
        metadata_failures);

    std::fclose(f);
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


struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

static ScreenPoint ProjectGeometryVertex(
    const GeometryVertex &v,
    const float center[3],
    float inv_extent,
    float cos_yaw,
    float sin_yaw,
    float cos_pitch,
    float sin_pitch,
    float zoom) {

    const float x = (v.x - center[0]) * inv_extent;
    const float y = (v.y - center[1]) * inv_extent;
    const float z = (v.z - center[2]) * inv_extent;

    const float rx =
        cos_yaw * x - sin_yaw * y;
    const float ry =
        sin_yaw * x + cos_yaw * y;

    const float py =
        cos_pitch * ry - sin_pitch * z;
    const float pz =
        sin_pitch * ry + cos_pitch * z;

    const float depth = 3.4f - py;

    if (depth <= 0.25f)
        return {};

    const float focal = 560.0f * zoom / depth;

    ScreenPoint out{};
    out.x = 480.0f + rx * focal;
    out.y = 292.0f - pz * focal;
    out.visible =
        out.x > -100.0f && out.x < 1060.0f &&
        out.y > 70.0f && out.y < 520.0f;

    return out;
}

int main() {
    // Keep generated diagnostics with the game data instead of scattering
    // them across ux0:data.
    sceIoMkdir("ux0:data/nfsmw", 0777);
    sceIoMkdir("ux0:data/nfsmw/logs", 0777);

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

        WriteGlobalBDiagnosticLog(
            global_b_tpk,
            global_b_jdlz,
            global_b_jdlz.data,
            global_b_jdlz.decompressed_size);
    }

    std::size_t global_b_pack_index = 0;
    std::size_t global_b_texture_index = 0;

    TpkMetadata global_b_meta{};

    const GeometryIndex geometry_index =
        ReadGeometryIndex(kGeometryPath);

    std::size_t geometry_selected = 0;
    GeometryMesh geometry_mesh{};

    if (geometry_index.valid &&
        geometry_index.displayed_objects > 0) {
        geometry_mesh =
            LoadGeometryObject(
                kGeometryPath,
                geometry_index,
                geometry_selected);
    }

    WriteGeometryLog(
        kGeometryPath,
        geometry_index,
        geometry_selected,
        geometry_mesh);

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
    float geometry_yaw = 0.85f;
    float geometry_pitch = -0.35f;
    float geometry_zoom = 1.0f;
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


    auto reload_geometry = [&]() {
        geometry_mesh = {};

        if (!geometry_index.valid ||
            geometry_index.displayed_objects == 0)
            return;

        if (geometry_selected >=
            geometry_index.displayed_objects)
            geometry_selected = 0;

        geometry_mesh =
            LoadGeometryObject(
                kGeometryPath,
                geometry_index,
                geometry_selected);

        WriteGeometryLog(
            kGeometryPath,
            geometry_index,
            geometry_selected,
            geometry_mesh);
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
            page = (page + 1u) % 5u;

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


        if (page == 4 &&
            geometry_index.valid &&
            geometry_index.displayed_objects > 0) {
            bool reload = false;

            if (pressed & SCE_CTRL_RIGHT) {
                geometry_selected =
                    (geometry_selected + 1) %
                    geometry_index.displayed_objects;
                reload = true;
            }

            if (pressed & SCE_CTRL_LEFT) {
                geometry_selected =
                    (geometry_selected +
                     geometry_index.displayed_objects - 1) %
                    geometry_index.displayed_objects;
                reload = true;
            }

            if (pressed & SCE_CTRL_DOWN) {
                geometry_selected =
                    std::min(
                        geometry_selected + 10,
                        geometry_index.displayed_objects - 1);
                reload = true;
            }

            if (pressed & SCE_CTRL_UP) {
                geometry_selected =
                    geometry_selected >= 10
                        ? geometry_selected - 10
                        : 0;
                reload = true;
            }

            if (reload)
                reload_geometry();

            const int analog_x =
                static_cast<int>(pad.lx) - 128;
            const int analog_y =
                static_cast<int>(pad.ly) - 128;

            if (std::abs(analog_x) > 18)
                geometry_yaw +=
                    static_cast<float>(analog_x) /
                    128.0f * 0.035f;

            if (std::abs(analog_y) > 18)
                geometry_pitch +=
                    static_cast<float>(analog_y) /
                    128.0f * 0.028f;

            geometry_pitch =
                std::max(
                    -1.20f,
                    std::min(1.20f, geometry_pitch));

            if (pad.buttons & SCE_CTRL_RTRIGGER)
                geometry_zoom =
                    std::min(2.4f, geometry_zoom * 1.018f);

            if (pad.buttons & SCE_CTRL_LTRIGGER)
                geometry_zoom =
                    std::max(0.45f, geometry_zoom / 1.018f);
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
             "NFSMW Vita Port - Milestone 9");

        char line[224];

        if (page == 0) {
            Draw(font, 720, 38, gray, 0.58f,
                 "1/5 GLOBALA textures");

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
                 "2/5 GlobalB JDLZ");

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
                 "3/5 GlobalB TPK index");

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
        } else if (page == 3) {
            Draw(font, 720, 38, gray, 0.58f,
                 "4/5 GlobalB texture browser");

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

        } else {
            Draw(font, 720, 38, gray, 0.58f,
                 "5/5 native geometry");

            if (!geometry_index.found) {
                Draw(font, 28, 92, amber, 0.78f,
                     "COBALTSS GEOMETRY.BIN not found");
                Draw(font, 28, 123, gray, 0.53f,
                     "Expected: ux0:data/nfsmw/CARS/COBALTSS/GEOMETRY.BIN");
            } else if (!geometry_index.valid) {
                std::snprintf(
                    line,
                    sizeof(line),
                    "Geometry index failed: %s",
                    geometry_index.error);
                Draw(font, 28, 92, red, 0.68f, line);
            } else if (!geometry_mesh.valid) {
                std::snprintf(
                    line,
                    sizeof(line),
                    "Solid %u/%u failed: %s",
                    static_cast<unsigned>(geometry_selected + 1),
                    static_cast<unsigned>(
                        geometry_index.displayed_objects),
                    geometry_mesh.error);
                Draw(font, 28, 92, red, 0.62f, line);
            } else {
                std::snprintf(
                    line,
                    sizeof(line),
                    "%u/%u  %s",
                    static_cast<unsigned>(geometry_selected + 1),
                    static_cast<unsigned>(
                        geometry_index.displayed_objects),
                    geometry_mesh.name);
                Draw(font, 28, 73, green, 0.66f, line);

                std::snprintf(
                    line,
                    sizeof(line),
                    "%u vertices | %u triangles | %u groups",
                    static_cast<unsigned>(
                        geometry_mesh.vertices.size()),
                    geometry_mesh.num_tris,
                    geometry_mesh.group_count);
                Draw(font, 28, 98, gray, 0.55f, line);

                float center[3]{};
                float max_extent = 0.001f;

                for (int axis = 0; axis < 3; ++axis) {
                    center[axis] =
                        (geometry_mesh.bbox_min[axis] +
                         geometry_mesh.bbox_max[axis]) *
                        0.5f;

                    max_extent =
                        std::max(
                            max_extent,
                            (geometry_mesh.bbox_max[axis] -
                             geometry_mesh.bbox_min[axis]) *
                                0.5f);
                }

                const float inv_extent =
                    1.0f / max_extent;

                const float cos_yaw =
                    std::cos(geometry_yaw);
                const float sin_yaw =
                    std::sin(geometry_yaw);
                const float cos_pitch =
                    std::cos(geometry_pitch);
                const float sin_pitch =
                    std::sin(geometry_pitch);

                const std::size_t total_triangles =
                    geometry_mesh.indices.size() / 3u;

                constexpr std::size_t kMaxWireTriangles = 1800;

                const std::size_t step =
                    std::max<std::size_t>(
                        1,
                        (total_triangles +
                         kMaxWireTriangles - 1) /
                            kMaxWireTriangles);

                std::size_t drawn = 0;

                for (std::size_t tri = 0;
                     tri < total_triangles;
                     tri += step) {
                    const std::uint32_t ia =
                        geometry_mesh.indices[tri * 3u + 0u];
                    const std::uint32_t ib =
                        geometry_mesh.indices[tri * 3u + 1u];
                    const std::uint32_t ic =
                        geometry_mesh.indices[tri * 3u + 2u];

                    if (ia >= geometry_mesh.vertices.size() ||
                        ib >= geometry_mesh.vertices.size() ||
                        ic >= geometry_mesh.vertices.size())
                        continue;

                    const ScreenPoint a =
                        ProjectGeometryVertex(
                            geometry_mesh.vertices[ia],
                            center,
                            inv_extent,
                            cos_yaw,
                            sin_yaw,
                            cos_pitch,
                            sin_pitch,
                            geometry_zoom);

                    const ScreenPoint b =
                        ProjectGeometryVertex(
                            geometry_mesh.vertices[ib],
                            center,
                            inv_extent,
                            cos_yaw,
                            sin_yaw,
                            cos_pitch,
                            sin_pitch,
                            geometry_zoom);

                    const ScreenPoint d =
                        ProjectGeometryVertex(
                            geometry_mesh.vertices[ic],
                            center,
                            inv_extent,
                            cos_yaw,
                            sin_yaw,
                            cos_pitch,
                            sin_pitch,
                            geometry_zoom);

                    if (a.visible && b.visible)
                        vita2d_draw_line(
                            a.x, a.y, b.x, b.y, white);
                    if (b.visible && d.visible)
                        vita2d_draw_line(
                            b.x, b.y, d.x, d.y, white);
                    if (d.visible && a.visible)
                        vita2d_draw_line(
                            d.x, d.y, a.x, a.y, white);

                    ++drawn;
                }

                std::snprintf(
                    line,
                    sizeof(line),
                    "wire tris %u/%u | bbox %.2f %.2f %.2f",
                    static_cast<unsigned>(drawn),
                    static_cast<unsigned>(total_triangles),
                    geometry_mesh.bbox_max[0] -
                        geometry_mesh.bbox_min[0],
                    geometry_mesh.bbox_max[1] -
                        geometry_mesh.bbox_min[1],
                    geometry_mesh.bbox_max[2] -
                        geometry_mesh.bbox_min[2]);

                Draw(font, 28, 468, gray, 0.52f, line);

                Draw(font, 28, 492, gray, 0.50f,
                     "ANALOG rotate | L/R zoom | D-PAD solid (UP/DOWN +/-10)");
            }

        }

        std::snprintf(
            line,
            sizeof(line),
            "CRC %s | GA %s | GB %s | TPK %s | GEO %s",
            crc_pass ? "PASS" : "FAIL",
            global_a.valid ? "VALID" : "INVALID",
            global_b.valid ? "VALID" :
                (global_b_jdlz.found ? "INVALID" : "N/A"),
            global_b_tpk.valid ? "VALID" : "N/A",
            geometry_index.valid
                ? (geometry_mesh.valid ? "VALID" : "ERR")
                : (geometry_index.found ? "ERR" : "N/A"));

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
