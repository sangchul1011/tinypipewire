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

/* One event staged via tpw_filter_port_push_event(), owning a copy of
 * the caller's data (since it's only guaranteed valid for that call,
 * not for the rest of the cycle). `key`, when set, points at a static
 * name from tpw_filter_event.c's property vocabulary table rather
 * than an owned copy, since that table outlives every filter. */
struct tpw_filter_pending_event {
    uint32_t offset;
    tpw_event_kind kind;
    const char* key;  /* static vocabulary entry, NULL unless kind == TPW_EVENT_PROPERTY */
    void* data;       /* owned copy */
    size_t size;
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

    /* Event ports only. incoming_events is this cycle's delivered
     * events (input direction), read via tpw_filter_port_get_event();
     * its data/key pointers alias either the dequeued control
     * sequence's memory or a staged pending_events entry's owned copy
     * — both stay valid for the rest of the cycle, so incoming_events
     * itself never owns the bytes it points to.
     *
     * pending_events serves two different purposes depending on
     * `direction`, since a given port is only ever one direction:
     *   - INPUT: events staged via tpw_filter_port_push_event(),
     *     consumed into incoming_events (in place of a real dequeue)
     *     and freed at the end of the cycle that delivers them —
     *     mirrors pushed_data's "next cycle" staging above.
     *   - OUTPUT: events staged via tpw_filter_port_push_event() during
     *     the current cycle's callback, encoded into a control sequence
     *     and freed once the callback returns.
     * event_output_capacity (output direction only) is the current
     * cycle's dequeued buffer's maximum byte size, set before the
     * callback runs so tpw_filter_port_push_event() can reject a push
     * that wouldn't fit instead of silently truncating later. */
    tpw_event* incoming_events;
    size_t n_incoming_events;
    size_t incoming_events_capacity;

    struct tpw_filter_pending_event* pending_events;
    size_t n_pending_events;
    size_t pending_events_capacity;
    size_t event_output_capacity;
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

/* Decodes a dequeued control sequence buffer (`data`/`size`) into
 * `port`'s incoming_events for the current cycle. Safe to call with
 * data == NULL or a buffer that isn't a valid control sequence — both
 * simply leave incoming_events empty for that cycle. */
void tpw_filter_event_decode(struct tpw_filter_port* port, const void* data, size_t size);

/* Moves `port`'s pending_events (staged via tpw_filter_port_push_event
 * on an input event port) into incoming_events for the current cycle
 * by aliasing their owned memory, in place of a real dequeue. */
void tpw_filter_event_load_pending_as_incoming(struct tpw_filter_port* port);

/* Frees the owned data of every entry in `port`'s pending_events and
 * resets the list to empty. Safe to call when there is nothing staged. */
void tpw_filter_event_clear_pending(struct tpw_filter_port* port);

/* Encodes `port`'s pending_events (staged via tpw_filter_port_push_event
 * on an output event port during the just-finished cycle) into
 * `buf`/`maxsize` as a control sequence, then clears pending_events.
 * Returns the number of bytes written (0 if there was nothing to
 * encode). Assumes tpw_filter_port_push_event() already rejected any
 * push that wouldn't fit in `maxsize`. */
size_t tpw_filter_event_finish_output(struct tpw_filter_port* port, void* buf, size_t maxsize);

/* Frees all owned event memory for `port` (pending_events and the
 * incoming_events array itself); a no-op for a non-event port. Called
 * from tpw_filter_destroy(). */
void tpw_filter_event_free_port(struct tpw_filter_port* port);

#endif /* TPW_FILTER_INTERNAL_H */
