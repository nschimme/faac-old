/*
 * FAAC - Freeware Advanced Audio Coder
 * Unit tests for HE-AAC v1 SBR time-grid decision module (sbr_grid_next).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "sbr.h"
#include "sbr_grid.h"

/* Helper to assert basic structural validity of a generated SbrGrid */
static void assert_grid_valid(const SbrGrid *g, int T)
{
    assert(g != NULL);
    assert(g->numEnvelopes >= 1);
    if (g->frameClass == SBR_FRAME_CLASS_VARVAR) {
        assert(g->numEnvelopes <= 5);
    } else {
        assert(g->numEnvelopes <= 4);
    }

    assert(g->bsPointer >= 0 && g->bsPointer <= g->numEnvelopes);

    /* Borders must be strictly increasing */
    for (int i = 0; i < g->numEnvelopes; i++) {
        assert(g->tEnv[i] < g->tEnv[i + 1]);
    }

    /* Check encodability of relative distances */
    if (g->frameClass == SBR_FRAME_CLASS_FIXFIX) {
        assert(g->tEnv[0] == 0);
        assert(g->tEnv[g->numEnvelopes] == T);
    } else if (g->frameClass == SBR_FRAME_CLASS_FIXVAR) {
        assert(g->tEnv[0] == 0);
        int n_rel = g->numEnvelopes - 1;
        for (int i = 0; i < n_rel; i++) {
            int d = g->tEnv[g->numEnvelopes - i] - g->tEnv[g->numEnvelopes - i - 1];
            assert(d >= 2 && d <= 8 && (d % 2 == 0));
        }
    } else if (g->frameClass == SBR_FRAME_CLASS_VARFIX) {
        assert(g->tEnv[g->numEnvelopes] == T);
        int n_rel = g->numEnvelopes - 1;
        for (int i = 0; i < n_rel; i++) {
            int d = g->tEnv[i + 1] - g->tEnv[i];
            assert(d >= 2 && d <= 8 && (d % 2 == 0));
        }
    } else if (g->frameClass == SBR_FRAME_CLASS_VARVAR) {
        int n_rel = g->numEnvelopes - 1;
        /* Check that relative distances are even and in [2, 8] */
        for (int i = 0; i < n_rel; i++) {
            int d = g->tEnv[i + 1] - g->tEnv[i];
            assert(d >= 2 && d <= 8 && (d % 2 == 0));
        }
    }
}

