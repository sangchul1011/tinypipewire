/* SPDX-License-Identifier: MIT */

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "tpw_stream_internal.h"

/* How long tpw_stream_create() waits for PipeWire to confirm the
 * connection before failing fast instead of blocking indefinitely. */
#define TPW_CONNECT_TIMEOUT_NSEC (5 * SPA_NSEC_PER_SEC)

static pthread_mutex_t g_pw_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_pw_init_count = 0;

static void tpw_global_init(void)
{
    pthread_mutex_lock(&g_pw_init_mutex);
    if (g_pw_init_count == 0)
        pw_init(NULL, NULL);
    g_pw_init_count++;
    pthread_mutex_unlock(&g_pw_init_mutex);
}

static void tpw_global_deinit(void)
{
    pthread_mutex_lock(&g_pw_init_mutex);
    g_pw_init_count--;
    if (g_pw_init_count == 0)
        pw_deinit();
    pthread_mutex_unlock(&g_pw_init_mutex);
}

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

    /* Stop the loop thread before destroying resources so no callback
     * races with teardown; afterwards destruction needs no locking. */
    if (stream->loop)
        pw_thread_loop_stop(stream->loop);

    if (stream->pw_stream) {
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
    }
    if (stream->core) {
        pw_core_disconnect(stream->core);
        stream->core = NULL;
    }
    if (stream->context) {
        pw_context_destroy(stream->context);
        stream->context = NULL;
    }
    if (stream->loop) {
        pw_thread_loop_destroy(stream->loop);
        stream->loop = NULL;
    }
}

static void tpw_core_on_done(void* data, uint32_t id, int seq)
{
    struct tpw_stream* stream = data;
    if (id == PW_ID_CORE && seq == stream->pending_seq) {
        stream->sync_done = true;
        pw_thread_loop_signal(stream->loop, false);
    }
}

static void tpw_core_on_error(void* data, uint32_t id, int seq, int res, const char* message)
{
    struct tpw_stream* stream = data;
    (void)id;
    (void)seq;
    (void)message;
    stream->connect_result = (res < 0) ? res : -1;
    stream->sync_done = true;
    pw_thread_loop_signal(stream->loop, false);
}

static const struct pw_core_events tpw_core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = tpw_core_on_done,
    .error = tpw_core_on_error,
};

tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data)
{
    if (!callback || (type != TPW_STREAM_TYPE_AUDIO && type != TPW_STREAM_TYPE_VIDEO))
        return NULL;

    tpw_global_init();

    struct tpw_stream* stream = calloc(1, sizeof(*stream));
    if (!stream) {
        tpw_global_deinit();
        return NULL;
    }

    stream->type = type;
    stream->state = TPW_STREAM_STATE_CREATED;
    stream->data_cb = callback;
    stream->user_data = user_data;

    stream->loop = pw_thread_loop_new("tpw-stream-loop", NULL);
    if (!stream->loop)
        goto fail;

    if (pw_thread_loop_start(stream->loop) < 0)
        goto fail;

    pw_thread_loop_lock(stream->loop);

    stream->context = pw_context_new(pw_thread_loop_get_loop(stream->loop), NULL, 0);
    if (!stream->context) {
        pw_thread_loop_unlock(stream->loop);
        goto fail;
    }

    stream->core = pw_context_connect(stream->context, NULL, 0);
    if (!stream->core) {
        pw_thread_loop_unlock(stream->loop);
        goto fail;
    }

    pw_core_add_listener(stream->core, &stream->core_listener, &tpw_core_events, stream);

    stream->pending_seq = pw_core_sync(stream->core, PW_ID_CORE, 0);
    stream->sync_done = false;
    stream->connect_result = 0;

    struct timespec deadline;
    pw_thread_loop_get_time(stream->loop, &deadline, TPW_CONNECT_TIMEOUT_NSEC);
    while (!stream->sync_done) {
        if (pw_thread_loop_timed_wait_full(stream->loop, &deadline) < 0) {
            stream->connect_result = -1;
            break;
        }
    }

    spa_hook_remove(&stream->core_listener);
    pw_thread_loop_unlock(stream->loop);

    if (stream->connect_result < 0)
        goto fail;

    return (tpw_stream_h)stream;

fail:
    tpw_stream_teardown(stream);
    free(stream);
    tpw_global_deinit();
    return NULL;
}

int tpw_stream_internal_connect(struct tpw_stream* stream, const struct spa_pod** params, uint32_t n_params)
{
    if (stream->pw_stream) {
        pw_thread_loop_lock(stream->loop);
        pw_stream_destroy(stream->pw_stream);
        stream->pw_stream = NULL;
        pw_thread_loop_unlock(stream->loop);
    }

    const char* media_type = (stream->type == TPW_STREAM_TYPE_AUDIO) ? "Audio" : "Video";
    struct pw_properties* props =
        pw_properties_new(PW_KEY_MEDIA_TYPE, media_type, PW_KEY_MEDIA_CATEGORY, "Capture", NULL);

    pw_thread_loop_lock(stream->loop);

    stream->pw_stream = pw_stream_new(stream->core, "tpw-stream", props);
    if (!stream->pw_stream) {
        pw_thread_loop_unlock(stream->loop);
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
        pw_thread_loop_unlock(stream->loop);
        return TPW_STREAM_ERR_CONNECT_FAILED;
    }

    /* Stay paused until tpw_stream_start() is called explicitly. */
    pw_stream_set_active(stream->pw_stream, false);

    pw_thread_loop_unlock(stream->loop);
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

    pw_thread_loop_lock(stream->loop);
    pw_stream_set_active(stream->pw_stream, true);
    pw_thread_loop_unlock(stream->loop);

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

    pw_thread_loop_lock(stream->loop);
    pw_stream_set_active(stream->pw_stream, false);
    pw_thread_loop_unlock(stream->loop);

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
    tpw_global_deinit();
}
