/* SPDX-License-Identifier: MIT */

#ifndef TPW_FILTER_H
#define TPW_FILTER_H

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

/* Stages `size` bytes from `data` for `port` (an input port) to be
 * delivered on the filter's next processing cycle, without creating any
 * PipeWire-level connection. Lets application code (for example, a
 * capture stream's data callback) feed a filter directly. Only the most
 * recently pushed buffer per port is kept. */
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
