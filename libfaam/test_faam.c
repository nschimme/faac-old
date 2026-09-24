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

#ifdef FAAM_HAVE_TAG_CHAPTER
static inline uint32_t t_read_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline void t_write_u32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}
static inline uint64_t t_read_u64(const uint8_t *b) {
    return ((uint64_t)t_read_u32(b) << 32) | (uint64_t)t_read_u32(b + 4);
}
static inline void t_write_u64(uint8_t *b, uint64_t v) {
    t_write_u32(b, (uint32_t)(v >> 32)); t_write_u32(b + 4, (uint32_t)v);
}

static bool t_is_offset_table_container(const char type[4]) {
    static const char *containers[] = { "trak", "mdia", "minf", "stbl" };
    for (size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); i++) {
        if (memcmp(type, containers[i], 4) == 0) return true;
    }
    return false;
}

static void t_shift_chunk_offsets(uint8_t *moov_buf, long box_off, bool is64, int64_t delta) {
    uint32_t count = t_read_u32(moov_buf + box_off + 12);
    long p = box_off + 16;
    for (uint32_t i = 0; i < count; i++) {
        if (is64) {
            uint64_t v = t_read_u64(moov_buf + p);
            t_write_u64(moov_buf + p, (uint64_t)((int64_t)v + delta));
            p += 8;
        } else {
            uint32_t v = t_read_u32(moov_buf + p);
            t_write_u32(moov_buf + p, (uint32_t)((int64_t)v + delta));
            p += 4;
        }
    }
}

static void t_fixup_moov_offsets(uint8_t *moov_buf, long start, long end, int64_t delta) {
    long pos = start;
    while (pos + 8 <= end) {
        uint32_t size = t_read_u32(moov_buf + pos);
        char type[4];
        memcpy(type, moov_buf + pos + 4, 4);
        if (size < 8 || pos + (long)size > end) break;

        if (memcmp(type, "stco", 4) == 0) t_shift_chunk_offsets(moov_buf, pos, false, delta);
        else if (memcmp(type, "co64", 4) == 0) t_shift_chunk_offsets(moov_buf, pos, true, delta);
        else if (t_is_offset_table_container(type)) t_fixup_moov_offsets(moov_buf, pos + 8, pos + size, delta);

        pos += size;
    }
}

/* Rewrites a just-muxed ftyp+mdat+moov file in place into a faststart-style
 * ftyp+moov+mdat layout, correcting every stco/co64 chunk offset for the
 * move (mdat's new start is ftyp_end+moov_size instead of ftyp_end, so every
 * stored offset needs += moov_size). This is test-only scaffolding to get a
 * moov-before-mdat file to patch against, since mux.c's cfg.faststart field
 * is currently unimplemented (see libfaam_internal.h / mux.c: nothing reads
 * cfg.faststart) -- it does not touch atom_patch.c or mux.c itself.
 */
static void make_faststart_layout(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)total);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)total, f) == (size_t)total);
    fclose(f);

    long pos = 0, ftyp_end = 0;
    long mdat_off = -1, moov_off = -1, moov_size = 0;
    while (pos + 8 <= total) {
        uint32_t size = t_read_u32(buf + pos);
        char type[4];
        memcpy(type, buf + pos + 4, 4);
        if (size < 8 || pos + (long)size > total) break;
        if (memcmp(type, "ftyp", 4) == 0 || memcmp(type, "wide", 4) == 0) ftyp_end = pos + size;
        else if (memcmp(type, "mdat", 4) == 0) mdat_off = pos;
        else if (memcmp(type, "moov", 4) == 0) { moov_off = pos; moov_size = size; }
        pos += size;
    }
    assert(ftyp_end > 0 && mdat_off == ftyp_end && moov_off > mdat_off);

    uint8_t *moov_buf = (uint8_t *)malloc((size_t)moov_size);
    assert(moov_buf != NULL);
    memcpy(moov_buf, buf + moov_off, (size_t)moov_size);
    t_fixup_moov_offsets(moov_buf, 8, moov_size, (int64_t)moov_size);

    FILE *out = fopen(path, "wb");
    assert(out != NULL);
    fwrite(buf, 1, (size_t)ftyp_end, out);
    fwrite(moov_buf, 1, (size_t)moov_size, out);
    fwrite(buf + mdat_off, 1, (size_t)(moov_off - mdat_off), out);
    fclose(out);

    free(moov_buf);
    free(buf);
}
#endif /* FAAM_HAVE_TAG_CHAPTER */

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

