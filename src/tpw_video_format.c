/* SPDX-License-Identifier: MIT */

#include "tpw_spa_format_internal.h"
#include "tpw_stream_internal.h"

int tpw_stream_set_video_config(tpw_stream_h handle, const tpw_video_config* config)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream || stream->type != TPW_STREAM_TYPE_VIDEO || !config || !config->pixel_format)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (config->width <= 0 || config->height <= 0 || config->fps < 0)
        return TPW_STREAM_ERR_INVALID_FORMAT;

    enum spa_video_format fmt = tpw_spa_lookup_pixel_format(config->pixel_format);
    if (fmt == SPA_VIDEO_FORMAT_UNKNOWN)
        return TPW_STREAM_ERR_INVALID_FORMAT;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = tpw_spa_build_video_format(&b, config, fmt);

    int res = tpw_stream_internal_connect(stream, params, 1);
    if (res < 0)
        return res;

    stream->format.video.width = config->width;
    stream->format.video.height = config->height;
    stream->format.video.format = fmt;
    stream->format_set = true;
    stream->state = TPW_STREAM_STATE_FORMAT_SET;
    return TPW_STREAM_OK;
}
