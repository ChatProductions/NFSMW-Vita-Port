#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache="${M12_SOURCE_CACHE:-$root/build/source-cache}"
version="9.0.1"
archive="ffmpeg-$version.tar.xz"
sha256="cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635"
url="https://ffmpeg.org/releases/$archive"
source="$cache/ffmpeg-$version"
prefix="$root/build/deps/ffmpeg-vp6-vita"
build="$root/build/deps/ffmpeg-vp6-vita-build"
vitasdk="${VITASDK:?VITASDK must be set}"
stamp="$prefix/.m12-vp6-build"
config_id="ffmpeg-$version-vita-ea-vp6-v1"

mkdir -p "$cache" "$root/build/deps"
if [[ ! -f "$cache/$archive" ]]; then
  curl --fail --location --retry 3 --output "$cache/$archive" "$url"
fi
printf '%s  %s\n' "$sha256" "$cache/$archive" | sha256sum --check

if [[ -f "$stamp" ]] && [[ "$(cat "$stamp")" == "$config_id" ]] && \
   [[ -f "$prefix/lib/libavformat.a" ]] && \
   [[ -f "$prefix/lib/libavcodec.a" ]] && \
   [[ -f "$prefix/lib/libavutil.a" ]] && \
   [[ -f "$prefix/lib/libswscale.a" ]]; then
  echo "M12 VP6 FFmpeg already built: $prefix"
  exit 0
fi

if [[ ! -x "$source/configure" ]]; then
  rm -rf "$source"
  tar -xJf "$cache/$archive" -C "$cache"
fi

rm -rf "$build" "$prefix"
mkdir -p "$build" "$prefix"
cd "$build"

"$source/configure" \
  --prefix="$prefix" \
  --enable-cross-compile \
  --cross-prefix="$vitasdk/bin/arm-vita-eabi-" \
  --arch=armv7-a \
  --cpu=cortex-a9 \
  --target-os=none \
  --disable-shared \
  --enable-static \
  --disable-programs \
  --disable-doc \
  --disable-avdevice \
  --disable-avfilter \
  --disable-network \
  --disable-autodetect \
  --disable-runtime-cpudetect \
  --disable-armv5te \
  --disable-armv6t2 \
  --disable-everything \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --enable-swscale \
  --enable-demuxer=ea \
  --enable-decoder=vp6 \
  --enable-protocol=file \
  --enable-pthreads \
  --disable-small \
  --optflags=-O3 \
  --disable-debug \
  --disable-bzlib \
  --disable-iconv \
  --disable-lzma \
  --disable-sdl2 \
  --disable-securetransport \
  --disable-xlib \
  --extra-cflags="-std=gnu11 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O3 -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -D_BSD_SOURCE" \
  --extra-ldflags="-L$vitasdk/arm-vita-eabi/lib -Wl,--gc-sections"

make -j"$(nproc)"
make install

test -f "$prefix/lib/libavformat.a"
test -f "$prefix/lib/libavcodec.a"
test -f "$prefix/lib/libavutil.a"
test -f "$prefix/lib/libswscale.a"
"$vitasdk/bin/arm-vita-eabi-nm" "$prefix/lib/libavcodec.a" | grep -q "ff_vp6_decoder"
printf '%s\n' "$config_id" > "$stamp"

echo "M12 VP6 FFmpeg installed at: $prefix"
