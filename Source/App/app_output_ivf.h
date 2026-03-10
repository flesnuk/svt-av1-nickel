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

#ifndef EbAppOutputivf_h
#define EbAppOutputivf_h

#include <stdint.h>

#include "app_config.h"

void write_ivf_stream_header(EbConfig *app_cfg, int32_t length);
void write_ivf_frame_header(EbConfig *app_cfg, uint32_t byte_count, uint64_t pts);

/* Resume support: read an existing .ivf file, count the number of complete frames,
 * set app_cfg->resume_frame_count and app_cfg->resume_last_pts, and seek the
 * file position to the byte right after the last valid frame (ready for appending).
 * Returns true on success (file had a valid IVF header), false otherwise. */
bool ivf_count_frames_and_seek(EbConfig *app_cfg);

#endif
