#include "decomp/bCrc32.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

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

    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 18, 18, 255));
    vita2d_pgf *font = vita2d_load_default_pgf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceCtrlData pad{};
    do {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        vita2d_start_drawing();
        vita2d_clear_screen();

        const unsigned int title = RGBA8(255, 255, 255, 255);
        const unsigned int status = pass
            ? RGBA8(80, 220, 120, 255)
            : RGBA8(240, 80, 80, 255);

        vita2d_pgf_draw_text(font, 48, 80, title, 1.25f,
                            "NFSMW Vita Port - Milestone 0");
        vita2d_pgf_draw_text(font, 48, 135, title, 1.0f,
                            "Original VishDec code running on ARM/Vita");

        char expected[64];
        char result[64];
        snprintf(expected, sizeof(expected), "Expected CRC: %08X", kExpected);
        snprintf(result, sizeof(result), "Actual CRC:   %08X", actual);

        vita2d_pgf_draw_text(font, 48, 205, title, 1.0f, expected);
        vita2d_pgf_draw_text(font, 48, 245, title, 1.0f, result);
        vita2d_pgf_draw_text(font, 48, 315, status, 1.4f,
                            pass ? "PASS" : "FAIL");
        vita2d_pgf_draw_text(font, 48, 440, title, 0.9f,
                            "Press START to exit");

        vita2d_end_drawing();
        vita2d_swap_buffers();
    } while (!(pad.buttons & SCE_CTRL_START));

    vita2d_free_pgf(font);
    vita2d_fini();

    sceKernelExitProcess(pass ? 0 : 1);
    return pass ? 0 : 1;
}