#ifdef FAAM_HAVE_TAG_CHAPTER
    /* --- Tests 3/4: in-place tag growth must not corrupt sample data ---
     *
     * faam_update_tags_stream()/faam_update_chapters_stream() rewrite the
     * ilst/chpl atom in place. When the new payload is bigger than what was
     * there, everything physically after it in the file has to shift, and if
     * mdat sits after the patched atom (moov-before-mdat / faststart-style
     * layout), every stco/co64 chunk offset -- an absolute file position --
     * has to move with it. Getting either of those wrong either overwrites
     * real file content or leaves stale offsets that point at the wrong
     * bytes. These tests build known frame content, force ilst growth well
     * past the atom's original size, and confirm every frame still reads
     * back byte-for-byte correct.
     */
#define TEST_FRAME_COUNT 40
#define TEST_FRAME_SIZE  96

    for (int faststart_case = 0; faststart_case < 2; faststart_case++) {
        const char *path = "test_grow.mp4";

        FILE *gf = fopen(path, "wb");
        assert(gf != NULL);
        faam_io gio = { gf, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

        faam_muxer_config gcfg;
        st = faam_muxer_config_init(&gcfg, sizeof(gcfg));
        assert(st == FAAM_OK);

        uint8_t g_asc[2] = { 0x12, 0x10 };
        faam_track_config g_tr;
        memset(&g_tr, 0, sizeof(g_tr));
        g_tr.struct_size = sizeof(g_tr);
        g_tr.track_type = FAAM_TRACK_AUDIO;
        g_tr.codec_id = FAAM_CODEC_AAC;
        g_tr.timescale = 44100;
        g_tr.sample_rate = 44100;
        g_tr.channels = 2;
        g_tr.codec_data = g_asc;
        g_tr.codec_data_len = sizeof(g_asc);

        uint32_t g_track_id = 0;
        st = faam_muxer_config_add_track(&gcfg, &g_tr, &g_track_id);
        assert(st == FAAM_OK);

        uint32_t g_muxer_size = 0;
        st = faam_muxer_get_state_size(&gcfg, &g_muxer_size);
        assert(st == FAAM_OK);
        void *g_mem = malloc(g_muxer_size);
        assert(g_mem != NULL);

        faam_muxer *gm = NULL;
        st = faam_muxer_init(g_mem, g_muxer_size, &gcfg, &gio, &gm);
        assert(st == FAAM_OK);

        uint8_t frame_buf[TEST_FRAME_SIZE];
        for (int i = 0; i < TEST_FRAME_COUNT; i++) {
            memset(frame_buf, (uint8_t)(i * 7 + 3), sizeof(frame_buf));
            st = faam_muxer_write_frame(gm, g_track_id, frame_buf, sizeof(frame_buf), 1024, true);
            assert(st == FAAM_OK);
        }

        st = faam_muxer_finalize(gm);
        assert(st == FAAM_OK);
        faam_muxer_close(gm);
        free(g_mem);
        fclose(gf);

        /* faam's own muxer always writes mdat before moov -- cfg.faststart
         * is declared in faam.h but mux.c never reads it, so it can't
         * actually produce a moov-first file yet. Synthesize that layout by
         * hand (ftyp+moov+mdat, with stco/co64 corrected for the move) so
         * the faststart_case=1 pass exercises atom_patch's chunk-offset
         * correction path the way a real faststart writer eventually would.
         */
        if (faststart_case == 1) {
            make_faststart_layout(path);
        }

        long size_before_tag = 0;
        {
            FILE *sf = fopen(path, "rb");
            assert(sf != NULL);
            fseek(sf, 0, SEEK_END);
            size_before_tag = ftell(sf);
            fclose(sf);
        }

        /* Force real growth: this dwarfs the tiny default ilst (just an
         * encoder tag) the muxer writes. */
        faam_metadata big_meta;
        memset(&big_meta, 0, sizeof(big_meta));
        strcpy(big_meta.title, "A Reasonably Long Test Title For Growth");
        strcpy(big_meta.artist, "Test Artist Name");
        strcpy(big_meta.album, "Test Album Name");
        big_meta.track_num = 3;
        big_meta.track_total = 12;
        big_meta.compilation = true;
        big_meta.num_custom_tags = 16;
        for (uint32_t i = 0; i < big_meta.num_custom_tags; i++) {
            snprintf(big_meta.custom_tags[i].name, sizeof(big_meta.custom_tags[i].name), "custom_tag_%u", i);
            memset(big_meta.custom_tags[i].value, 'x', sizeof(big_meta.custom_tags[i].value) - 1);
            big_meta.custom_tags[i].value[sizeof(big_meta.custom_tags[i].value) - 1] = '\0';
        }

        FILE *tf = fopen(path, "r+b");
        assert(tf != NULL);
        faam_io tio = { tf, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };
        st = faam_update_tags_stream(&tio, &big_meta);
        assert(st == FAAM_OK);
        fclose(tf);

        long size_after_grow = 0;
        {
            FILE *sf = fopen(path, "rb");
            assert(sf != NULL);
            fseek(sf, 0, SEEK_END);
            size_after_grow = ftell(sf);
            fclose(sf);
        }
        assert(size_after_grow > size_before_tag);

        if (faststart_case == 0) {
            /* mdat precedes the patched atom here, so growth only ever has
             * to shift the (empty, in this layout) tail after moov -- no
             * unrelated content should have moved or been touched. */
            long delta = size_after_grow - size_before_tag;
            assert(delta > 0);

            /* Shrinking back onto the now-larger slot should pad in place
             * (free atom), not shrink the file -- confirms the cheap path
             * still runs instead of an unnecessary tail shift. */
            faam_metadata small_meta;
            memset(&small_meta, 0, sizeof(small_meta));
            strcpy(small_meta.title, "T");

            FILE *tf2 = fopen(path, "r+b");
            assert(tf2 != NULL);
            faam_io tio2 = { tf2, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };
            st = faam_update_tags_stream(&tio2, &small_meta);
            assert(st == FAAM_OK);
            fclose(tf2);

            long size_after_shrink = 0;
            FILE *sf2 = fopen(path, "rb");
            assert(sf2 != NULL);
            fseek(sf2, 0, SEEK_END);
            size_after_shrink = ftell(sf2);
            fclose(sf2);
            assert(size_after_shrink == size_after_grow);
        }

        /* The real assertion: every sample frame must still demux to
         * exactly what was muxed, regardless of layout. Under the old
         * 64KB-capped, non-shifting tag.c this would either fail to locate
         * ilst (moov past 64KB was never the trigger here, file is small)
         * or -- in the faststart_case=1 pass -- read back corrupted/wrong
         * frame bytes because stco still pointed at pre-shift offsets while
         * mdat physically moved by the ilst growth delta.
         */
        FILE *df = fopen(path, "rb");
        assert(df != NULL);
        faam_io dio = { df, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

        uint32_t d_size = 0;
        st = faam_demuxer_get_state_size(&d_size);
        assert(st == FAAM_OK);
        void *d_mem = malloc(d_size);
        assert(d_mem != NULL);

        faam_demuxer *dd = NULL;
        st = faam_demuxer_init(d_mem, d_size, &dio, &dd);
        assert(st == FAAM_OK);

        int frames_seen = 0;
        uint8_t read_buf[TEST_FRAME_SIZE];
        faam_frame_loc loc;
        while (faam_demuxer_next_frame_loc(dd, &loc) == FAAM_OK) {
            uint32_t got_bytes = 0;
            st = faam_demuxer_read_frame(dd, read_buf, sizeof(read_buf), &got_bytes);
            assert(st == FAAM_OK);
            assert(got_bytes == TEST_FRAME_SIZE);

            uint8_t expected = (uint8_t)(frames_seen * 7 + 3);
            for (int b = 0; b < TEST_FRAME_SIZE; b++) {
                assert(read_buf[b] == expected);
            }
            frames_seen++;
        }
        assert(frames_seen == TEST_FRAME_COUNT);

        faam_demuxer_close(dd);
        free(d_mem);
        fclose(df);

        remove(path);
    }
#endif /* FAAM_HAVE_TAG_CHAPTER */

    printf("libfaam stream unit tests passed successfully.\n");
    return 0;
}