/* 1. Every row of the transition table and fixed-right-border request */
static void test_transition_table(void)
{
    printf("Running Test 1: Transition table coverage...\n");

    SbrGridState st;
    SbrGrid grid;

    /* FIXFIX + att=0 -> FIXFIX */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(st.prevClass == SBR_FRAME_CLASS_FIXFIX);

    /* FIXFIX + att=1 -> FIXVAR */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_FIXVAR);

    /* FIXVAR + att=0 + spread=0 -> VARFIX */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_FIXVAR;
    st.spread = false;
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(st.prevClass == SBR_FRAME_CLASS_VARFIX);

    /* FIXVAR + att=0 + spread=1 -> VARVAR (consume spread) */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_FIXVAR;
    st.spread = true;
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.spread == false);

    /* FIXVAR + att=1 -> VARVAR (clear spread) */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_FIXVAR;
    st.spread = true;
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.spread == false);

    /* VARFIX + att=0 -> FIXFIX */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARFIX;
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(st.prevClass == SBR_FRAME_CLASS_FIXFIX);

    /* VARFIX + att=1 -> FIXVAR */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARFIX;
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_FIXVAR);

    /* VARVAR + att=0 + spread=0 -> VARFIX */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARVAR;
    st.spread = false;
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(st.prevClass == SBR_FRAME_CLASS_VARFIX);

    /* VARVAR + att=0 + spread=1 -> VARVAR (consume spread) */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARVAR;
    st.spread = true;
    st.numFollowUpBorders = 1;
    st.followUpBorders[0] = 8;
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.spread == false);

    /* VARVAR + att=1 -> VARVAR (clear spread) */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARVAR;
    st.spread = true;
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.prevClass == SBR_FRAME_CLASS_VARVAR);
    assert(st.spread == false);

    /* Fixed-right-border requests from each previous class */
    /* From FIXFIX */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_FIXFIX;
    sbr_grid_next(&st, 16, 1, 2, 0, 1, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);

    /* From FIXVAR */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_FIXVAR;
    st.spread = true;
    sbr_grid_next(&st, 16, 1, 2, 0, 1, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(st.spread == false);

    /* From VARFIX */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARFIX;
    sbr_grid_next(&st, 16, 1, 2, 0, 1, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);

    /* From VARVAR */
    sbr_grid_state_init(&st);
    st.prevClass = SBR_FRAME_CLASS_VARVAR;
    st.spread = true;
    sbr_grid_next(&st, 16, 1, 2, 0, 1, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(st.spread == false);

    printf("PASS: Test 1 (Transition table coverage)\n");
}

/* 2. Worked traces and spread flag assertions */
static void test_worked_traces(void)
{
    printf("Running Test 2: Worked traces...\n");

    SbrGridState st;
    SbrGrid grid;

    /* Trace 1: 000 -> FF FF FF */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(!st.spread);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(!st.spread);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(!st.spread);

    /* Trace 2: 100 (no spread) -> FV VF FF (using pos = 2) */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    assert(!st.spread);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(!st.spread);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(!st.spread);

    /* Trace 3: 100 (spread) -> FV VV VF (using pos = 10) */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 10, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    assert(st.spread == true);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    assert(!st.spread);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    assert(!st.spread);

    /* Trace 4: 1100 -> FV VV VF FF */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXFIX);

    /* Trace 5: 1110 -> FV VV VV VF */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARVAR);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);

    /* Trace 6: 1010 -> FV VF FV VF */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);
    sbr_grid_next(&st, 16, 1, 2, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_FIXVAR);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &grid);
    assert(grid.frameClass == SBR_FRAME_CLASS_VARFIX);

    printf("PASS: Test 2 (Worked traces)\n");
}

/* 3. Worked example: pos 3 -> onset 7, borders 7, 9, 13, and continuity */
static void test_worked_example_pos3(void)
{
    printf("Running Test 3: Worked example pos 3...\n");

    SbrGridState st;
    SbrGrid g1, g2;

    sbr_grid_state_init(&st);
    /* Detector pos 3, offset 4 -> onset A = 7 */
    sbr_grid_next(&st, 16, 1, 3, 0, 0, 1, 1, &g1);

    assert(g1.frameClass == SBR_FRAME_CLASS_FIXVAR);
    assert(g1.tEnv[1] == 7);
    assert(g1.tEnv[2] == 9);
    assert(g1.tEnv[3] == 13);
    int closing_border = g1.tEnv[g1.numEnvelopes];

    /* Follow-up frame (no attack -> VARFIX) */
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &g2);
    assert(g2.frameClass == SBR_FRAME_CLASS_VARFIX);
    int opening_border = g2.tEnv[0];

    assert(closing_border - 16 == opening_border);

    printf("PASS: Test 3 (Worked example pos 3)\n");
}

