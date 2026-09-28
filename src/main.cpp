#include "decomp/bCrc32.h"
#include "debugScreen.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdio>
#include <cstring>

static constexpr unsigned int kExpected = 0x66F9D7D0u;
static constexpr const char *kProbe = "NFSMW-Vita-Port";

static void write_log(unsigned int actual, bool pass) {
    FILE *f = fopen("ux0:data/nfsmw-vita-port-m0.log", "w");
    if (!f) return;

    fprintf(f, "NFSMW Vita Port - Milestone 0\n");
    fprintf(f, "Routine: bCalculateCrc32 (VishDec)\n");
    fprintf(f, "Input: %s\n", kProbe);
    fprintf(f, "Expected: %08X\n", kExpected);
    fprintf(f, "Actual:   %08X\n", actual);
    fprintf(f, "Result: %s\n", pass ? "PASS" : "FAIL");
    fclose(f);
}

int main() {
    const unsigned int actual =
        bCalculateCrc32(kProbe, static_cast<int>(strlen(kProbe)), 0);
    const bool pass = actual == kExpected;

    write_log(actual, pass);

    psvDebugScreenInit();
    psvDebugScreenPrintf("NFSMW Vita Port - Milestone 0\n\n");
    psvDebugScreenPrintf("Original VishDec code running on ARM/Vita\n\n");
    psvDebugScreenPrintf("Expected CRC: %08X\n", kExpected);
    psvDebugScreenPrintf("Actual CRC:   %08X\n\n", actual);
    psvDebugScreenPrintf("Result: %s\n\n", pass ? "PASS" : "FAIL");
    psvDebugScreenPrintf("Press START to exit\n");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceCtrlData pad{};
    while (true) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START)
            break;
        sceKernelDelayThread(16 * 1000);
    }

    // This M0 build deliberately does not initialize SceGxm/libvita2d.
    // It uses the VitaSDK debug framebuffer only, minimizing GPU involvement.
    psvDebugScreenFinish();
    sceKernelDelayThread(100 * 1000);

    sceKernelExitProcess(pass ? 0 : 1);
    return pass ? 0 : 1;
}
