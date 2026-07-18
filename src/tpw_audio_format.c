/* SPDX-License-Identifier: MIT */

#include "tpw_stream_internal.h"

int tpw_stream_set_audio_config(tpw_stream_h handle, const tpw_audio_config* config)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream || stream->type != TPW_STREAM_TYPE_AUDIO || !config)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (config->sample_rate <= 0 || config->channels <= 0)
        return TPW_STREAM_ERR_INVALID_FORMAT;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_S16,
        .rate = (uint32_t)config->sample_rate,
        .channels = (uint32_t)config->channels,
    };
    const struct spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int res = tpw_stream_internal_connect(stream, params, 1);
    if (res < 0)
        return res;

    stream->format.audio.sample_rate = config->sample_rate;
    stream->format.audio.channels = config->channels;
    stream->format_set = true;
    stream->state = TPW_STREAM_STATE_FORMAT_SET;
    return TPW_STREAM_OK;
}
