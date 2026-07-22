/* SPDX-License-Identifier: MIT */

#include <string.h>

#include <spa/param/audio/dsp-utils.h>

#include "tpw_spa_format_internal.h"

static const struct {
    const char* name;
    enum spa_audio_format format;
} tpw_audio_sample_formats[] = {
    { "S16", SPA_AUDIO_FORMAT_S16 },
    { "S24", SPA_AUDIO_FORMAT_S24 },
    { "S32", SPA_AUDIO_FORMAT_S32 },
    { "F32", SPA_AUDIO_FORMAT_F32 },
};

enum spa_audio_format tpw_spa_lookup_audio_format(const char* name)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(tpw_audio_sample_formats); i++) {
        if (strcmp(tpw_audio_sample_formats[i].name, name) == 0)
            return tpw_audio_sample_formats[i].format;
    }
    return SPA_AUDIO_FORMAT_UNKNOWN;
}

const struct spa_pod* tpw_spa_build_audio_format(struct spa_pod_builder* b, const tpw_audio_config* config,
                                                  enum spa_audio_format fmt)
{
    struct spa_audio_info_raw info = {
        .format = fmt,
        .rate = (uint32_t)config->sample_rate,
        .channels = (uint32_t)config->channels,
    };
    return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

static const struct {
    const char* name;
    enum spa_video_format format;
} tpw_pixel_formats[] = {
    { "RGB", SPA_VIDEO_FORMAT_RGB },
    { "YUYV", SPA_VIDEO_FORMAT_YUY2 },
    { "NV12", SPA_VIDEO_FORMAT_NV12 },
    { "I420", SPA_VIDEO_FORMAT_I420 },
};

enum spa_video_format tpw_spa_lookup_pixel_format(const char* name)
{
    for (size_t i = 0; i < SPA_N_ELEMENTS(tpw_pixel_formats); i++) {
        if (strcmp(tpw_pixel_formats[i].name, name) == 0)
            return tpw_pixel_formats[i].format;
    }
    return SPA_VIDEO_FORMAT_UNKNOWN;
}

const struct spa_pod* tpw_spa_build_video_format(struct spa_pod_builder* b, const tpw_video_config* config,
                                                  enum spa_video_format fmt)
{
    struct spa_rectangle size = SPA_RECTANGLE((uint32_t)config->width, (uint32_t)config->height);

    struct spa_pod_frame f;
    spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(fmt),
        SPA_FORMAT_VIDEO_size,   SPA_POD_Rectangle(&size), 0);
    if (config->fps > 0) {
        /* Fixed frame rate requested by the caller. */
        struct spa_fraction fr = SPA_FRACTION((uint32_t)config->fps, 1);
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&fr), 0);
    } else {
        /* fps == 0: let the source negotiate the frame rate. */
        struct spa_fraction fr_def = SPA_FRACTION(30, 1);
        struct spa_fraction fr_min = SPA_FRACTION(0, 1);
        struct spa_fraction fr_max = SPA_FRACTION(1000, 1);
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate,
            SPA_POD_CHOICE_RANGE_Fraction(&fr_def, &fr_min, &fr_max), 0);
    }
    return spa_pod_builder_pop(b, &f);
}

const struct spa_pod* tpw_spa_build_signal_format(struct spa_pod_builder* b)
{
    struct spa_audio_info_dsp info = { .format = SPA_AUDIO_FORMAT_DSP_F32 };
    return spa_format_audio_dsp_build(b, SPA_PARAM_EnumFormat, &info);
}

const struct spa_pod* tpw_spa_build_event_format(struct spa_pod_builder* b)
{
    struct spa_pod_frame f;
    spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_application),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_control), 0);
    return spa_pod_builder_pop(b, &f);
}
