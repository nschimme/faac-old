/*
 * Unit and Integration Tests for libfaam over Stream I/O
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "faam.h"

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
}

int main(void)
{
    /* Test 1: ASC build & parse */
    faam_asc_info asc_in;
    memset(&asc_in, 0, sizeof(asc_in));
    asc_in.object_type = 2; /* AAC-LC */
    asc_in.sample_rate = 44100;
    asc_in.channels = 2;

    uint8_t asc_buf[64];
    uint32_t asc_len = 0;
    faam_status st = faam_asc_build(&asc_in, asc_buf, sizeof(asc_buf), &asc_len);
    assert(st == FAAM_OK);
    assert(asc_len == 2);

    faam_asc_info asc_out;
    st = faam_asc_parse(asc_buf, asc_len, &asc_out);
    assert(st == FAAM_OK);
    assert(asc_out.object_type == 2);
    assert(asc_out.sample_rate == 44100);
    assert(asc_out.channels == 2);

    /* Test 2: Stream Muxer file creation */
    FILE *fout = fopen("test_output.m4a", "wb");
    assert(fout != NULL);

    faam_io io_out = { fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    faam_muxer_config cfg;
    st = faam_muxer_config_init(&cfg, sizeof(cfg));
    assert(st == FAAM_OK);
    cfg.asc_buf = asc_buf;
    cfg.asc_len = asc_len;

    uint32_t muxer_size = 0;
    st = faam_muxer_get_state_size(&cfg, &muxer_size);
    assert(st == FAAM_OK);
    assert(muxer_size > 0);

    void *mem_m = malloc(muxer_size);
    assert(mem_m != NULL);

    faam_muxer *m = NULL;
    st = faam_muxer_init(mem_m, muxer_size, &cfg, &io_out, &m);
    assert(st == FAAM_OK);
    assert(m != NULL);

    uint8_t dummy_frame[512];
    memset(dummy_frame, 0xAB, sizeof(dummy_frame));
    for (int i = 0; i < 10; i++) {
        st = faam_muxer_write_frame(m, dummy_frame, sizeof(dummy_frame), 1024);
        assert(st == FAAM_OK);
    }

    st = faam_muxer_finalize(m);
    assert(st == FAAM_OK);
    faam_muxer_close(m);
    free(mem_m);
    fclose(fout);

    /* Test 3: Stream Demuxer file reading */
    FILE *fin = fopen("test_output.m4a", "rb");
    assert(fin != NULL);

    faam_io io_in = { fin, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    uint32_t demuxer_size = 0;
    st = faam_demuxer_get_state_size(&demuxer_size);
    assert(st == FAAM_OK);
    assert(demuxer_size > 0);

    void *mem_d = malloc(demuxer_size);
    assert(mem_d != NULL);

    faam_demuxer *d = NULL;
    st = faam_demuxer_init(mem_d, demuxer_size, &io_in, &d);
    assert(st == FAAM_OK);
    assert(d != NULL);

    uint8_t read_asc[64];
    uint32_t read_asc_len = 0;
    st = faam_demuxer_get_asc(d, read_asc, sizeof(read_asc), &read_asc_len);
    assert(st == FAAM_OK);
    assert(read_asc_len > 0);

    faam_gapless_info gapless;
    st = faam_demuxer_get_gapless(d, &gapless);
    assert(st == FAAM_OK);

    faam_demuxer_close(d);
    free(mem_d);
    fclose(fin);

    remove("test_output.m4a");

    printf("libfaam stream unit tests passed successfully.\n");
    return 0;
}
