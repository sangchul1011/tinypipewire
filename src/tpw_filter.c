/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/keys.h>
#include <spa/utils/dict.h>

#include "tpw_filter_internal.h"
#include "tpw_log_internal.h"

void tpw_filter_on_param_changed(void* data, void* port_data, uint32_t id, const struct spa_pod* param)
{
    struct tpw_filter* filter = data;
    struct tpw_filter_port* port = port_data;

    if (id != SPA_PARAM_Format || !port)
        return;

    if (param != NULL) {
        /* Format negotiated. A DMABUF port advertises its DmaBuf Buffers
         * param now (deferred from add time, which crashes 1.0.5); a
         * no-op for every other port. */
        tpw_filter_dmabuf_update_params(port);
        return;
    }

    /* param == NULL: the port's negotiated format was cleared — its source
     * is gone, or a DMABUF port's source could not provide DMABUF. */
    if (port->use_dmabuf)
        tpw_filter_dmabuf_log_unavailable(port);
    else
        tpw_log_warning("filter '%s': a port's source became unavailable",
                        filter->name ? filter->name : "tpw-filter");

    if (filter->error_cb)
        filter->error_cb((tpw_filter_h)filter, (tpw_filter_port_h)port, TPW_STREAM_ERR_SOURCE_UNAVAILABLE,
                          filter->user_data);
}

static const struct pw_filter_events tpw_filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .param_changed = tpw_filter_on_param_changed,
    .process = tpw_filter_on_process,
};

bool tpw_filter_add_port_to_list(struct tpw_filter* filter, struct tpw_filter_port* port)
{
    if (filter->n_ports == filter->ports_capacity) {
        size_t new_cap = filter->ports_capacity == 0 ? 4 : filter->ports_capacity * 2;
        struct tpw_filter_port** grown = realloc(filter->ports, new_cap * sizeof(*grown));
        if (!grown)
            return false;
        filter->ports = grown;
        filter->ports_capacity = new_cap;
    }
    filter->ports[filter->n_ports++] = port;
    return true;
}

static void tpw_filter_teardown(struct tpw_filter* filter)
{
    if (!filter)
        return;

    if (filter->conn.loop && filter->pw_filter) {
        pw_thread_loop_lock(filter->conn.loop);
        pw_filter_destroy(filter->pw_filter);
        filter->pw_filter = NULL;
        pw_thread_loop_unlock(filter->conn.loop);
    } else if (filter->pw_filter) {
        pw_filter_destroy(filter->pw_filter);
        filter->pw_filter = NULL;
    }

    tpw_pw_core_teardown(&filter->conn);
}

tpw_filter_h tpw_filter_create(const char* name, tpw_filter_process_cb callback, void* user_data)
{
    if (!callback)
        return NULL;

    tpw_pw_global_init();

    struct tpw_filter* filter = calloc(1, sizeof(*filter));
    if (!filter) {
        tpw_pw_global_deinit();
        return NULL;
    }

    filter->state = TPW_FILTER_STATE_CREATED;
    filter->process_cb = callback;
    filter->user_data = user_data;

    if (name && *name) {
        filter->name = strdup(name);
        if (!filter->name) {
            free(filter);
            tpw_pw_global_deinit();
            return NULL;
        }
    }

    if (tpw_pw_core_connect(&filter->conn, "tpw-filter-loop") < 0) {
        tpw_filter_teardown(filter);
        free(filter->name);
        free(filter);
        tpw_pw_global_deinit();
        return NULL;
    }

    pw_thread_loop_lock(filter->conn.loop);
    /* No PipeWire-level linking is exposed to callers, so ports may stay
     * unlinked forever; node.always-process keeps .process() firing
     * regardless so the app can drive I/O via push_port_data. */
    struct pw_properties* props = pw_properties_new(PW_KEY_NODE_ALWAYS_PROCESS, "true", NULL);
    filter->pw_filter =
        pw_filter_new(filter->conn.core, filter->name ? filter->name : "tpw-filter", props);
    if (!filter->pw_filter) {
        pw_thread_loop_unlock(filter->conn.loop);
        tpw_log_error("filter '%s': failed to create pipewire filter", filter->name ? filter->name : "tpw-filter");
        tpw_filter_teardown(filter);
        free(filter->name);
        free(filter);
        tpw_pw_global_deinit();
        return NULL;
    }

    pw_filter_add_listener(filter->pw_filter, &filter->filter_listener, &tpw_filter_events, filter);
    pw_thread_loop_unlock(filter->conn.loop);

    return (tpw_filter_h)filter;
}

int tpw_filter_set_error_cb(tpw_filter_h handle, tpw_filter_error_cb callback)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter)
        return TPW_STREAM_ERR_INVALID_ARG;

    filter->error_cb = callback;
    return TPW_STREAM_OK;
}

