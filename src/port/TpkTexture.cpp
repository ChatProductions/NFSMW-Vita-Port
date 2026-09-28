#include "port/TpkTexture.h"
#include "port/TpkMetadata.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {

constexpr std::uint32_t kArgb32 = 0x00000015u;
constexpr std::uint32_t kP8 = 0x00000029u;
constexpr std::uint32_t kDxt1 = 0x31545844u; // "DXT1"
constexpr std::uint32_t kDxt3 = 0x33545844u; // "DXT3"
constexpr std::uint32_t kDxt5 = 0x35545844u; // "DXT5"

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

void SetResult(TpkTextureLoadResult *out, TpkTextureLoadResult value) {
    if (out)
        *out = value;
}

Rgba Decode565(std::uint16_t c) {
    const std::uint8_t r5 =
        static_cast<std::uint8_t>((c >> 11) & 0x1Fu);
    const std::uint8_t g6 =
        static_cast<std::uint8_t>((c >> 5) & 0x3Fu);
    const std::uint8_t b5 =
        static_cast<std::uint8_t>(c & 0x1Fu);

    return {
        static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
        static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
        static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
        255
    };
}

Rgba Lerp(const Rgba &a,
          const Rgba &b,
          unsigned wa,
          unsigned wb,
          unsigned denom) {
    return {
        static_cast<std::uint8_t>((wa * a.r + wb * b.r) / denom),
        static_cast<std::uint8_t>((wa * a.g + wb * b.g) / denom),
        static_cast<std::uint8_t>((wa * a.b + wb * b.b) / denom),
        255
    };
}

void BuildColorPalette(const std::uint8_t *block,
                       bool dxt1,
                       Rgba colors[4]) {
    const std::uint16_t c0 =
        static_cast<std::uint16_t>(
            block[0] | (static_cast<std::uint16_t>(block[1]) << 8));
    const std::uint16_t c1 =
        static_cast<std::uint16_t>(
            block[2] | (static_cast<std::uint16_t>(block[3]) << 8));

    colors[0] = Decode565(c0);
    colors[1] = Decode565(c1);

    if (dxt1 && c0 <= c1) {
        colors[2] = Lerp(colors[0], colors[1], 1, 1, 2);
        colors[3] = {0, 0, 0, 0};
    } else {
        colors[2] = Lerp(colors[0], colors[1], 2, 1, 3);
        colors[3] = Lerp(colors[0], colors[1], 1, 2, 3);
    }
}

void WritePixel(std::uint8_t *dst,
                unsigned stride,
                unsigned x,
                unsigned y,
                const Rgba &p) {
    std::uint8_t *d =
        dst + static_cast<std::size_t>(y) * stride +
        static_cast<std::size_t>(x) * 4u;

    d[0] = p.r;
    d[1] = p.g;
    d[2] = p.b;
    d[3] = p.a;
}

