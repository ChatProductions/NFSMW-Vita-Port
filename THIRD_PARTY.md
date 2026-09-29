# Source origins

This project is a clean integration workspace.

## NFSMW-VishDec

Initial imported original-game routine:

- `src/decomp/bCrc32.cpp`
- Original path: `src/Speed/Indep/bWare/Src/bCrc32.cpp`
- Source repository: `ChatProductions/NFSMW-VishDec`
- Upstream: `dbalatoni13/nfsmw`
- Upstream license: CC0-1.0

The function body is kept unchanged for Milestone 0 so the first ARM/Vita test proves that recovered NFSMW code itself can execute on Vita.

## NFSMW-Vish

Milestone 10 imports the MIT-licensed Vita gameplay/runtime implementation under `runtime/vish/` as the executable foundation: platform, input, audio/video, frontend shell, driving loop, streaming shell, HUD, map, traffic, and renderer. Runtime paths are consolidated under `ux0:data/nfsmw`.

This is no longer reference-only use. The local license copy is `third_party/VISH-LICENSE.txt`.

The direct retail-data loaders proven in M0-M8 remain in this repository and will replace Vish's prepared/custom asset inputs subsystem by subsystem.

## nfsmw-nx

Used only as an engineering/performance reference at Milestone 0. No NX source code is included yet.


## MWSDK

Milestone 5 adapts the JDLZ decompression algorithm from:

- Source repository: `TsyVM/MWSDK`
- Source file: `src/jdlz.cpp`
- License: MIT
- Copyright: 2026 MWSDK contributors
- Local license copy: `third_party/MWSDK-LICENSE.txt`

The Vita integration adds file I/O, allocation limits, validation, and cache output around the documented JDLZ decoder.
