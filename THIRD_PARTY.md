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

Used as a VitaSDK/build-system reference. Milestone 0 does not copy its game-reimplementation modules.

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
