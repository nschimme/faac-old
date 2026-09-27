#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"

static void simulate_trace(const int *attacks, const int *positions, int nframes, int slots, SbrFrameClass *out_classes)
{
    SignalAnalysis sa;
    memset(&sa, 0, sizeof(sa));
    SbrAnalysisFrame f;
    memset(&f, 0, sizeof(f));
    f.numSlots = slots;

    SBRInfo sbr;
    memset(&sbr, 0, sizeof(sbr));
    sbr.numEnvFixFix = 2;

    SbrFrameData fd;
    memset(&fd, 0, sizeof(fd));

    for (int frame = 0; frame < nframes; frame++) {
        bool lfe[1] = { false };
        int att = attacks[frame];
        int pos = positions[frame];

        memset(&f, 0, sizeof(f));
        f.numSlots = slots;
        if (att) {
            /* Create a sharp energy jump at pos to trigger measure() transient detection */
            for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
                f.bandE[0][pos][k] = 100.0f;
                f.bandE[0][pos + 1][k] = 100.0f;
            }
            f.totalE[0][pos] = 1000.0f;
        } else {
            for (int s = 0; s < slots; s++) f.totalE[0][s] = 1.0f;
        }

        int bt[1] = { att ? ONLY_SHORT_WINDOW : ONLY_LONG_WINDOW };
        SbrFinalizeFrame(&sa, &f, &f, &f, 1, lfe, bt, &sbr, &fd);
        out_classes[frame] = fd.ch[0].grid.frameClass;
    }
}

int main(void)
{
    printf("Running expanded SBR envelope-grid invariant and trace tests...\n");

    /* Test Trace 1: 0 0 0 -> FIXFIX FIXFIX FIXFIX */
    {
        int attacks[3] = {0, 0, 0};
        int pos[3] = {0, 0, 0};
        SbrFrameClass classes[3];
        simulate_trace(attacks, pos, 3, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXFIX);
        assert(classes[1] == SBR_FRAME_CLASS_FIXFIX);
        assert(classes[2] == SBR_FRAME_CLASS_FIXFIX);
        printf("PASS: Trace 0 0 0 (T=16)\n");
    }

    /* Test Trace 2: 1 0 0 -> FIXVAR VARFIX FIXFIX (no spread, T=16, pos=2) */
    {
        int attacks[3] = {1, 0, 0};
        int pos[3] = {2, 0, 0};
        SbrFrameClass classes[3];
        simulate_trace(attacks, pos, 3, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[1] == SBR_FRAME_CLASS_VARFIX);
        assert(classes[2] == SBR_FRAME_CLASS_FIXFIX);
        printf("PASS: Trace 1 0 0 no spread (T=16)\n");
    }

    /* Test Trace 3: 1 0 0 with spread -> FIXVAR VARVAR VARFIX (T=16, pos=7 causing remainder < min_join) */
    {
        int attacks[3] = {1, 0, 0};
        int pos[3] = {7, 0, 0};
        SbrFrameClass classes[3];
        simulate_trace(attacks, pos, 3, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[1] == SBR_FRAME_CLASS_VARVAR);
        assert(classes[2] == SBR_FRAME_CLASS_VARFIX);
        printf("PASS: Trace 1 0 0 with spread (T=16)\n");
    }

    /* Test Trace 4: 1 1 0 0 -> FIXVAR VARVAR VARFIX FIXFIX */
    {
        int attacks[4] = {1, 1, 0, 0};
        int pos[4] = {2, 2, 0, 0};
        SbrFrameClass classes[4];
        simulate_trace(attacks, pos, 4, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[1] == SBR_FRAME_CLASS_VARVAR);
        assert(classes[2] == SBR_FRAME_CLASS_VARFIX);
        assert(classes[3] == SBR_FRAME_CLASS_FIXFIX);
        printf("PASS: Trace 1 1 0 0 (T=16)\n");
    }

    /* Test Trace 5: 1 1 1 0 -> FIXVAR VARVAR VARVAR VARFIX */
    {
        int attacks[4] = {1, 1, 1, 0};
        int pos[4] = {2, 2, 2, 0};
        SbrFrameClass classes[4];
        simulate_trace(attacks, pos, 4, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[1] == SBR_FRAME_CLASS_VARVAR);
        assert(classes[2] == SBR_FRAME_CLASS_VARVAR);
        assert(classes[3] == SBR_FRAME_CLASS_VARFIX);
        printf("PASS: Trace 1 1 1 0 (T=16)\n");
    }

    /* Test Trace 6: 1 0 1 0 -> FIXVAR VARFIX FIXVAR VARFIX */
    {
        int attacks[4] = {1, 0, 1, 0};
        int pos[4] = {2, 0, 2, 0};
        SbrFrameClass classes[4];
        simulate_trace(attacks, pos, 4, 16, classes);
        assert(classes[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[1] == SBR_FRAME_CLASS_VARFIX);
        assert(classes[2] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes[3] == SBR_FRAME_CLASS_VARFIX);
        printf("PASS: Trace 1 0 1 0 (T=16)\n");
    }

    /* Test Non-16 Slot Modes: T=15, T=9, T=18 */
    {
        int attacks[3] = {1, 0, 0};
        int pos15[3] = {1, 0, 0};
        int pos9[3] = {1, 0, 0};
        int pos18[3] = {4, 0, 0};
        SbrFrameClass classes15[3], classes9[3], classes18[3];
        simulate_trace(attacks, pos15, 3, 15, classes15);
        assert(classes15[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes15[1] == SBR_FRAME_CLASS_VARFIX);

        simulate_trace(attacks, pos9, 3, 9, classes9);
        assert(classes9[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes9[1] == SBR_FRAME_CLASS_VARFIX);

        simulate_trace(attacks, pos18, 3, 18, classes18);
        assert(classes18[0] == SBR_FRAME_CLASS_FIXVAR);
        assert(classes18[1] == SBR_FRAME_CLASS_VARFIX);

        printf("PASS: Non-16 slot modes (T=15, T=9, T=18)\n");
    }

    printf("All expanded SBR envelope-grid tests PASSED successfully!\n");
    return 0;
}
