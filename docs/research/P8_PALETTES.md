# P8 palette recovery notes

Milestone 8 closes the remaining texture-format gap in the retail PC 1.3 data path.

## Grounding

User-supplied `speed.exe` used for reverse engineering:

- SHA-256: `80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c`
- PE32 / x86
- image base: `0x00400000`
- timestamp: 2005-12-01

Relevant retail functions align with MWSDK's recovered PC 1.3 addresses:

- `0x00507B50` — `TexturePackChunk::LoadChunk`
- `0x004FCDB0` — streamed texture/palette pointer binding
- `0x006DDD90` — texture upload / P8 expansion path

## What the executable proves

The standard 124-byte texture entry contains two streamed regions:

- `+0x30` — texture data offset
- `+0x34` — palette data offset
- `+0x38` — texture data size
- `+0x3C` — palette data size

At `0x004FCDB0`, the engine resolves both offsets against the active streamed block:

- the texture region is assigned to runtime field `+0x70`
- the palette region is assigned to runtime field `+0x74`

At `0x006DDD90`, format `0x29` (`D3DFMT_P8`) takes a special path:

1. allocate a 32-bit output buffer;
2. read each 8-bit pixel index;
3. fetch a DWORD from `palette[index]`;
4. write the expanded DWORD into the output buffer;
5. upload the result as format `0x15` (`D3DFMT_A8R8G8B8`).

So the correct Vita path is not to invent a palette. It is to read the 1024-byte palette stream already referenced by the texture entry and expand the P8 base level to RGBA8.

## Retail GlobalB confirmation

In `Global\\GlobalTextures.tpk`, the 16 P8 textures each have:

- `palette_size = 0x400` (256 entries × 4 bytes)
- palette offsets from `0x0000` through `0x3C00` in `0x400` increments

Their pixel data begins after the palette area. This exactly matches the runtime loader behavior above.

Milestone 8 mirrors that retail expansion path and keeps the existing DXT1/DXT3/DXT5/ARGB32 decoders unchanged.
