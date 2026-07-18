/* SPDX-License-Identifier: MIT */

#include <string.h>

#include "tpw_stream_internal.h"

static const struct {
    const char* name;
    enum spa_video_format format;
} tpw_pixel_formats[] = {
    { "RGB", SPA_VIDEO_FORMAT_RGB },
    { "YUYV", SPA_VIDEO_FORMAT_YUY2 },
    { "NV12", SPA_VIDEO_FORMAT_NV12 },
    { "I420", SPA_VIDEO_FORMAT_I420 },
};

static enum spa_video_format tpw_lookup_pixel_format(const char* name)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(tpw_pixel_formats); i++) {
        if (strcmp(tpw_pixel_formats[i].name, name) == 0)
            return tpw_pixel_formats[i].format;
    }
    return SPA_VIDEO_FORMAT_UNKNOWN;
}

int tpw_stream_set_video_config(tpw_stream_h handle, const tpw_video_config* config)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream || stream->type != TPW_STREAM_TYPE_VIDEO || !config || !config->pixel_format)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (config->width <= 0 || config->height <= 0 || config->fps < 0)
        return TPW_STREAM_ERR_INVALID_FORMAT;

    enum spa_video_format fmt = tpw_lookup_pixel_format(config->pixel_format);
    if (fmt == SPA_VIDEO_FORMAT_UNKNOWN)
        return TPW_STREAM_ERR_INVALID_FORMAT;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_rectangle size = SPA_RECTANGLE((uint32_t)config->width, (uint32_t)config->height);
    const struct spa_pod* params[1];

    struct spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(&b,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(fmt),
        SPA_FORMAT_VIDEO_size,   SPA_POD_Rectangle(&size), 0);
    if (config->fps > 0) {
        /* Fixed frame rate requested by the caller. */
        struct spa_fraction fr = SPA_FRACTION((uint32_t)config->fps, 1);
        spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&fr), 0);
    } else {
        /* fps == 0: let the source negotiate the frame rate. */
        struct spa_fraction fr_def = SPA_FRACTION(30, 1);
        struct spa_fraction fr_min = SPA_FRACTION(0, 1);
        struct spa_fraction fr_max = SPA_FRACTION(1000, 1);
        spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_framerate,
            SPA_POD_CHOICE_RANGE_Fraction(&fr_def, &fr_min, &fr_max), 0);
    }
    params[0] = spa_pod_builder_pop(&b, &f);

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
