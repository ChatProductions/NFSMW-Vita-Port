# NFSMW Vita Port

Native PlayStation Vita porting workspace for Need for Speed: Most Wanted (2005).

## Current direction

M10 changes the project from a sequence of standalone asset demonstrations into one playable runtime.

The executable now uses the proven MIT-licensed Vita runtime from **NFSMW-Vish** for the game shell: input, menus, audio/video, driving, world streaming, traffic, HUD, map and Vita rendering. The M0-M8 retail-data work is kept in-tree and continues to build separately so those runtime subsystems can be replaced with loaders for the user's original PC files instead of prepared intermediate assets.

The three reference projects have distinct roles:

- **NFSMW-Vish**: Vita platform and playable runtime foundation.
- **NFSMW-VishDec**: recovered original NFSMW game logic and structures, integrated where portable/available.
- **nfsmw-nx**: architecture and performance reference only; GPL source is not copied into this project.

MWSDK/MWEncyclopedia are used to cross-check original PC data formats.

## Data layout

Runtime data and logs are consolidated under:

```text
ux0:data/nfsmw/
```

The original PC installation contains the source assets the direct loaders are targeting, including `GLOBAL`, `CARS`, `TRACKS`, `FRONTEND`, `LANGUAGES`, movies and audio.

No commercial game assets are stored in this repository.

## Proven direct-original support

Before the M10 runtime pivot, hardware validation established:

- EAGL/bChunk parsing
- JDLZ decompression in memory
- TPK inventory and metadata
- ARGB32, DXT1, DXT3, DXT5 decoding
- P8 palette recovery and rendering from the retail data

Those are now infrastructure for the playable port, not separate end-user viewer milestones.
