/* SPDX-License-Identifier: MIT */

#include <stdlib.h>

#include "tpw_stream_internal.h"

void tpw_stream_on_state_changed(void* data, enum pw_stream_state old, enum pw_stream_state state,
                                  const char* error)
{
    struct tpw_stream* stream = data;
    (void)error;

    bool lost_source = (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) &&
                        (old == PW_STREAM_STATE_STREAMING || old == PW_STREAM_STATE_PAUSED);

    if (lost_source && stream->state == TPW_STREAM_STATE_RUNNING) {
        stream->state = TPW_STREAM_STATE_STOPPED;
        if (stream->error_cb)
            stream->error_cb((tpw_stream_h)stream, TPW_STREAM_ERR_SOURCE_UNAVAILABLE, stream->user_data);
    }
}

static const struct pw_stream_events tpw_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = tpw_stream_on_state_changed,
    .process = tpw_stream_on_process,
};

static void tpw_stream_teardown(struct tpw_stream* stream)
{
    if (!stream)
        return;

    if (stream->conn.loop && stream->pw_stream) {
        pw_thread_loop_lock(stream->conn.loop);
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
    } else if (stream->pw_stream) {
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
    }

    tpw_pw_core_teardown(&stream->conn);
}

tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data)
{
    if (!callback || (type != TPW_STREAM_TYPE_AUDIO && type != TPW_STREAM_TYPE_VIDEO))
        return NULL;

    tpw_pw_global_init();

    struct tpw_stream* stream = calloc(1, sizeof(*stream));
    if (!stream) {
        tpw_pw_global_deinit();
        return NULL;
    }

    stream->type = type;
    stream->state = TPW_STREAM_STATE_CREATED;
    stream->data_cb = callback;
    stream->user_data = user_data;

    if (tpw_pw_core_connect(&stream->conn, "tpw-stream-loop") < 0) {
        tpw_stream_teardown(stream);
        free(stream);
        tpw_pw_global_deinit();
        return NULL;
    }

    return (tpw_stream_h)stream;
}

int tpw_stream_internal_connect(struct tpw_stream* stream, const struct spa_pod** params, uint32_t n_params)
{
    if (stream->pw_stream) {
        pw_thread_loop_lock(stream->conn.loop);
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
    }

    const char* media_type = (stream->type == TPW_STREAM_TYPE_AUDIO) ? "Audio" : "Video";
    struct pw_properties* props =
        pw_properties_new(PW_KEY_MEDIA_TYPE, media_type, PW_KEY_MEDIA_CATEGORY, "Capture", NULL);

    pw_thread_loop_lock(stream->conn.loop);

    stream->pw_stream = pw_stream_new(stream->conn.core, "tpw-stream", props);
    if (!stream->pw_stream) {
        pw_thread_loop_unlock(stream->conn.loop);
        return TPW_STREAM_ERR_CONNECT_FAILED;
    }

    pw_stream_add_listener(stream->pw_stream, &stream->stream_listener, &tpw_stream_events, stream);

    int res = pw_stream_connect(stream->pw_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                 PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                     PW_STREAM_FLAG_RT_PROCESS,
                                 params, n_params);
    if (res < 0) {
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->conn.loop);
        return TPW_STREAM_ERR_CONNECT_FAILED;
    }

    /* Stay paused until tpw_stream_start() is called explicitly. */
    pw_stream_set_active(stream->pw_stream, false);

    pw_thread_loop_unlock(stream->conn.loop);
    return TPW_STREAM_OK;
}

int tpw_stream_set_error_cb(tpw_stream_h handle, tpw_stream_error_cb callback)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream)
        return TPW_STREAM_ERR_INVALID_ARG;

    stream->error_cb = callback;
    return TPW_STREAM_OK;
}

int tpw_stream_start(tpw_stream_h handle)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (!stream->format_set || !stream->pw_stream)
        return TPW_STREAM_ERR_NOT_CONFIGURED;

    pw_thread_loop_lock(stream->conn.loop);
    pw_stream_set_active(stream->pw_stream, true);
    pw_thread_loop_unlock(stream->conn.loop);

    stream->state = TPW_STREAM_STATE_RUNNING;
    return TPW_STREAM_OK;
}

int tpw_stream_stop(tpw_stream_h handle)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (stream->state != TPW_STREAM_STATE_RUNNING)
        return TPW_STREAM_OK;

    pw_thread_loop_lock(stream->conn.loop);
    pw_stream_set_active(stream->pw_stream, false);
    pw_thread_loop_unlock(stream->conn.loop);

    stream->state = TPW_STREAM_STATE_STOPPED;
    return TPW_STREAM_OK;
}

void tpw_stream_destroy(tpw_stream_h handle)
{
    struct tpw_stream* stream = (struct tpw_stream*)handle;
    if (!stream)
        return;

    if (stream->state == TPW_STREAM_STATE_RUNNING)
        tpw_stream_stop(handle);

    tpw_stream_teardown(stream);
    free(stream);
    tpw_pw_global_deinit();
}
