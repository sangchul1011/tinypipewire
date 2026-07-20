/* SPDX-License-Identifier: MIT */

#ifndef TPW_FILTER_INTERNAL_H
#define TPW_FILTER_INTERNAL_H

#include <stdbool.h>

#include <pipewire/filter.h>
#include <spa/param/video/format-utils.h>

#include "tpw/tpw_filter.h"
#include "tpw_pw_core_internal.h"

enum tpw_filter_state {
    TPW_FILTER_STATE_CREATED,
    TPW_FILTER_STATE_RUNNING,
    TPW_FILTER_STATE_STOPPED,
};

struct tpw_filter_audio_port_state {
    int sample_rate;
    int channels;
};

struct tpw_filter_video_port_state {
    int width;
    int height;
    enum spa_video_format format;
};

/* One input or output port on a filter. This struct IS the PipeWire
 * port's user-data block (pw_filter_add_port() allocates it inline), so
 * a tpw_filter_port_h is just this pointer. Only resolved values are
 * kept (not the caller's tpw_video_config, whose pixel_format pointer
 * isn't guaranteed to outlive the call that added the port). */
struct tpw_filter_port {
    struct tpw_filter* filter;
    tpw_filter_port_direction direction;
    tpw_stream_type media_type;
    union {
        struct tpw_filter_audio_port_state audio;
        struct tpw_filter_video_port_state video;
    } config;

    /* Staged application-pushed buffer (input ports only), consumed and
     * cleared on the next processing cycle. */
    void* pushed_data;
    size_t pushed_size;
    size_t pushed_capacity;
};

/* One multi-port filter. */
struct tpw_filter {
    char* name;
    enum tpw_filter_state state;

    struct tpw_filter_port** ports;
    size_t n_ports;
    size_t ports_capacity;

    tpw_filter_process_cb process_cb;
    tpw_filter_error_cb error_cb;
    void* user_data;

    struct tpw_pw_core_conn conn;
    struct pw_filter* pw_filter;
    struct spa_hook filter_listener;
};

/* Appends `port` to filter->ports, growing the array as needed. Returns
 * false on allocation failure. */
bool tpw_filter_add_port_to_list(struct tpw_filter* filter, struct tpw_filter_port* port);

/* .process callback registered on the underlying pw_filter; assembles
 * one tpw_filter_port_buffer per port (consuming any staged pushed
 * buffer first) and invokes the developer's process_cb once. */
void tpw_filter_on_process(void* data, struct spa_io_position* position);

/* .param_changed callback registered on the underlying pw_filter;
 * treats a port's format being cleared (param == NULL for
 * SPA_PARAM_Format) as that port's source becoming unavailable. */
void tpw_filter_on_param_changed(void* data, void* port_data, uint32_t id, const struct spa_pod* param);

#endif /* TPW_FILTER_INTERNAL_H */
