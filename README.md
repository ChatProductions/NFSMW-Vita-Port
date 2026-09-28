# NFSMW Vita Port

Experimental PlayStation Vita porting project for Need for Speed: Most Wanted (2005).

This repository is intentionally separate from the three reference forks:

- `ChatProductions/NFSMW-Vish`: Vita-specific implementation and VitaSDK reference.
- `ChatProductions/NFSMW-VishDec`: matching decompilation used as the source of original game logic.
- `ChatProductions/nfsmw-nx`: Switch recompilation/port used as an engineering and performance reference.

## Milestone 0

The first milestone is deliberately small:

1. Build a clean VitaSDK VPK.
2. Compile a genuinely original, platform-independent NFSMW routine from VishDec for ARM.
3. Execute it on the Vita.
4. Show PASS/FAIL on screen and exit cleanly.

The initial routine is `bCalculateCrc32` from the original bWare layer. It has no platform dependencies, making it a useful first proof that recovered NFSMW code can compile and execute inside a native Vita application.

No original game assets are stored in this repository.
