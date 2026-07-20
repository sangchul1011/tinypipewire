/* SPDX-License-Identifier: MIT */

#ifndef TPW_FILTER_H
#define TPW_FILTER_H

#include <stdint.h>
#include <stddef.h>

#include "tpw/tpw_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to one multi-port filter. */
typedef struct tpw_filter* tpw_filter_h;

/* Opaque handle to one input or output port on a filter. */
typedef struct tpw_filter_port* tpw_filter_port_h;

/* Direction of a filter port. */
typedef enum {
    TPW_FILTER_PORT_INPUT,
    TPW_FILTER_PORT_OUTPUT
} tpw_filter_port_direction;

/* One port's buffer for a single processing cycle. `data`/`size`/
 * `capacity` are valid only for the duration of the process callback. */
typedef struct {
    tpw_filter_port_h port;
    void* data;      /* NULL if no buffer was available this cycle */
    size_t size;      /* input: bytes available to read.
                          output: bytes to publish; set by the callback
                          before returning (0 = no output this cycle) */
    size_t capacity;  /* output ports only: max bytes `data` can hold */
} tpw_filter_port_buffer;

/* Invoked once per processing cycle with every port's buffer.
 * `buffers`/`n_buffers` are valid only for the duration of this call. */
typedef void (*tpw_filter_process_cb)(tpw_filter_h filter, tpw_filter_port_buffer* buffers,
                                       size_t n_buffers, void* user_data);

/* Reports that `port` on `filter` became unavailable while running;
 * the filter's other ports are unaffected. */
typedef void (*tpw_filter_error_cb)(tpw_filter_h filter, tpw_filter_port_h port, int error_code,
                                     void* user_data);

/* Which real wire kind an event carries. MIDI/OSC carry real MIDI/OSC
 * wire bytes for interop with other PipeWire MIDI/OSC clients;
 * PROPERTY is the general-purpose kind for anything else a caller
 * writes. UNKNOWN is read-only: it appears only on an event
 * tpw_filter_port_get_event() decoded from a control item this library
 * doesn't recognize (for example, one written by some other PipeWire
 * client using a control kind outside this set); `data`/`size` are
 * that item's raw, undecoded value bytes, and `key` is NULL. Passing
 * UNKNOWN to tpw_filter_port_push_event() is rejected. */
typedef enum {
    TPW_EVENT_MIDI,
    TPW_EVENT_OSC,
    TPW_EVENT_PROPERTY,
    TPW_EVENT_UNKNOWN
} tpw_event_kind;

/* One discrete, time-stamped item exchanged through an event port.
 * `offset` is this event's position within the current cycle, in
 * frames. `key` is meaningful only for TPW_EVENT_PROPERTY (a name from
 * the supported property vocabulary) and MUST be NULL otherwise.
 * `data`/`size` are: MIDI/OSC -> real wire-format bytes; PROPERTY ->
 * the value's raw bytes; UNKNOWN -> the raw undecoded control value's
 * bytes. Pointers are valid only for the duration of the processing
 * callback (when read from tpw_filter_port_get_event()) or the call to
 * tpw_filter_port_push_event() (the library copies what it needs from
 * a pushed event). */
typedef struct {
    uint32_t offset;
    tpw_event_kind kind;
    const char* key;
    const void* data;
    size_t size;
} tpw_event;

/* Creates an empty filter (no ports yet), discoverable by `name` for
 * cross-application routing (name may be NULL/empty). Internally owns
 * and manages its own PipeWire thread-loop/context/core. Fails fast
 * (returns NULL) if PipeWire cannot be reached. */
tpw_filter_h tpw_filter_create(const char* name, tpw_filter_process_cb callback, void* user_data);

/* Registers (or clears, with NULL) the optional per-port async-error callback. */
int tpw_filter_set_error_cb(tpw_filter_h filter, tpw_filter_error_cb callback);

/* Adds one audio port (input or output) to `filter`. Must be called
 * before tpw_filter_start(); adding ports after starting is unsupported.
 * Returns NULL on invalid arguments or an unsupported format. */