int tpw_filter_set_period_hint(tpw_filter_h handle, uint32_t max_period_ns)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (filter->state != TPW_FILTER_STATE_CREATED)
        return TPW_STREAM_ERR_NOT_CONFIGURED;

    filter->period_hint_ns = max_period_ns;
    return TPW_STREAM_OK;
}

/* Numerator of the node.latency "num/48000" time ratio for a period hint,
 * floored (never coarser than requested) and clamped to at least 1. The
 * denominator 48000 is an arbitrary reference — PipeWire rescales the ratio
 * to the actual graph clock, so no real sample rate is assumed. */
uint32_t tpw_filter_period_hint_num(uint32_t period_ns)
{
    uint64_t num = (uint64_t)period_ns * 48000u / 1000000000ULL;
    return num < 1 ? 1u : (uint32_t)num;
}

/* Applies the period hint as a node.latency preference; a no-op when unset.
 * PipeWire rescales the ratio to the graph clock. */
static void tpw_filter_apply_period_hint(struct tpw_filter* filter)
{
    if (filter->period_hint_ns == 0)
        return;

    char latency[32];
    snprintf(latency, sizeof(latency), "%u/48000", tpw_filter_period_hint_num(filter->period_hint_ns));

    struct spa_dict_item items[] = { SPA_DICT_ITEM_INIT(PW_KEY_NODE_LATENCY, latency) };
    struct spa_dict dict = SPA_DICT_INIT(items, 1);
    pw_filter_update_properties(filter->pw_filter, NULL, &dict);
}

int tpw_filter_start(tpw_filter_h handle)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (filter->n_ports == 0)
        return TPW_STREAM_ERR_NOT_CONFIGURED;

    pw_thread_loop_lock(filter->conn.loop);
    if (filter->state == TPW_FILTER_STATE_CREATED) {
        /* First start: ports were already added with their format params,
         * so connecting now negotiates and activates them together. The
         * period hint (if any) is a node property, so it must be set before
         * connect. */
        tpw_filter_apply_period_hint(filter);
        int res = pw_filter_connect(filter->pw_filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0);
        if (res < 0) {
            pw_thread_loop_unlock(filter->conn.loop);
            tpw_log_error("filter '%s': failed to connect (result=%d)", filter->name ? filter->name : "tpw-filter", res);
            return TPW_STREAM_ERR_CONNECT_FAILED;
        }
    } else {
        pw_filter_set_active(filter->pw_filter, true);
    }
    pw_thread_loop_unlock(filter->conn.loop);

    filter->state = TPW_FILTER_STATE_RUNNING;
    return TPW_STREAM_OK;
}

int tpw_filter_stop(tpw_filter_h handle)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter)
        return TPW_STREAM_ERR_INVALID_ARG;
    if (filter->state != TPW_FILTER_STATE_RUNNING)
        return TPW_STREAM_OK;

    /* Drop any links first: they were made against the running graph and a
     * restart re-links explicitly. */
    tpw_filter_release_all_links(filter);

    pw_thread_loop_lock(filter->conn.loop);
    pw_filter_set_active(filter->pw_filter, false);
    /* Return any held buffer to the pool now that processing is paused, and
     * reset per-port hold state so a restart begins holding afresh. */
    for (size_t i = 0; i < filter->n_ports; i++) {
        struct tpw_filter_port* port = filter->ports[i];
        if (port->held) {
            pw_filter_queue_buffer(port, port->held);
            port->held = NULL;
        }
        port->has_held = false;
        port->held_data = NULL;
        port->held_dmabuf_buf = NULL;
        port->current_dmabuf_buf = NULL;
    }
    pw_thread_loop_unlock(filter->conn.loop);

    filter->state = TPW_FILTER_STATE_STOPPED;
    return TPW_STREAM_OK;
}

void tpw_filter_destroy(tpw_filter_h handle)
{
    struct tpw_filter* filter = (struct tpw_filter*)handle;
    if (!filter)
        return;

    if (filter->state == TPW_FILTER_STATE_RUNNING)
        tpw_filter_stop(handle);
    else
        tpw_filter_release_all_links(filter);

    /* The registry and any links must go while the loop still runs, since
     * destroying their proxies talks to the server. */
    if (filter->conn.loop) {
        pw_thread_loop_lock(filter->conn.loop);
        tpw_pw_registry_teardown(&filter->registry);
        pw_thread_loop_unlock(filter->conn.loop);
    }

    /* The ports themselves are owned by pw_filter and freed by
     * pw_filter_destroy() inside tpw_filter_teardown(); only the extra
     * heap allocations for each port's push-staging buffer/events are
     * ours, so they must be freed before teardown while the port
     * structs are still valid. */
    for (size_t i = 0; i < filter->n_ports; i++) {
        if (filter->ports[i]) {
            free(filter->ports[i]->pushed_data);
            tpw_filter_event_free_port(filter->ports[i]);
        }
    }
    free(filter->ports);

    tpw_filter_teardown(filter);

    free(filter->name);
    free(filter);
    tpw_pw_global_deinit();
}
