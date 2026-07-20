/* SPDX-License-Identifier: MIT */

#include <pthread.h>
#include <time.h>

#include "tpw_log_internal.h"
#include "tpw_pw_core_internal.h"

/* How long tpw_pw_core_connect() waits for PipeWire to confirm the
 * connection before failing fast instead of blocking indefinitely. */
#define TPW_CONNECT_TIMEOUT_NSEC (5 * SPA_NSEC_PER_SEC)

static pthread_mutex_t g_pw_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_pw_init_count = 0;

void tpw_pw_global_init(void)
{
    pthread_mutex_lock(&g_pw_init_mutex);
    if (g_pw_init_count == 0)
        pw_init(NULL, NULL);
    g_pw_init_count++;
    pthread_mutex_unlock(&g_pw_init_mutex);
}

void tpw_pw_global_deinit(void)
{
    pthread_mutex_lock(&g_pw_init_mutex);
    g_pw_init_count--;
    if (g_pw_init_count == 0)
        pw_deinit();
    pthread_mutex_unlock(&g_pw_init_mutex);
}

static void tpw_pw_core_on_done(void* data, uint32_t id, int seq)
{
    struct tpw_pw_core_conn* conn = data;
    if (id == PW_ID_CORE && seq == conn->pending_seq) {
        conn->sync_done = true;
        pw_thread_loop_signal(conn->loop, false);
    }
}

static void tpw_pw_core_on_error(void* data, uint32_t id, int seq, int res, const char* message)
{
    struct tpw_pw_core_conn* conn = data;
    (void)id;
    (void)seq;
    (void)message;
    conn->connect_result = (res < 0) ? res : -1;
    conn->sync_done = true;
    pw_thread_loop_signal(conn->loop, false);
}

static const struct pw_core_events tpw_pw_core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = tpw_pw_core_on_done,
    .error = tpw_pw_core_on_error,
};

int tpw_pw_core_connect(struct tpw_pw_core_conn* conn, const char* loop_name)
{
    conn->loop = pw_thread_loop_new(loop_name, NULL);
    if (!conn->loop) {
        tpw_log_error("'%s': failed to create thread loop", loop_name);
        return -1;
    }

    if (pw_thread_loop_start(conn->loop) < 0) {
        tpw_log_error("'%s': failed to start thread loop", loop_name);
        return -1;
    }

    pw_thread_loop_lock(conn->loop);

    conn->context = pw_context_new(pw_thread_loop_get_loop(conn->loop), NULL, 0);
    if (!conn->context) {
        pw_thread_loop_unlock(conn->loop);
        tpw_log_error("'%s': failed to create pipewire context", loop_name);
        return -1;
    }

    conn->core = pw_context_connect(conn->context, NULL, 0);
    if (!conn->core) {
        pw_thread_loop_unlock(conn->loop);
        tpw_log_error("'%s': failed to connect to pipewire core", loop_name);
        return -1;
    }

    pw_core_add_listener(conn->core, &conn->core_listener, &tpw_pw_core_events, conn);

    conn->pending_seq = pw_core_sync(conn->core, PW_ID_CORE, 0);
    conn->sync_done = false;
    conn->connect_result = 0;

    struct timespec deadline;
    pw_thread_loop_get_time(conn->loop, &deadline, TPW_CONNECT_TIMEOUT_NSEC);
    while (!conn->sync_done) {
        if (pw_thread_loop_timed_wait_full(conn->loop, &deadline) < 0) {
            conn->connect_result = -1;
            break;
        }
    }

    spa_hook_remove(&conn->core_listener);
    pw_thread_loop_unlock(conn->loop);

    if (conn->connect_result < 0)
        tpw_log_error("'%s': pipewire core sync failed or timed out (result=%d)", loop_name, conn->connect_result);

    return conn->connect_result;
}

void tpw_pw_core_teardown(struct tpw_pw_core_conn* conn)
{
    if (!conn)
        return;

    /* Stop the loop thread before destroying resources so no callback
     * races with teardown; afterwards destruction needs no locking. */
    if (conn->loop)
        pw_thread_loop_stop(conn->loop);

    if (conn->core) {
        pw_core_disconnect(conn->core);
        conn->core = NULL;
    }
    if (conn->context) {
        pw_context_destroy(conn->context);
        conn->context = NULL;
    }
    if (conn->loop) {
        pw_thread_loop_destroy(conn->loop);
        conn->loop = NULL;
    }
}
