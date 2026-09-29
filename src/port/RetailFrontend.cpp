#include "port/RetailFrontend.h"
#include "port/JdlzFile.h"
#include "port/TpkInventory.h"
#include "port/TpkMetadata.h"
#include "port/TpkTexture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

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

static bool LoadFile(const char *path, std::vector<std::uint8_t> &out) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long n = std::ftell(f);
    if (n <= 0 || static_cast<unsigned long>(n) > 96u * 1024u * 1024u) {
        std::fclose(f);
        return false;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

static bool NameMatches(const char *name, const char *wanted) {
    return name && wanted && std::strcmp(name, wanted) == 0;
}

struct Candidate {
    vita2d_texture *texture = nullptr;
    char name[64]{};
    std::uint64_t score = 0;
};

static bool TryBundleMemory(const void *data,
                            std::uint32_t size,
                            const char *tag,
                            Candidate &best) {
    const TpkInventory inv = ScanTpkPacksMemory(data, size);
    Log("%s TPK inventory: valid=%d packs=%u shown=%u",
        tag, inv.valid ? 1 : 0, inv.pack_count_total,
        static_cast<unsigned>(inv.displayed_packs));

    if (!inv.valid)
        return false;

    static const char *preferred[] = {
        "WS_MWSPLASHBACK",
        "MW_GRIT_01",
        "DEMO_SPLASH",
        "FE_GRAY1B",
        "MW_GRIT_02"
    };

    bool saw_valid = false;
    for (std::size_t p = 0; p < inv.displayed_packs; ++p) {
        const TpkPackSummary &pack = inv.packs[p];
        if (pack.compressed_entries) {
            Log("%s pack %s skipped: compressed texture table", tag, pack.name);
            continue;
        }

        const TpkMetadata meta =
            ReadTpkMetadataMemory(
                data, size, pack.container_offset, pack.container_size);
        if (!meta.valid) {
            Log("%s pack %s metadata invalid", tag, pack.name);
            continue;
        }
        saw_valid = true;
        Log("%s pack %s textures=%u", tag, meta.pack_name, meta.texture_count);

        for (std::size_t i = 0; i < meta.displayed_textures; ++i) {
            const TpkTextureMetadata &t = meta.textures[i];
            std::uint64_t score =
                static_cast<std::uint64_t>(t.width) * t.height;

            for (std::size_t pref = 0;
                 pref < sizeof(preferred) / sizeof(preferred[0]);
                 ++pref) {
                if (NameMatches(t.name, preferred[pref])) {
                    score += 1000000000ull -
                             static_cast<std::uint64_t>(pref) * 10000000ull;
                    break;
                }
            }

            if (t.width < 256 || t.height < 128)
                continue;
            if (score <= best.score)
                continue;

            TpkTextureLoadResult result{};
            vita2d_texture *candidate =
                LoadTpkTextureBaseLevelMemory(
                    data, size, meta, i, &result);
            if (!candidate) {
                Log("%s texture %s rejected: %s",
                    tag, t.name, DescribeTpkTextureLoadResult(result));
                continue;
            }

            if (best.texture)
                vita2d_free_texture(best.texture);
            best.texture = candidate;
            best.score = score;
            std::snprintf(best.name, sizeof(best.name), "%s", t.name);
            Log("%s selected frontend texture candidate: %s %ux%u",
                tag, t.name, t.width, t.height);
        }
    }

    return saw_valid;
}

} // namespace

RetailFrontendAssets LoadRetailFrontendAssets() {
    RetailFrontendAssets out{};
    Candidate best{};

    const char *frontb_paths[] = {
        "ux0:data/nfsmw/FRONTEND/FrontB.lzc",
        "ux0:data/nfsmw/FRONTEND/FRONTB.LZC",
        "ux0:data/nfsmw/frontend/FrontB.lzc"
    };

    for (const char *path : frontb_paths) {
        FILE *probe = std::fopen(path, "rb");
        if (!probe) continue;
        std::fclose(probe);

        JdlzMemoryResult dec = DecompressJdlzFileToMemory(path);
        Log("FrontB: %s found=%d valid=%d packed=%u unpacked=%u %s",
            path, dec.found ? 1 : 0, dec.valid ? 1 : 0,
            dec.compressed_size, dec.decompressed_size,
            dec.error ? dec.error : "?");

        if (dec.valid) {
            out.frontb_ok =
                TryBundleMemory(
                    dec.data, dec.decompressed_size, "FrontB", best);
            FreeJdlzMemory(dec);
        }
        break;
    }

    const char *fronta_paths[] = {
        "ux0:data/nfsmw/FRONTEND/FrontA.bun",
        "ux0:data/nfsmw/FRONTEND/FRONTA.BUN",
        "ux0:data/nfsmw/frontend/FrontA.bun"
    };

    std::vector<std::uint8_t> raw;
    for (const char *path : fronta_paths) {
        if (!LoadFile(path, raw))
            continue;
        Log("FrontA: %s bytes=%u", path, static_cast<unsigned>(raw.size()));
        out.fronta_ok =
            TryBundleMemory(
                raw.data(), static_cast<std::uint32_t>(raw.size()),
                "FrontA", best);
        raw.clear();
        break;
    }

    out.background = best.texture;
    std::snprintf(
        out.background_name,
        sizeof(out.background_name),
        "%s",
        best.name[0] ? best.name : "none");

    Log("Frontend retail result: FrontB=%d FrontA=%d background=%s",
        out.frontb_ok ? 1 : 0,
        out.fronta_ok ? 1 : 0,
        out.background_name);
    return out;
}

void FreeRetailFrontendAssets(RetailFrontendAssets &assets) {
    if (assets.background) {
        vita2d_wait_rendering_done();
        vita2d_free_texture(assets.background);
        assets.background = nullptr;
    }
}