/* 4. Exhaustive sweep over attack pos 0..15, split 0/1, 3-frame attack patterns */
static void test_exhaustive_sweep(void)
{
    printf("Running Test 4: Exhaustive sweep over attack patterns & positions...\n");

    int T = 16;
    for (int pos = 0; pos < 16; pos++) {
        for (int split = 0; split <= 1; split++) {
            for (int pat = 0; pat < 8; pat++) {
                int att0 = (pat >> 2) & 1;
                int att1 = (pat >> 1) & 1;
                int att2 = pat & 1;

                SbrGridState st;
                sbr_grid_state_init(&st);

                SbrGrid g0, g1, g2;
                sbr_grid_next(&st, T, att0, pos, split, 0, 1, 1, &g0);
                assert_grid_valid(&g0, T);

                sbr_grid_next(&st, T, att1, pos, split, 0, 1, 1, &g1);
                assert_grid_valid(&g1, T);

                if (g0.frameClass == SBR_FRAME_CLASS_FIXVAR &&
                    (g1.frameClass == SBR_FRAME_CLASS_VARFIX || g1.frameClass == SBR_FRAME_CLASS_VARVAR)) {
                    assert(g0.tEnv[g0.numEnvelopes] - T == g1.tEnv[0]);
                }

                sbr_grid_next(&st, T, att2, pos, split, 0, 1, 1, &g2);
                assert_grid_valid(&g2, T);

                if (g1.frameClass == SBR_FRAME_CLASS_FIXVAR &&
                    (g2.frameClass == SBR_FRAME_CLASS_VARFIX || g2.frameClass == SBR_FRAME_CLASS_VARVAR)) {
                    assert(g1.tEnv[g1.numEnvelopes] - T == g2.tEnv[0]);
                }
            }
        }
    }

    printf("PASS: Test 4 (Exhaustive sweep)\n");
}

/* 5. Stable frames */
static void test_stable_frames(void)
{
    printf("Running Test 5: Stable frames...\n");

    SbrGridState st;
    SbrGrid g;

    /* T=16 unsplit */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 0, 0, 0, 0, 1, 1, &g);
    assert(g.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(g.numEnvelopes == 1);
    assert(g.tEnv[0] == 0 && g.tEnv[1] == 16);

    /* T=16 split */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 16, 0, 1, 1, 0, 1, 1, &g);
    assert(g.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(g.numEnvelopes == 2);
    assert(g.tEnv[0] == 0 && g.tEnv[1] == 8 && g.tEnv[2] == 16);

    /* T=15 split */
    sbr_grid_state_init(&st);
    sbr_grid_next(&st, 15, 0, 1, 1, 0, 1, 1, &g);
    assert(g.frameClass == SBR_FRAME_CLASS_FIXFIX);
    assert(g.numEnvelopes == 2);
    assert(g.tEnv[0] == 0 && g.tEnv[1] == 8 && g.tEnv[2] == 15);

    printf("PASS: Test 5 (Stable frames)\n");
}

/* 6. Stereo equality helper */
static void test_stereo_equality(void)
{
    printf("Running Test 6: Stereo grid equality helper...\n");

    SbrGrid g1, g2;
    memset(&g1, 0, sizeof(g1));
    memset(&g2, 0, sizeof(g2));

    g1.frameClass = SBR_FRAME_CLASS_FIXVAR;
    g1.numEnvelopes = 2;
    g1.tEnv[0] = 0; g1.tEnv[1] = 6; g1.tEnv[2] = 16;
    g1.freqRes[0] = 1; g1.freqRes[1] = 1;
    g1.bsPointer = 1;

    g2 = g1;
    assert(sbr_grid_equal(&g1, &g2));

    g2.tEnv[1] = 8;
    assert(!sbr_grid_equal(&g1, &g2));

    g2 = g1;
    g2.freqRes[0] = 0;
    assert(!sbr_grid_equal(&g1, &g2));

    g2 = g1;
    g2.frameClass = SBR_FRAME_CLASS_FIXFIX;
    assert(!sbr_grid_equal(&g1, &g2));

    printf("PASS: Test 6 (Stereo grid equality helper)\n");
}

int main(void)
{
    printf("Running deterministic SBR envelope-grid unit tests...\n");

    test_transition_table();
    test_worked_traces();
    test_worked_example_pos3();
    test_exhaustive_sweep();
    test_stable_frames();
    test_stereo_equality();

    printf("All SBR time-grid tests PASSED successfully!\n");
    return 0;
}