tpw_filter_port_h tpw_filter_add_audio_port(tpw_filter_h filter, tpw_filter_port_direction direction,
                                             const tpw_audio_config* config);

/* Adds one video port (input or output) to `filter`. Same timing and
 * failure behavior as tpw_filter_add_audio_port(). */
tpw_filter_port_h tpw_filter_add_video_port(tpw_filter_h filter, tpw_filter_port_direction direction,
                                             const tpw_video_config* config);

/* Adds one signal port (input or output) to `filter` — a continuous
 * channel of raw 32-bit float values, one value per frame of each
 * processing cycle (matching how audio port buffers are sized). No
 * format configuration is needed. Same timing and failure behavior as
 * tpw_filter_add_audio_port(). */
tpw_filter_port_h tpw_filter_add_signal_port(tpw_filter_h filter, tpw_filter_port_direction direction);

/* Adds one event port (input or output) to `filter` — carries zero or
 * more discrete tpw_event items per processing cycle instead of a raw
 * buffer. No format configuration is needed. Same timing and failure
 * behavior as tpw_filter_add_audio_port(). */
tpw_filter_port_h tpw_filter_add_event_port(tpw_filter_h filter, tpw_filter_port_direction direction);

/* Returns the media kind `port` was added with (AUDIO/VIDEO/SIGNAL/
 * EVENT). Valid for any port handle obtained from any add_*_port()
 * call. */
tpw_stream_type tpw_filter_port_get_type(tpw_filter_port_h port);

/* Returns the number of events available on `port` (an input event
 * port) for the current processing cycle; 0 if none. Valid only during
 * the processing callback. */
size_t tpw_filter_port_event_count(tpw_filter_port_h port);

/* Reads the event at `index` (0-based, cycle-delivery order) on `port`
 * (an input event port) into `*out`. Valid only during the processing
 * callback; `out`'s data/key pointers are valid only for that same
 * call. Returns 0 on success, a tpw_stream_error code otherwise
 * (invalid index, wrong port kind/direction, or an event of an
 * unrecognized kind). */
int tpw_filter_port_get_event(tpw_filter_port_h port, size_t index, tpw_event* out);

/* Adds one event to `port`'s event queue; the library copies `event`'s
 * data. Behavior depends on `port`'s direction:
 *   - Output port: appends to the current processing cycle's outgoing
 *     events, published when the cycle ends. Must be called only from
 *     within the processing callback (its capacity is bounded by that
 *     cycle's negotiated buffer).
 *   - Input port: stages the event for delivery on the filter's next
 *     processing cycle, without creating any PipeWire-level connection
 *     — the event-port equivalent of tpw_filter_push_port_data(), for
 *     application code (for example, a test or another in-process
 *     source) to feed an event port directly. Callable anytime, not
 *     just from within the processing callback.
 * Returns 0 on success, a tpw_stream_error code otherwise (wrong port
 * kind, an invalid/unrecognized PROPERTY key, or — output ports only —
 * no room left in the current cycle's buffer). */
int tpw_filter_port_push_event(tpw_filter_port_h port, const tpw_event* event);

/* Stages `size` bytes from `data` for `port` (an input port) to be
 * delivered on the filter's next processing cycle, without creating any
 * PipeWire-level connection. Lets application code (for example, a
 * capture stream's data callback) feed a filter directly. Only the most
 * recently pushed buffer per port is kept. Not valid for event ports —
 * use tpw_filter_port_push_event() instead. */
int tpw_filter_push_port_data(tpw_filter_h filter, tpw_filter_port_h port, const void* data, size_t size);

/* Starts processing. Fails if the filter has zero ports. Safe to call
 * again after stop(). */
int tpw_filter_start(tpw_filter_h filter);

/* Stops processing; the filter may be restarted via tpw_filter_start(). */
int tpw_filter_stop(tpw_filter_h filter);

/* Releases all resources owned by `filter`, including its ports and its
 * internal thread-loop/context, whether or not it was running. `filter`
 * and any of its port handles are invalid after this returns. */
void tpw_filter_destroy(tpw_filter_h filter);

#ifdef __cplusplus
}
#endif

#endif /* TPW_FILTER_H */