bool DecodeDxt(const std::uint8_t *src,
               std::size_t src_size,
               std::uint32_t format,
               unsigned width,
               unsigned height,
               std::uint8_t *dst,
               unsigned stride) {
    const unsigned block_bytes =
        (format == kDxt1) ? 8u : 16u;

    const unsigned blocks_x =
        std::max(1u, (width + 3u) / 4u);
    const unsigned blocks_y =
        std::max(1u, (height + 3u) / 4u);

    const std::size_t required =
        static_cast<std::size_t>(blocks_x) *
        blocks_y * block_bytes;

    if (src_size < required)
        return false;

    for (unsigned by = 0; by < blocks_y; ++by) {
        for (unsigned bx = 0; bx < blocks_x; ++bx) {
            const std::uint8_t *block =
                src +
                (static_cast<std::size_t>(by) * blocks_x + bx) *
                    block_bytes;

            Rgba colors[4]{};
            const std::uint8_t *color_block =
                block + ((format == kDxt1) ? 0u : 8u);

            BuildColorPalette(
                color_block,
                format == kDxt1,
                colors);

            const std::uint32_t color_bits =
                static_cast<std::uint32_t>(color_block[4]) |
                (static_cast<std::uint32_t>(color_block[5]) << 8) |
                (static_cast<std::uint32_t>(color_block[6]) << 16) |
                (static_cast<std::uint32_t>(color_block[7]) << 24);

            std::uint8_t alpha_palette[8]{};
            std::uint64_t alpha_bits = 0;

            if (format == kDxt5) {
                const std::uint8_t a0 = block[0];
                const std::uint8_t a1 = block[1];

                alpha_palette[0] = a0;
                alpha_palette[1] = a1;

                if (a0 > a1) {
                    alpha_palette[2] =
                        static_cast<std::uint8_t>((6u * a0 + a1) / 7u);
                    alpha_palette[3] =
                        static_cast<std::uint8_t>((5u * a0 + 2u * a1) / 7u);
                    alpha_palette[4] =
                        static_cast<std::uint8_t>((4u * a0 + 3u * a1) / 7u);
                    alpha_palette[5] =
                        static_cast<std::uint8_t>((3u * a0 + 4u * a1) / 7u);
                    alpha_palette[6] =
                        static_cast<std::uint8_t>((2u * a0 + 5u * a1) / 7u);
                    alpha_palette[7] =
                        static_cast<std::uint8_t>((a0 + 6u * a1) / 7u);
                } else {
                    alpha_palette[2] =
                        static_cast<std::uint8_t>((4u * a0 + a1) / 5u);
                    alpha_palette[3] =
                        static_cast<std::uint8_t>((3u * a0 + 2u * a1) / 5u);
                    alpha_palette[4] =
                        static_cast<std::uint8_t>((2u * a0 + 3u * a1) / 5u);
                    alpha_palette[5] =
                        static_cast<std::uint8_t>((a0 + 4u * a1) / 5u);
                    alpha_palette[6] = 0;
                    alpha_palette[7] = 255;
                }

                for (unsigned i = 0; i < 6; ++i) {
                    alpha_bits |=
                        static_cast<std::uint64_t>(block[2 + i]) <<
                        (8u * i);
                }
            }

            for (unsigned py = 0; py < 4; ++py) {
                for (unsigned px = 0; px < 4; ++px) {
                    const unsigned x = bx * 4u + px;
                    const unsigned y = by * 4u + py;

                    if (x >= width || y >= height)
                        continue;

                    const unsigned texel = py * 4u + px;
                    const unsigned color_index =
                        (color_bits >> (2u * texel)) & 0x3u;

                    Rgba pixel = colors[color_index];

                    if (format == kDxt3) {
                        const unsigned byte_index = texel / 2u;
                        const unsigned shift =
                            (texel & 1u) ? 4u : 0u;

                        const std::uint8_t a4 =
                            static_cast<std::uint8_t>(
                                (block[byte_index] >> shift) & 0x0Fu);

                        pixel.a =
                            static_cast<std::uint8_t>(
                                (a4 << 4) | a4);
                    } else if (format == kDxt5) {
                        const unsigned alpha_index =
                            static_cast<unsigned>(
                                (alpha_bits >> (3u * texel)) & 0x7u);

                        pixel.a = alpha_palette[alpha_index];
                    }

                    WritePixel(dst, stride, x, y, pixel);
                }
            }
        }
    }

    return true;
}

bool ValidateEncodedSize(const TpkTextureMetadata &t,
                         std::size_t &required) {
    if (t.width == 0 || t.height == 0 || t.base_size == 0)
        return false;

    if (t.format == kArgb32) {
        required =
            static_cast<std::size_t>(t.width) *
            t.height * 4u;
    } else if (t.format == kDxt1 ||
               t.format == kDxt3 ||
               t.format == kDxt5) {
        const unsigned block_bytes =
            (t.format == kDxt1) ? 8u : 16u;

        const unsigned blocks_x =
            std::max(
                1u,
                (static_cast<unsigned>(t.width) + 3u) / 4u);

        const unsigned blocks_y =
            std::max(
                1u,
                (static_cast<unsigned>(t.height) + 3u) / 4u);

        required =
            static_cast<std::size_t>(blocks_x) *
            blocks_y * block_bytes;
    } else {
        return false;
    }

    return t.base_size >= required;
}

vita2d_texture *CreateTextureFromEncoded(
    const std::uint8_t *src,
    std::size_t src_size,
    const TpkTextureMetadata &t,
    TpkTextureLoadResult *result) {

    if (!src) {
        SetResult(result, TpkTextureLoadResult::InvalidMetadata);
        return nullptr;
    }

    if (t.format == kP8) {
        SetResult(result, TpkTextureLoadResult::UnsupportedFormat);
        return nullptr;
    }

    std::size_t required = 0;

    if (!ValidateEncodedSize(t, required)) {
        const bool known_format =
            t.format == kArgb32 ||
            t.format == kDxt1 ||
            t.format == kDxt3 ||
            t.format == kDxt5;

        SetResult(
            result,
            known_format
                ? TpkTextureLoadResult::InvalidSize
                : TpkTextureLoadResult::UnsupportedFormat);

        return nullptr;
    }

    if (src_size < required) {
        SetResult(result, TpkTextureLoadResult::InvalidSize);
        return nullptr;
    }

    vita2d_texture *texture =
        vita2d_create_empty_texture(t.width, t.height);

    if (!texture) {
        SetResult(result, TpkTextureLoadResult::OutOfMemory);
        return nullptr;
    }

    auto *dst =
        static_cast<std::uint8_t *>(
            vita2d_texture_get_datap(texture));

    const unsigned int stride =
        vita2d_texture_get_stride(texture);

    bool ok = true;

    if (t.format == kArgb32) {
        for (std::uint32_t y = 0; y < t.height; ++y) {
            const std::uint8_t *srow =
                src +
                static_cast<std::size_t>(y) *
                    t.width * 4u;

            std::uint8_t *drow =
                dst +
                static_cast<std::size_t>(y) *
                    stride;

            for (std::uint32_t x = 0; x < t.width; ++x) {
                const std::uint8_t b = srow[x * 4u + 0u];
                const std::uint8_t g = srow[x * 4u + 1u];
                const std::uint8_t r = srow[x * 4u + 2u];
                const std::uint8_t a = srow[x * 4u + 3u];

                drow[x * 4u + 0u] = r;
                drow[x * 4u + 1u] = g;
                drow[x * 4u + 2u] = b;
                drow[x * 4u + 3u] = a;
            }
        }
    } else {
        ok = DecodeDxt(
            src,
            src_size,
            t.format,
            t.width,
            t.height,
            dst,
            stride);
    }

    if (!ok) {
        vita2d_free_texture(texture);
        SetResult(result, TpkTextureLoadResult::InvalidSize);
        return nullptr;
    }

    SetResult(result, TpkTextureLoadResult::Ok);
    return texture;
}

} // namespace

