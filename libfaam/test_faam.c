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
    /* Test 1: Stream Muxer file creation (Audio & Video tracks, chapters) */
    FILE *fout = fopen("test_output.mp4", "wb");
    assert(fout != NULL);

    faam_io io_out = { fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    faam_muxer_config cfg;
    faam_status st = faam_muxer_config_init(&cfg, sizeof(cfg));
    assert(st == FAAM_OK);

    faam_chapter chaps[2];
    chaps[0].start_ms = 0;
    chaps[0].duration_ms = 5000;
    strcpy(chaps[0].title, "Intro");
    chaps[1].start_ms = 5000;
    chaps[1].duration_ms = 10000;
    strcpy(chaps[1].title, "Event");
    cfg.chapters = chaps;
    cfg.num_chapters = 2;

    /* Track 1: Audio */
    uint8_t dummy_asc[2] = { 0x12, 0x10 };
    faam_track_config a_tr;
    memset(&a_tr, 0, sizeof(a_tr));
    a_tr.struct_size = sizeof(a_tr);
    a_tr.track_type = FAAM_TRACK_AUDIO;
    a_tr.codec_id = FAAM_CODEC_AAC;
    a_tr.timescale = 44100;
    a_tr.sample_rate = 44100;
    a_tr.channels = 2;
    a_tr.codec_data = dummy_asc;
    a_tr.codec_data_len = sizeof(dummy_asc);

    uint32_t a_track_id = 0;
    st = faam_muxer_config_add_track(&cfg, &a_tr, &a_track_id);
    assert(st == FAAM_OK);
    assert(a_track_id == 1);

    /* Track 2: Video (H.264 Security Camera) */
    uint8_t dummy_avcc[10] = { 0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x02, 0x67, 0x64 };
    faam_track_config v_tr;
    memset(&v_tr, 0, sizeof(v_tr));
    v_tr.struct_size = sizeof(v_tr);
    v_tr.track_type = FAAM_TRACK_VIDEO;
    v_tr.codec_id = FAAM_CODEC_H264;
    v_tr.timescale = 90000;
    v_tr.width = 1920;
    v_tr.height = 1080;
    v_tr.codec_data = dummy_avcc;
    v_tr.codec_data_len = sizeof(dummy_avcc);

    uint32_t v_track_id = 0;
    st = faam_muxer_config_add_track(&cfg, &v_tr, &v_track_id);
    assert(st == FAAM_OK);
    assert(v_track_id == 2);

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
        st = faam_muxer_write_frame(m, a_track_id, dummy_frame, sizeof(dummy_frame), 1024, true);
        assert(st == FAAM_OK);
        st = faam_muxer_write_frame(m, v_track_id, dummy_frame, sizeof(dummy_frame), 3000, (i % 5 == 0));
        assert(st == FAAM_OK);
    }

    st = faam_muxer_finalize(m);
    assert(st == FAAM_OK);
    faam_muxer_close(m);
    free(mem_m);
    fclose(fout);

    /* Test 2: Stream Demuxer file reading */
    FILE *fin = fopen("test_output.mp4", "rb");
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

    uint32_t num_tracks = 0;
    st = faam_demuxer_get_num_tracks(d, &num_tracks);
    assert(st == FAAM_OK);
    assert(num_tracks == 2);

    faam_track_info t1, t2;
    st = faam_demuxer_get_track_info(d, 0, &t1);
    assert(st == FAAM_OK);
    assert(t1.track_type == FAAM_TRACK_AUDIO);

    st = faam_demuxer_get_track_info(d, 1, &t2);
    assert(st == FAAM_OK);
    assert(t2.track_type == FAAM_TRACK_VIDEO);
    assert(t2.width == 1920 && t2.height == 1080);

    uint8_t read_codec_data[64];
    uint32_t read_cdata_len = 0;
    st = faam_demuxer_get_codec_data(d, t1.track_id, read_codec_data, sizeof(read_codec_data), &read_cdata_len);
    assert(st == FAAM_OK);
    assert(read_cdata_len == sizeof(dummy_asc));

    faam_chapter read_chaps[10];
    uint32_t read_ch_count = 0;
    st = faam_demuxer_get_chapters(d, read_chaps, 10, &read_ch_count);
    assert(st == FAAM_OK);
    assert(read_ch_count == 2);
    assert(strcmp(read_chaps[0].title, "Intro") == 0);
    assert(strcmp(read_chaps[1].title, "Event") == 0);

    faam_demuxer_close(d);
    free(mem_d);
    fclose(fin);

    remove("test_output.mp4");

    printf("libfaam stream unit tests passed successfully.\n");
    return 0;
}
