/* SPDX-License-Identifier: MIT */

#ifndef TPW_STREAM_H
#define TPW_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to one audio- or video-capture stream. */
typedef struct tpw_stream* tpw_stream_h;

/* Classifies a stream, or a tpw_filter_* port, by the kind of media it
 * carries. SIGNAL and EVENT are filter-port-only: tpw_stream_create()
 * only accepts AUDIO/VIDEO and rejects the other two. */
typedef enum {
    TPW_STREAM_TYPE_AUDIO,
    TPW_STREAM_TYPE_VIDEO,
    TPW_STREAM_TYPE_SIGNAL, /* filter ports only, see tpw_filter.h */
    TPW_STREAM_TYPE_EVENT   /* filter ports only, see tpw_filter.h */
} tpw_stream_type;

/* Library error codes. Negative values only; 0 is success. */
typedef enum {
    TPW_STREAM_OK                     = 0,
    TPW_STREAM_ERR_INVALID_ARG        = -1,
    TPW_STREAM_ERR_CONNECT_FAILED     = -2,
    TPW_STREAM_ERR_INVALID_FORMAT     = -3,
    TPW_STREAM_ERR_NOT_CONFIGURED     = -4,
    TPW_STREAM_ERR_SOURCE_UNAVAILABLE = -5
} tpw_stream_error;

/* One delivered buffer of captured audio samples or a video frame.
 * `data`/`size`/`pts` are valid only for the duration of the data
 * callback. A struct (rather than loose parameters) so future fields
 * can be added without changing tpw_stream_data_cb's signature. */
typedef struct {
    void* data;
    size_t size;
    int64_t pts; /* capture timestamp in nanoseconds (the driver clock
                    used by the underlying SPA node, e.g. ALSA or
                    V4L2), or -1 if the buffer carried no timestamp
                    metadata. */
} tpw_stream_buffer;

/* Delivers one buffer of captured audio samples or a video frame.
 * `buf` is valid only for the duration of this call. */
typedef void (*tpw_stream_data_cb)(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data);

/* One cycle's writable region for a playback stream. `data`, `available`
 * and `pts` are set by the library; the callback sets `size`. */
typedef struct {
    void* data;        /* writable region for this cycle, never NULL */
    size_t available;  /* bytes the callback may write this cycle: what the
                          device asked for, or the region's full size when
                          the graph states no request. Not the region's
                          capacity — it is usually smaller. */
    int64_t pts;       /* when this cycle's first sample is expected to be
                          heard, in monotonic nanoseconds, or -1 if the graph
                          cannot state one. The mirror of the capture
                          buffer's pts, not the same thing: capture reports
                          when samples were taken, playback when they will be
                          played. */
    size_t size;       /* set by the callback: bytes actually written.
                          Clamped to `available` and floored to whole frames;
                          the remainder up to `available` is emitted as
                          silence, so 0 emits a silent cycle rather than
                          stopping. */
} tpw_stream_playback_buffer;

/* Asks the application to fill one cycle of audio. Runs on the real-time
 * data thread: it MUST NOT block, allocate, or perform I/O. `buf` is valid
 * only for the duration of this call. A cycle whose callback runs past its
 * budget is emitted as silence and recorded in the library log, not
 * reported through tpw_stream_error_cb. */
typedef void (*tpw_stream_playback_cb)(tpw_stream_h stream, tpw_stream_playback_buffer* buf,
                                        void* user_data);

/* Reports that `stream`'s source became unavailable while running. */
typedef void (*tpw_stream_error_cb)(tpw_stream_h stream, int error_code, void* user_data);

/* Creates a stream of `type`. Owns and manages its own PipeWire
 * thread-loop/context/core internally. Fails fast (returns NULL) if
 * PipeWire cannot be reached. */
tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data);

/* Creates an audio playback stream, emitting to an output device instead
 * of capturing from an input one. Audio only: with no media type parameter
 * a video playback stream cannot be expressed. Owns its own PipeWire
 * thread-loop/context/core and fails fast (returns NULL) if PipeWire cannot
 * be reached, exactly as tpw_stream_create() does. The result is used with
 * the same set_error_cb/set_target/set_audio_config/start/stop/destroy
 * calls; tpw_stream_set_video_config() is rejected on it. */
tpw_stream_h tpw_stream_create_playback(tpw_stream_playback_cb callback, void* user_data);

/* Registers (or clears, with NULL) the optional async-error callback. */
int tpw_stream_set_error_cb(tpw_stream_h stream, tpw_stream_error_cb callback);

/* Sets (or clears, with NULL) the PipeWire node this stream should
 * connect to, by name or serial (as shown by `wpctl status` or
 * `pw-cli ls Node`). Must be called before
 * tpw_stream_set_audio_config()/tpw_stream_set_video_config(), which is
 * what actually connects the stream. If never called, the stream
 * auto-connects to PipeWire's default source for its media type.
 *
 * This is a hint to the session manager, which does the wiring, so it is
 * meaningful only while autoconnect is on. Mutually exclusive with
 * tpw_stream_set_autoconnect(false): whichever of the two is called second
 * returns TPW_STREAM_ERR_INVALID_ARG. */
int tpw_stream_set_target(tpw_stream_h stream, const char* target);

/* Turns automatic connection off, so the application wires the stream
 * itself with tpw_stream_link(). On by default, which is what every
 * existing caller already gets. Must be called before the format is set;
 * the routing mode is fixed once the stream connects. Mutually exclusive
 * with tpw_stream_set_target(): whichever of the two is called second
 * returns TPW_STREAM_ERR_INVALID_ARG. */
