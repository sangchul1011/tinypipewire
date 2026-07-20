/* SPDX-License-Identifier: MIT */

#ifndef TPW_PW_CORE_INTERNAL_H
#define TPW_PW_CORE_INTERNAL_H

#include <stdbool.h>

#include <pipewire/pipewire.h>

/* Shared PipeWire thread-loop/context/core connection, reused by both
 * tpw_stream and tpw_filter so each owns a fully independent PipeWire
 * client without duplicating the setup/teardown logic. */
struct tpw_pw_core_conn {
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;

    struct spa_hook core_listener;
    int pending_seq;
    int connect_result;
    bool sync_done;
};

/* Increments the process-wide pw_init() refcount, calling pw_init() on
 * the first call. Must be paired with tpw_pw_global_deinit(). */
void tpw_pw_global_init(void);

/* Decrements the process-wide pw_init() refcount, calling pw_deinit()
 * when it reaches zero. */
void tpw_pw_global_deinit(void);

/* Starts a thread-loop, creates a context on it, and connects a core,
 * waiting (bounded) for PipeWire to confirm the connection before
 * returning. Returns 0 on success, a negative tpw_stream_error-style
 * code on failure (conn's fields are left safe to pass to
 * tpw_pw_core_teardown() either way). */
int tpw_pw_core_connect(struct tpw_pw_core_conn* conn, const char* loop_name);

/* Stops and destroys whatever conn holds (safe to call on a
 * partially-initialized or already-torn-down conn). */
void tpw_pw_core_teardown(struct tpw_pw_core_conn* conn);

#endif /* TPW_PW_CORE_INTERNAL_H */
