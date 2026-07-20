/* SPDX-License-Identifier: MIT */

#include <stdlib.h>
#include <string.h>

#include "tpw_filter_internal.h"
#include "tpw_log_internal.h"
#include "tpw_spa_format_internal.h"

static void* tpw_filter_add_port_common(struct tpw_filter* filter, tpw_filter_port_direction direction,
                                         const struct spa_pod** params)
{
    enum spa_direction pw_dir = (direction == TPW_FILTER_PORT_INPUT) ? SPA_DIRECTION_INPUT : SPA_DIRECTION_OUTPUT;

    pw_thread_loop_lock(filter->conn.loop);
    void* port_data =
        pw_filter_add_port(filter->pw_filter, pw_dir, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                            sizeof(struct tpw_filter_port), NULL, params, 1);
    pw_thread_loop_unlock(filter->conn.loop);
    return port_data;
}

tpw_filter_port_h tpw_filter_add_audio_port(tpw_filter_h handle, tpw_filter_port_direction direction,
                                             const tpw_audio_config* config)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter || !config || filter->state != TPW_FILTER_STATE_CREATED)
        return NULL;
    if (config->sample_rate <= 0 || config->channels <= 0)
        return NULL;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = tpw_spa_build_audio_format(&b, config);

    void* port_data = tpw_filter_add_port_common(filter, direction, params);
    if (!port_data)
        return NULL;

    struct tpw_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = TPW_STREAM_TYPE_AUDIO;
    port->config.audio.sample_rate = config->sample_rate;
    port->config.audio.channels = config->channels;

    if (!tpw_filter_add_port_to_list(filter, port))
        return NULL;

    return (tpw_filter_port_h)port;
}

int tpw_filter_push_port_data(tpw_filter_h handle, tpw_filter_port_h port_handle, const void* data, size_t size)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    struct tpw_filter_port* port = (struct tpw_filter_port*)port_handle;
    if (!filter || !port || port->filter != filter || port->direction != TPW_FILTER_PORT_INPUT)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (size > 0 && !data)
        return TPW_STREAM_ERR_INVALID_ARG;

    pw_thread_loop_lock(filter->conn.loop);

    if (size > port->pushed_capacity) {
        void* grown = realloc(port->pushed_data, size);
        if (!grown) {
            pw_thread_loop_unlock(filter->conn.loop);
            tpw_log_error("filter '%s': failed to grow push buffer to %zu bytes",
                          filter->name ? filter->name : "tpw-filter", size);
            return TPW_STREAM_ERR_INVALID_ARG;
        }
        port->pushed_data = grown;
        port->pushed_capacity = size;
    }
    if (size > 0)
        memcpy(port->pushed_data, data, size);
    port->pushed_size = size;

    pw_thread_loop_unlock(filter->conn.loop);
    return TPW_STREAM_OK;
}

tpw_filter_port_h tpw_filter_add_video_port(tpw_filter_h handle, tpw_filter_port_direction direction,
                                             const tpw_video_config* config)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter || !config || !config->pixel_format || filter->state != TPW_FILTER_STATE_CREATED)
        return NULL;
    if (config->width <= 0 || config->height <= 0 || config->fps < 0)
        return NULL;

    enum spa_video_format fmt = tpw_spa_lookup_pixel_format(config->pixel_format);
    if (fmt == SPA_VIDEO_FORMAT_UNKNOWN)
        return NULL;

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    params[0] = tpw_spa_build_video_format(&b, config, fmt);

    void* port_data = tpw_filter_add_port_common(filter, direction, params);
    if (!port_data)
        return NULL;

    struct tpw_filter_port* port = port_data;
    port->filter = filter;
    port->direction = direction;
    port->media_type = TPW_STREAM_TYPE_VIDEO;
    port->config.video.width = config->width;
    port->config.video.height = config->height;
    port->config.video.format = fmt;

    if (!tpw_filter_add_port_to_list(filter, port))
        return NULL;

    return (tpw_filter_port_h)port;
}