int tpw_stream_set_autoconnect(tpw_stream_h stream, bool enable);

/* Connects every channel of `stream` to `target` — a node name or an
 * object.serial — with PipeWire core links, needing no session manager.
 * Channels are paired by position, so a stereo stream reaches a stereo
 * device's two ports without the caller naming any of them.
 *
 * Requires autoconnect to be off, and must be called after
 * tpw_stream_start(): the target and the stream's own ports are both
 * resolved in the running graph, and the ports appear shortly after the
 * stream starts. Blocks until every link negotiates; on any channel
 * failing, none is left behind.
 *
 * A device offering more channels than the stream has is not an error:
 * the surplus stay unconnected and the condition is logged, since a mono
 * stream reaching one side of a stereo device looks like a fault. A
 * device offering fewer is rejected outright.
 *
 * Returns 0, or a tpw_stream_error: NOT_CONFIGURED before start,
 * INVALID_ARG for a bad mode/target/channel count or an already-linked
 * stream, CONNECT_FAILED when negotiation fails or times out. */
int tpw_stream_link(tpw_stream_h stream, const char* target);

/* Releases every link created on `stream`. tpw_stream_destroy() does this
 * itself, so an explicit call is only needed to re-target a stream.
 * tpw_stream_stop() deliberately does NOT release: a stopped stream
 * resumes on the same device when started again. Returns 0, or
 * TPW_STREAM_ERR_INVALID_ARG when the stream has no links. */
int tpw_stream_unlink(tpw_stream_h stream);

/* Audio capture configuration passed to tpw_stream_set_audio_config()
 * and tpw_filter_add_audio_port(). */
typedef struct {
    int sample_rate;          /* Hz, e.g. 48000 */
    int channels;             /* channel count, e.g. 2 */
    const char* format;       /* "S16", "S24", "S32", or "F32"; NULL defaults to "S16" */
} tpw_audio_config;

/* Video capture configuration passed to tpw_stream_set_video_config(). */
typedef struct {
    int width;
    int height;
    const char* pixel_format; /* "RGB", "YUYV", "NV12", or "I420" */
    int fps;                  /* frames per second; 0 negotiates automatically */
} tpw_video_config;

/* Selects a video port/stream's buffer memory. AUTO is the default
 * (graph-selected, normally CPU-mapped). DMABUF negotiates file
 * descriptors instead of a CPU buffer. */
typedef enum {
    TPW_PORT_MEMORY_AUTO,
    TPW_PORT_MEMORY_DMABUF
} tpw_port_memory;

/* One plane of a DMABUF-delivered frame. `fd` is borrowed (import-only,
 * not owned): valid only for the callback that received it, do not close
 * it. */
typedef struct {
    int      fd;
    uint32_t offset;   /* byte offset to the plane within the dmabuf */
    uint32_t stride;   /* row stride in bytes */
    uint32_t size;     /* valid bytes of this plane */
} tpw_dmabuf_plane;

/* Configures audio format before starting an audio stream. */
int tpw_stream_set_audio_config(tpw_stream_h stream, const tpw_audio_config* config);

/* Configures video format before starting a video stream. */
int tpw_stream_set_video_config(tpw_stream_h stream, const tpw_video_config* config);

/* Per-stream DMABUF options. A NULL or zeroed struct means AUTO, i.e. the
 * behavior of tpw_stream_set_video_config(). */
typedef struct {
    tpw_port_memory memory;
} tpw_stream_dmabuf_opts;

/* Configures video format before starting a video capture stream, with
 * options. Equivalent to tpw_stream_set_video_config() when `opts` is NULL.
 * With opts->memory == TPW_PORT_MEMORY_DMABUF, the stream negotiates DMABUF
 * frames: tpw_stream_buffer.data is NULL for each delivered frame and
 * planes are read via tpw_stream_buffer_dmabuf(). Video capture only - the
 * same type/direction rejection as tpw_stream_set_video_config() applies,
 * so calling this on an audio or playback stream returns
 * TPW_STREAM_ERR_INVALID_ARG.
 *
 * If the source cannot provide DMABUF, negotiation fails asynchronously:
 * the condition is logged and tpw_stream_error_cb (if set) is invoked with
 * TPW_STREAM_ERR_SOURCE_UNAVAILABLE, the same path used when a source is
 * lost after connecting. The stream delivers no frames until reconfigured
 * without DMABUF. */
int tpw_stream_set_video_config_ex(tpw_stream_h stream, const tpw_video_config* config,
                                    const tpw_stream_dmabuf_opts* opts);

/* Fills up to `max_planes` entries of `planes` with the current cycle's
 * DMABUF frame layout and returns the plane count. Returns 0 for a
 * non-DMABUF stream or a cycle with no buffer, without writing `planes`.
 * Valid only during the data callback (tpw_stream_data_cb). */
size_t tpw_stream_buffer_dmabuf(tpw_stream_h stream, tpw_dmabuf_plane* planes, size_t max_planes);

/* Starts data delivery. Requires a format to already be set. */
int tpw_stream_start(tpw_stream_h stream);

/* Stops data delivery; the stream may be started again later. */
int tpw_stream_stop(tpw_stream_h stream);

/* Releases all resources owned by `stream`. Invalid for further use
 * after this call, running or not. */
void tpw_stream_destroy(tpw_stream_h stream);

#ifdef __cplusplus
}
#endif

#endif /* TPW_STREAM_H */
