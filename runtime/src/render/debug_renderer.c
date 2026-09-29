#include "renderer.h"
#include "debugScreen.h"
int renderer_init(void) { return psvDebugScreenInit(); }
void renderer_status(AssetResult result, unsigned checks) {
    psvDebugScreenPrintf("\033[2J\033[H");
    psvDebugScreenPrintf("NFSMW-VITA | M0 BOOTSTRAP | 0.0.1\n\n");
    psvDebugScreenPrintf("VITA NATIVE APP RUNNING\n");
    psvDebugScreenPrintf("RENDERER: VITASDK DEBUG FRAMEBUFFER\n\n");
    psvDebugScreenPrintf("ASSET ROOT:\n%s\n\n", NFSMW_ASSET_ROOT);
    psvDebugScreenPrintf("PROBE: GLOBAL/GLOBALB.LZC (FIRST 16 BYTES)\n%s\n", asset_result_string(result));
    psvDebugScreenPrintf("\nCHECKS: %u\n\n", checks);
    psvDebugScreenPrintf("CROSS: CHECK AGAIN     START: EXIT\n\n");
    psvDebugScreenPrintf("NO GAME ENGINE OR PROPRIETARY ASSETS INCLUDED.\n");
}
void renderer_shutdown(void) { psvDebugScreenFinish(); }
