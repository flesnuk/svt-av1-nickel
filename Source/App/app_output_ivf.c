/*
* Copyright(c) 2022 Intel Corporation
*
* This source code is subject to the terms of the BSD 3-Clause Clear License and
* the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "app_config.h"
#include "app_output_ivf.h"

#define AV1_FOURCC 0x31305641 // used for ivf header
#define IVF_STREAM_HEADER_SIZE 32
#define IVF_FRAME_HEADER_SIZE 12

static __inline void mem_put_le32(void *vmem, int32_t val) {
    uint8_t *mem = (uint8_t *)vmem;

    mem[0] = (uint8_t)((val >> 0) & 0xff);
    mem[1] = (uint8_t)((val >> 8) & 0xff);
    mem[2] = (uint8_t)((val >> 16) & 0xff);
    mem[3] = (uint8_t)((val >> 24) & 0xff);
}

static __inline void mem_put_le16(void *vmem, int32_t val) {
    uint8_t *mem = (uint8_t *)vmem;

    mem[0] = (uint8_t)((val >> 0) & 0xff);
    mem[1] = (uint8_t)((val >> 8) & 0xff);
}

void write_ivf_stream_header(EbConfig *app_cfg, int32_t length) {
    char header[IVF_STREAM_HEADER_SIZE] = {'D', 'K', 'I', 'F'};
    mem_put_le16(header + 4, 0); // version
    mem_put_le16(header + 6, 32); // header size
    mem_put_le32(header + 8, AV1_FOURCC); // fourcc
    mem_put_le16(header + 12, app_cfg->input_padded_width); // width
    mem_put_le16(header + 14, app_cfg->input_padded_height); // height
    mem_put_le32(header + 16, app_cfg->config.frame_rate_numerator); // rate
    mem_put_le32(header + 20, app_cfg->config.frame_rate_denominator); // scale
    mem_put_le32(header + 24, length); // length
    mem_put_le32(header + 28, 0); // unused
    fwrite(header, 1, IVF_STREAM_HEADER_SIZE, app_cfg->bitstream_file);
}

void write_ivf_frame_header(EbConfig *app_cfg, uint32_t byte_count, uint64_t pts) {
    char header[IVF_FRAME_HEADER_SIZE];

    mem_put_le32(&header[0], (int32_t)byte_count);
    mem_put_le32(&header[4], (int32_t)(pts & 0xFFFFFFFF));
    mem_put_le32(&header[8], (int32_t)(pts >> 32));

    app_cfg->ivf_count++;
    fwrite(header, 1, IVF_FRAME_HEADER_SIZE, app_cfg->bitstream_file);
}

/* Read a little-endian 32-bit uint from a byte buffer */
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) |
        ((uint32_t)buf[3] << 24);
}

/* Read a little-endian 64-bit uint from a byte buffer */
static uint64_t read_le64(const uint8_t *buf) {
    return (uint64_t)read_le32(buf) | ((uint64_t)read_le32(buf + 4) << 32);
}

bool ivf_count_frames_and_seek(EbConfig *app_cfg) {
    FILE *f = app_cfg->bitstream_file;
    if (!f)
        return false;

    /* Rewind and validate the IVF stream header */
    rewind(f);

    uint8_t stream_hdr[IVF_STREAM_HEADER_SIZE];
    if (fread(stream_hdr, 1, IVF_STREAM_HEADER_SIZE, f) != IVF_STREAM_HEADER_SIZE)
        return false;

    /* Check the DKIF magic */
    if (stream_hdr[0] != 'D' || stream_hdr[1] != 'K' || stream_hdr[2] != 'I' || stream_hdr[3] != 'F')
        return false;

    /* Walk through all frame headers, count frames, track last valid position */
    int64_t  frame_count  = 0;
    int64_t  last_pts     = -1;
    int64_t  last_end_pos = IVF_STREAM_HEADER_SIZE; /* position after last complete frame */

    uint8_t frame_hdr[IVF_FRAME_HEADER_SIZE];
    for (;;) {
        /* Remember where this frame header starts */
        int64_t hdr_pos = ftello(f);
        if (hdr_pos < 0)
            break;

        size_t n = fread(frame_hdr, 1, IVF_FRAME_HEADER_SIZE, f);
        if (n != IVF_FRAME_HEADER_SIZE)
            break; /* EOF or truncated header – stop here */

        uint32_t frame_size = read_le32(frame_hdr);
        uint64_t pts        = read_le64(frame_hdr + 4);

        /* Sanity: frame_size of 0 or absurdly large indicates corruption */
        if (frame_size == 0 || frame_size > 256 * 1024 * 1024)
            break;

        /* Seek past the frame payload */
        if (fseeko(f, (int64_t)frame_size, SEEK_CUR) != 0)
            break;

        /* Verify we landed at expected position (detects truncated payload) */
        int64_t after = ftello(f);
        if (after < 0 || after != hdr_pos + IVF_FRAME_HEADER_SIZE + (int64_t)frame_size)
            break;

        /* This frame is complete */
        frame_count++;
        last_pts     = (int64_t)pts;
        last_end_pos = after;
    }

    app_cfg->resume_frame_count = frame_count;
    app_cfg->resume_last_pts    = last_pts;

    /* Seek to the end of the last valid frame so new data is appended */
    fseeko(f, last_end_pos, SEEK_SET);

    fprintf(stderr, "Resume: found %lld complete frame(s) in existing .ivf (last PTS=%lld). "
                    "Appending from frame %lld.\n",
            (long long)frame_count, (long long)last_pts, (long long)frame_count);

    return true;
}
