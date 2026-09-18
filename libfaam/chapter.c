/*
 * Stream-based Chapter / Bookmark management for libfaam (QuickTime chpl atom)
 */

#include "libfaam_internal.h"

static inline void write_u32(uint8_t *b, uint32_t val) {
    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)(val >> 16);
    b[2] = (uint8_t)(val >> 8);
    b[3] = (uint8_t)val;
}

faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count)
{
    if (!io || !chapters) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    io->seek(io->user_data, 0);
    uint32_t cap = 65536;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return FAAM_ERR_INSUFFICIENT_MEM;

    int32_t bytes = io->read(io->user_data, buf, cap);
    if (bytes < 32) {
        free(buf);
        return FAAM_ERR_BAD_CONTAINER;
    }

    /* Build QuickTime chpl atom payload safely */
    uint32_t max_chpl_cap = 16 + count * 265;
    uint8_t *chpl_buf = (uint8_t *)malloc(max_chpl_cap);
    if (!chpl_buf) {
        free(buf);
        return FAAM_ERR_INSUFFICIENT_MEM;
    }

    uint32_t chpl_len = 16;
    write_u32(chpl_buf + 8, 0); /* ver/flags */
    write_u32(chpl_buf + 12, count);

    for (uint32_t c = 0; c < count; c++) {
        uint64_t start_time = chapters[c].start_ms * 10000ULL; /* 100ns units */
        write_u64_be(chpl_buf + chpl_len, start_time);
        chpl_len += 8;

        size_t tlen = strlen(chapters[c].title);
        if (tlen > 255) tlen = 255;
        chpl_buf[chpl_len++] = (uint8_t)tlen;
        memcpy(chpl_buf + chpl_len, chapters[c].title, tlen);
        chpl_len += (uint32_t)tlen;
    }

    write_u32(chpl_buf, chpl_len);
    memcpy(chpl_buf + 4, "chpl", 4);

    /* Locate udta or moov atom */
    uint32_t udta_offset = 0;
    uint32_t chpl_offset = 0;
    uint32_t pos = 0;

    while (pos + 8 <= (uint32_t)bytes) {
        uint32_t size = read_u32_be(buf + pos);
        if (size < 8 || pos + size > (uint32_t)bytes) break;

        if (memcmp(buf + pos + 4, "moov", 4) == 0) {
            uint32_t sub = pos + 8;
            uint32_t moov_end = pos + size;
            while (sub + 8 <= moov_end) {
                uint32_t sub_size = read_u32_be(buf + sub);
                if (sub_size < 8 || sub + sub_size > moov_end) break;

                if (memcmp(buf + sub + 4, "udta", 4) == 0) {
                    udta_offset = sub;
                    uint32_t u_sub = sub + 8;
                    uint32_t udta_end = sub + sub_size;
                    while (u_sub + 8 <= udta_end) {
                        uint32_t u_size = read_u32_be(buf + u_sub);
                        if (u_size < 8 || u_sub + u_size > udta_end) break;
                        if (memcmp(buf + u_sub + 4, "chpl", 4) == 0) {
                            chpl_offset = u_sub;
                            break;
                        }
                        u_sub += u_size;
                    }
                }
                sub += sub_size;
            }
        }
        pos += size;
    }

    if (chpl_offset > 0) {
        uint32_t old_chpl_size = read_u32_be(buf + chpl_offset);
        if (old_chpl_size >= chpl_len) {
            uint32_t diff = old_chpl_size - chpl_len;
            if (diff >= 8) {
                io->seek(io->user_data, chpl_offset);
                io->write(io->user_data, chpl_buf, chpl_len);
                uint8_t free_box[8];
                write_u32(free_box, diff);
                memcpy(free_box + 4, "free", 4);
                io->write(io->user_data, free_box, 8);
            } else {
                write_u32(chpl_buf, old_chpl_size);
                io->seek(io->user_data, chpl_offset);
                io->write(io->user_data, chpl_buf, chpl_len);
            }
        } else {
            int32_t delta = (int32_t)(chpl_len - old_chpl_size);
            io->seek(io->user_data, chpl_offset);
            io->write(io->user_data, chpl_buf, chpl_len);

            if (udta_offset > 0) {
                uint32_t udta_sz = read_u32_be(buf + udta_offset) + delta;
                uint8_t hdr[4]; write_u32(hdr, udta_sz);
                io->seek(io->user_data, udta_offset); io->write(io->user_data, hdr, 4);
            }
            if (pos > 0) {
                uint32_t moov_sz = read_u32_be(buf) + delta;
                uint8_t hdr[4]; write_u32(hdr, moov_sz);
                io->seek(io->user_data, 0); io->write(io->user_data, hdr, 4);
            }
        }
    } else if (udta_offset > 0) {
        io->seek(io->user_data, udta_offset + 8);
        io->write(io->user_data, chpl_buf, chpl_len);
    }

    free(chpl_buf);
    free(buf);
    return FAAM_OK;
}