const char *DescribeTpkTextureLoadResult(
    TpkTextureLoadResult result) {
    switch (result) {
        case TpkTextureLoadResult::Ok:
            return "OK";
        case TpkTextureLoadResult::InvalidMetadata:
            return "invalid metadata";
        case TpkTextureLoadResult::UnsupportedFormat:
            return "unsupported format (P8 palette unresolved)";
        case TpkTextureLoadResult::InvalidSize:
            return "invalid texture size";
        case TpkTextureLoadResult::IoError:
            return "I/O error";
        case TpkTextureLoadResult::OutOfMemory:
            return "out of memory";
    }

    return "unknown";
}

vita2d_texture *LoadTpkTextureBaseLevel(
    const char *path,
    const TpkMetadata &meta,
    std::size_t texture_index,
    TpkTextureLoadResult *result) {

    SetResult(result, TpkTextureLoadResult::InvalidMetadata);

    if (!meta.valid ||
        texture_index >= meta.displayed_textures) {
        return nullptr;
    }

    const TpkTextureMetadata &t =
        meta.textures[texture_index];

    if (static_cast<std::uint64_t>(t.data_offset) +
            t.base_size >
        meta.data_blob_size) {
        SetResult(result, TpkTextureLoadResult::InvalidSize);
        return nullptr;
    }

    FILE *f = std::fopen(path, "rb");

    if (!f) {
        SetResult(result, TpkTextureLoadResult::IoError);
        return nullptr;
    }

    const std::uint32_t absolute_offset =
        meta.data_blob_offset + t.data_offset;

    if (std::fseek(
            f,
            static_cast<long>(absolute_offset),
            SEEK_SET) != 0) {
        std::fclose(f);
        SetResult(result, TpkTextureLoadResult::IoError);
        return nullptr;
    }

    auto *src =
        static_cast<std::uint8_t *>(
            std::malloc(t.base_size));

    if (!src) {
        std::fclose(f);
        SetResult(result, TpkTextureLoadResult::OutOfMemory);
        return nullptr;
    }

    if (std::fread(src, 1, t.base_size, f) != t.base_size) {
        std::free(src);
        std::fclose(f);
        SetResult(result, TpkTextureLoadResult::IoError);
        return nullptr;
    }

    std::fclose(f);

    vita2d_texture *texture =
        CreateTextureFromEncoded(
            src,
            t.base_size,
            t,
            result);

    std::free(src);
    return texture;
}

vita2d_texture *LoadTpkTextureBaseLevelMemory(
    const void *data_ptr,
    std::uint32_t data_size,
    const TpkMetadata &meta,
    std::size_t texture_index,
    TpkTextureLoadResult *result) {

    SetResult(result, TpkTextureLoadResult::InvalidMetadata);

    if (!data_ptr ||
        !meta.valid ||
        texture_index >= meta.displayed_textures) {
        return nullptr;
    }

    const TpkTextureMetadata &t =
        meta.textures[texture_index];

    const std::uint64_t absolute_offset =
        static_cast<std::uint64_t>(meta.data_blob_offset) +
        t.data_offset;

    const std::uint64_t end =
        absolute_offset + t.base_size;

    if (absolute_offset > data_size ||
        end > data_size) {
        SetResult(result, TpkTextureLoadResult::InvalidSize);
        return nullptr;
    }

    const auto *data =
        static_cast<const std::uint8_t *>(data_ptr);

    return CreateTextureFromEncoded(
        data + absolute_offset,
        t.base_size,
        t,
        result);
}
