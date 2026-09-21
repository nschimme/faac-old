/*
 * Comprehensive Unit & Integration Test for FAAD3 Engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "faad.h"

#define NUM_THREADS 8
#define ITERATIONS_PER_THREAD 100

static void run_decoder_iteration(void)
{
    faad_config cfg;
    faad_status st = faad_config_init(&cfg, sizeof(cfg));
    assert(st == FAAD_OK);

    faad_decoder *dec = NULL;
    st = faad_decoder_create(&cfg, NULL, 0, &dec);
    assert(st == FAAD_OK);
    assert(dec != NULL);

    faad_stream_info info;
    st = faad_decoder_get_info(dec, &info);
    assert(st == FAAD_OK);

    st = faad_decoder_flush(dec);
    assert(st == FAAD_OK);

    faad_decoder_destroy(dec);
}

#if defined(_WIN32) && !defined(__MINGW32__)
static DWORD WINAPI thread_test_worker(LPVOID arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        run_decoder_iteration();
    }
    return 0;
}
#else
static void *thread_test_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        run_decoder_iteration();
    }
    return NULL;
}
#endif

/* AudioSpecificConfig signalling: the 0x2b7 sync extension carries a real
 * sbrPresentFlag, so an explicit "no SBR" (FFmpeg's default for AAC-LC in
 * MP4) must stay LC at the core rate, while faac's HE-AAC form and the
 * explicit hierarchical AOT 5/29 forms enable it. */
static void test_asc_sbr_signalling(void)
{
    static const uint8_t lc_explicit_no_sbr[] = { 0x14, 0x08, 0x56, 0xe5, 0x00 };
    static const uint8_t lc_plain[]           = { 0x14, 0x08 };
    static const uint8_t he_sbr_present[]     = { 0x14, 0x08, 0x56, 0xe5, 0xa8 };
    /* explicit hierarchical: AOT 5 / 29, 16 kHz, mono, 32 kHz out, core LC */
    static const uint8_t he_hierarchical[]    = { 0x2c, 0x0a, 0x88, 0x00 };
    static const uint8_t hev2_hierarchical[]  = { 0xec, 0x0a, 0x88, 0x00 };
    const struct { const uint8_t *asc; uint32_t len; enum faad_object_type obj; uint32_t rate; } cases[] = {
        { lc_explicit_no_sbr, sizeof(lc_explicit_no_sbr), FAAD_OBJ_LC,        16000 },
        { lc_plain,           sizeof(lc_plain),           FAAD_OBJ_LC,        16000 },
        { he_sbr_present,     sizeof(he_sbr_present),     FAAD_OBJ_HE_AAC_V1, 32000 },
        { he_hierarchical,    sizeof(he_hierarchical),    FAAD_OBJ_HE_AAC_V1, 32000 },
        { hev2_hierarchical,  sizeof(hev2_hierarchical),  FAAD_OBJ_HE_AAC_V1, 32000 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        faad_config cfg;
        assert(faad_config_init(&cfg, sizeof(cfg)) == FAAD_OK);
        cfg.stream_format = FAAD_STREAM_RAW;
        faad_decoder *dec = NULL;
        assert(faad_decoder_create(&cfg, cases[i].asc, cases[i].len, &dec) == FAAD_OK);
        faad_stream_info info;
        assert(faad_decoder_get_info(dec, &info) == FAAD_OK);
        assert(info.sample_rate == cases[i].rate);
        assert(info.channels == 1);
        assert(info.object_type == cases[i].obj);
        faad_decoder_destroy(dec);
    }
}

int main(void)
{
    test_asc_sbr_signalling();

    faad_config cfg;
    faad_status st = faad_config_init(&cfg, sizeof(cfg));
    assert(st == FAAD_OK);
    assert(cfg.stream_format == FAAD_STREAM_ADTS);

    uint32_t state_bytes = 0;
    st = faad_get_state_size(&cfg, &state_bytes);
    assert(st == FAAD_OK);
    assert(state_bytes > 0);

    /* Test 1: Static Memory Placement (Zero Heap Allocation) */
    void *static_mem = malloc(state_bytes);
    assert(static_mem != NULL);

    faad_decoder *dec_static = NULL;
    st = faad_decoder_init(static_mem, state_bytes, &cfg, NULL, 0, &dec_static);
    assert(st == FAAD_OK);
    assert(dec_static != NULL);

    faad_stream_info info;
    st = faad_decoder_get_info(dec_static, &info);
    assert(st == FAAD_OK);
    assert(info.channels == 2);

    st = faad_decoder_flush(dec_static);
    assert(st == FAAD_OK);

    free(static_mem);

    /* Test 2: Heap Wrapper Initialization */
    faad_decoder *dec_heap = NULL;
    st = faad_decoder_create(&cfg, NULL, 0, &dec_heap);
    assert(st == FAAD_OK);
    assert(dec_heap != NULL);

    st = faad_decoder_flush(dec_heap);
    assert(st == FAAD_OK);

    faad_decoder_destroy(dec_heap);

    /* Test 3: Concurrent Multi-Threaded Stress Test */
#if defined(_WIN32) && !defined(__MINGW32__)
    HANDLE threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = CreateThread(NULL, 0, thread_test_worker, NULL, 0, NULL);
        assert(threads[i] != NULL);
    }
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_test_worker, NULL);
        assert(rc == 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    printf("FAAD3 static placement, heap, and concurrent multi-threading tests passed successfully.\n");
    return 0;
}
