/* SPDX-License-Identifier: MIT */

/**
 * @file tpw_stream.h
 * @brief Single audio/video capture or audio playback stream, wired
 *        through PipeWire with no session-manager configuration required.
 */

#ifndef TPW_STREAM_H
#define TPW_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle to one audio- or video-capture stream. */
typedef struct tpw_stream* tpw_stream_h;

/**
 * @brief Classifies a stream, or a tpw_filter_* port, by the kind of media
 *        it carries.
 *
 * SIGNAL and EVENT are filter-port-only: tpw_stream_create() only accepts
 * AUDIO/VIDEO and rejects the other two.
 */
typedef enum {
    TPW_STREAM_TYPE_AUDIO  = 0, /**< Raw audio samples. */
    TPW_STREAM_TYPE_VIDEO  = 1, /**< Raw video frames. */
    TPW_STREAM_TYPE_SIGNAL = 2, /**< Filter ports only, see tpw_filter.h. */
    TPW_STREAM_TYPE_EVENT  = 3  /**< Filter ports only, see tpw_filter.h. */
} tpw_stream_type;

/** @brief Library error codes. Negative values only; 0 is success. */
typedef enum {
    TPW_STREAM_OK                     = 0,  /**< Success. */
    TPW_STREAM_ERR_INVALID_ARG        = -1, /**< A NULL/out-of-range argument, or a call invalid for the object's current state or routing mode. */
    TPW_STREAM_ERR_CONNECT_FAILED     = -2, /**< Connecting to PipeWire, or negotiating a link/format, failed or timed out. */
    TPW_STREAM_ERR_INVALID_FORMAT     = -3, /**< An unrecognized pixel/sample format string, or an out-of-range dimension/rate. */
    TPW_STREAM_ERR_NOT_CONFIGURED     = -4, /**< Called before a required prior step, e.g. start() before a format was set, or link() before start(). */
    TPW_STREAM_ERR_SOURCE_UNAVAILABLE = -5  /**< The connected source disappeared, or could not provide the requested memory type (see tpw_stream_error_cb). */
} tpw_stream_error;

/**
 * @brief One delivered buffer of captured audio samples or a video frame.
 *
 * A struct (rather than loose parameters) so future fields can be added
 * without changing tpw_stream_data_cb's signature. `data`/`size`/`pts` are
 * valid only for the duration of the data callback.
 */
typedef struct {
    void* data;  /**< Captured bytes, or NULL when the stream negotiated DMABUF (see tpw_stream_get_dmabuf_planes()). */
    size_t size; /**< Bytes available at `data`; 0 when `data` is NULL. */
    int64_t pts; /**< Capture timestamp in nanoseconds (the driver clock used by the underlying SPA node, e.g. ALSA or V4L2), or -1 if the buffer carried no timestamp metadata. */
} tpw_stream_buffer;

/**
 * @brief Delivers one buffer of captured audio samples or a video frame.
 * @param stream    The stream that produced `buf`.
 * @param buf       This cycle's buffer; valid only for the duration of this call.
 * @param user_data The pointer passed to tpw_stream_create().
 */
typedef void (*tpw_stream_data_cb)(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data);

/**
 * @brief One cycle's writable region for a playback stream.
 *
 * `data`, `available` and `pts` are set by the library; the callback sets
 * `size`.
 */
typedef struct {
    void* data;       /**< Writable region for this cycle, never NULL. */
    size_t available; /**< Bytes the callback may write this cycle: what the device asked for, or the region's full size when the graph states no request. Not the region's capacity — it is usually smaller. */
    int64_t pts;      /**< When this cycle's first sample is expected to be heard, in monotonic nanoseconds, or -1 if the graph cannot state one. The mirror of the capture buffer's pts, not the same thing: capture reports when samples were taken, playback when they will be played. */
    size_t size;      /**< Set by the callback: bytes actually written. Clamped to `available` and floored to whole frames; the remainder up to `available` is emitted as silence, so 0 emits a silent cycle rather than stopping. */
} tpw_stream_playback_buffer;

/**
 * @brief Asks the application to fill one cycle of audio.
 *
 * Runs on the real-time data thread: it MUST NOT block, allocate, or
 * perform I/O. A cycle whose callback runs past its budget is emitted as
 * silence and recorded in the library log, not reported through
 * tpw_stream_error_cb.
 *
 * @param stream    The playback stream asking for data.
 * @param buf       This cycle's writable region; valid only for the duration of this call.
 * @param user_data The pointer passed to tpw_stream_create_playback().
 */
typedef void (*tpw_stream_playback_cb)(tpw_stream_h stream, tpw_stream_playback_buffer* buf,
                                        void* user_data);

/**
 * @brief Reports that `stream`'s source became unavailable while running.
 * @param stream     The affected stream.
 * @param error_code A tpw_stream_error, currently always TPW_STREAM_ERR_SOURCE_UNAVAILABLE.
 * @param user_data  The pointer passed to tpw_stream_create()/tpw_stream_create_playback().
 */
typedef void (*tpw_stream_error_cb)(tpw_stream_h stream, int error_code, void* user_data);

/**
 * @brief Creates a stream of `type`.
 *
 * Owns and manages its own PipeWire thread-loop/context/core internally.
 *
 * @param type      TPW_STREAM_TYPE_AUDIO or TPW_STREAM_TYPE_VIDEO; SIGNAL/EVENT are rejected.
 * @param callback  Invoked with each delivered buffer once the stream is started.
 * @param user_data Passed unchanged to `callback`.
 * @return A new stream handle, or NULL if PipeWire cannot be reached or `type` is rejected.
 */
tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data);

/**
 * @brief Creates an audio playback stream, emitting to an output device
 *        instead of capturing from an input one.
 *
 * Audio only: with no media type parameter a video playback stream cannot
 * be expressed. Owns its own PipeWire thread-loop/context/core and fails
 * fast exactly as tpw_stream_create() does. The result is used with the
 * same set_error_cb/set_target/set_audio_config/start/stop/destroy calls;
 * tpw_stream_set_video_config() is rejected on it.
 *
 * @param callback  Invoked once per cycle to fill the next block of audio.
 * @param user_data Passed unchanged to `callback`.
 * @return A new playback stream handle, or NULL if PipeWire cannot be reached.
 */
tpw_stream_h tpw_stream_create_playback(tpw_stream_playback_cb callback, void* user_data);

/**
 * @brief Registers (or clears, with NULL) the optional async-error callback.
 * @param stream   The stream to configure.
 * @param callback The callback to invoke on source loss, or NULL to clear it.
 * @return TPW_STREAM_OK, or TPW_STREAM_ERR_INVALID_ARG for a NULL `stream`.
 */
int tpw_stream_set_error_cb(tpw_stream_h stream, tpw_stream_error_cb callback);

/**
 * @brief Sets (or clears, with NULL) the PipeWire node this stream should
 *        connect to, by name or serial (as shown by `wpctl status` or
 *        `pw-cli ls Node`).
 *
 * Must be called before tpw_stream_set_audio_config()/
 * tpw_stream_set_video_config(), which is what actually connects the
 * stream. If never called, the stream auto-connects to PipeWire's default
 * source for its media type.
 *
 * This is a hint to the session manager, which does the wiring, so it is
 * meaningful only while autoconnect is on. Mutually exclusive with
 * tpw_stream_set_autoconnect(false): whichever of the two is called second
 * returns TPW_STREAM_ERR_INVALID_ARG.
 *
 * @param stream The stream to target, before its format is set.
 * @param target A node name or object.serial, or NULL to clear a previously set target.
 * @return TPW_STREAM_OK, or TPW_STREAM_ERR_INVALID_ARG (NULL stream, format already set, or autoconnect already turned off).
 */
int tpw_stream_set_target(tpw_stream_h stream, const char* target);

/**
 * @brief Turns automatic connection off, so the application wires the
 *        stream itself with tpw_stream_link().
 *
 * On by default, which is what every existing caller already gets. Must
 * be called before the format is set; the routing mode is fixed once the
 * stream connects. Mutually exclusive with tpw_stream_set_target():
 * whichever of the two is called second returns TPW_STREAM_ERR_INVALID_ARG.
 *
 * @param stream The stream to configure, before its format is set.
 * @param enable false to opt out of autoconnect (manual wiring via tpw_stream_link()); true restores the default.
 * @return TPW_STREAM_OK, or TPW_STREAM_ERR_INVALID_ARG (NULL stream, format already set, or a target already set).
 */
int tpw_stream_set_autoconnect(tpw_stream_h stream, bool enable);

/**
 * @brief Connects every channel of `stream` to `target` — a node name or
 *        an object.serial — with PipeWire core links, needing no session
 *        manager.
 *
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
 * @param stream The running stream to wire, with autoconnect off.
 * @param target A node name or an object.serial.
 * @return TPW_STREAM_OK, or a tpw_stream_error: NOT_CONFIGURED before start, INVALID_ARG for a bad mode/target/channel count or an already-linked stream, CONNECT_FAILED when negotiation fails or times out.
 */
int tpw_stream_link(tpw_stream_h stream, const char* target);

/**
 * @brief Releases every link created on `stream`.
 *
 * tpw_stream_destroy() does this itself, so an explicit call is only
 * needed to re-target a stream. tpw_stream_stop() deliberately does NOT
 * release: a stopped stream resumes on the same device when started
 * again.
 *
 * @param stream The stream to unlink.
 * @return TPW_STREAM_OK, or TPW_STREAM_ERR_INVALID_ARG when the stream has no links.
 */
int tpw_stream_unlink(tpw_stream_h stream);

/**
 * @brief Audio capture configuration passed to
 *        tpw_stream_set_audio_config() and tpw_filter_add_audio_port().
 */
typedef struct {
    int sample_rate;    /**< Hz, e.g. 48000. */
    int channels;       /**< Channel count, e.g. 2. */
    const char* format; /**< "U8", "S16", "S24", "S24_32", "S32", or "F32"; NULL defaults to "S16". */
} tpw_audio_config;

/** @brief Video capture configuration passed to tpw_stream_set_video_config(). */
typedef struct {
    int width;                /**< Frame width in pixels. */
    int height;               /**< Frame height in pixels. */
    const char* pixel_format; /**< "RGB", "YUYV", "NV12", "NV21", "I420", or "MJPG". MJPG is JPEG-compressed: each delivered frame's size varies, and DMABUF delivery is not available for it. */
    int fps;                  /**< Frames per second; 0 negotiates automatically. */
} tpw_video_config;

/**
 * @brief Selects a video port/stream's buffer memory.
 *
 * AUTO is the default (graph-selected, normally CPU-mapped). DMABUF
 * negotiates file descriptors instead of a CPU buffer.
 */
typedef enum {
    TPW_PORT_MEMORY_AUTO   = 0, /**< Graph-selected, normally CPU-mapped. */
    TPW_PORT_MEMORY_DMABUF = 1  /**< Negotiate DMABUF file descriptors. */
} tpw_port_memory;

/**
 * @brief One plane of a DMABUF-delivered frame.
 *
 * `fd` is borrowed (import-only, not owned): valid only for the callback
 * that received it, do not close it.
 */
typedef struct {
    int      fd;     /**< Borrowed DMABUF file descriptor; do not close. */
    uint32_t offset; /**< Byte offset to the plane within the dmabuf. */
    uint32_t stride; /**< Row stride in bytes. */
    uint32_t size;   /**< Valid bytes of this plane. */
} tpw_dmabuf_plane;

/**
 * @brief Configures audio format before starting an audio stream.
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested sample rate, channel count, and sample format.
 * @return TPW_STREAM_OK, TPW_STREAM_ERR_INVALID_ARG (NULL stream/config, or not an audio stream), TPW_STREAM_ERR_INVALID_FORMAT (unrecognized format or out-of-range field), or TPW_STREAM_ERR_CONNECT_FAILED.
 */
int tpw_stream_set_audio_config(tpw_stream_h stream, const tpw_audio_config* config);

/**
 * @brief Configures video format before starting a video stream.
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested width, height, pixel format, and frame rate.
 * @return TPW_STREAM_OK, TPW_STREAM_ERR_INVALID_ARG (NULL stream/config, or not a video-capture stream), TPW_STREAM_ERR_INVALID_FORMAT (unrecognized pixel format or out-of-range dimension), or TPW_STREAM_ERR_CONNECT_FAILED.
 */
int tpw_stream_set_video_config(tpw_stream_h stream, const tpw_video_config* config);

/**
 * @brief Per-stream DMABUF options.
 *
 * A NULL or zeroed struct means AUTO, i.e. the behavior of
 * tpw_stream_set_video_config().
 */
typedef struct {
    tpw_port_memory memory; /**< AUTO (default) or DMABUF. */
    uint32_t reserved[2];   /**< Must be zero; reserved for future options. */
} tpw_stream_dmabuf_opts;

/**
 * @brief Configures video format before starting a video capture stream,
 *        with options.
 *
 * Equivalent to tpw_stream_set_video_config() when `opts` is NULL. With
 * opts->memory == TPW_PORT_MEMORY_DMABUF, the stream negotiates DMABUF
 * frames: tpw_stream_buffer.data is NULL for each delivered frame and
 * planes are read via tpw_stream_get_dmabuf_planes(). Video capture only —
 * the same type/direction rejection as tpw_stream_set_video_config()
 * applies, so calling this on an audio or playback stream returns
 * TPW_STREAM_ERR_INVALID_ARG.
 *
 * If the source cannot provide DMABUF, negotiation fails asynchronously:
 * the condition is logged and tpw_stream_error_cb (if set) is invoked
 * with TPW_STREAM_ERR_SOURCE_UNAVAILABLE, the same path used when a
 * source is lost after connecting. The stream delivers no frames until
 * reconfigured without DMABUF.
 *
 * config->pixel_format == "MJPG" combined with DMABUF is rejected right
 * away with TPW_STREAM_ERR_INVALID_ARG instead: that combination never
 * works, so it fails immediately rather than through the async path above.
 *
 * @param stream The stream to configure; must not have started yet.
 * @param config The requested width, height, pixel format, and frame rate.
 * @param opts   DMABUF options, or NULL for AUTO.
 * @return TPW_STREAM_OK, TPW_STREAM_ERR_INVALID_ARG (NULL stream/config, not a video-capture stream, or MJPG requested with DMABUF), TPW_STREAM_ERR_INVALID_FORMAT, or TPW_STREAM_ERR_CONNECT_FAILED.
 */
int tpw_stream_set_video_config_ex(tpw_stream_h stream, const tpw_video_config* config,
                                    const tpw_stream_dmabuf_opts* opts);

/**
 * @brief Fills up to `planes_len` entries of `planes` with the current
 *        cycle's DMABUF frame layout.
 *
 * Valid only during the data callback (tpw_stream_data_cb). `planes`
 * comes before `planes_len`, its own capacity, rather than after,
 * keeping the array and its capacity adjacent even though `planes` is
 * the out-parameter here.
 *
 * @param[in]  stream     The stream whose current-cycle buffer to read.
 * @param[out] planes     Filled with up to `planes_len` planes, most-significant plane first.
 * @param[in]  planes_len Capacity of `planes`.
 * @return The plane count actually available, which may exceed `planes_len` if it was too small; 0 for a non-DMABUF stream or a cycle with no buffer, and `planes` is left unwritten.
 */
size_t tpw_stream_get_dmabuf_planes(tpw_stream_h stream, tpw_dmabuf_plane* planes, size_t planes_len);

/**
 * @brief Starts data delivery. Requires a format to already be set.
 * @param stream The stream to start.
 * @return TPW_STREAM_OK, TPW_STREAM_ERR_INVALID_ARG (NULL stream), TPW_STREAM_ERR_NOT_CONFIGURED (no format set), or TPW_STREAM_ERR_CONNECT_FAILED.
 */
int tpw_stream_start(tpw_stream_h stream);

/**
 * @brief Stops data delivery; the stream may be started again later.
 *
 * With `drain` true, blocks until every buffer already queued has
 * actually been played (a playback stream) or delivered to the data
 * callback (a capture stream), so nothing already queued is lost. If
 * that does not complete within a few seconds (a lost device, for
 * instance), a warning is logged and the stream stops anyway.
 *
 * @param stream The stream to stop.
 * @param drain  true to wait for what is already queued to finish first.
 * @return TPW_STREAM_OK, or TPW_STREAM_ERR_INVALID_ARG for a NULL `stream`.
 */
int tpw_stream_stop(tpw_stream_h stream, bool drain);

/**
 * @brief Releases all resources owned by `stream`.
 *
 * Invalid for further use after this call, running or not.
 *
 * @param stream The stream to destroy; NULL is a no-op.
 */
void tpw_stream_destroy(tpw_stream_h stream);

#ifdef __cplusplus
}
#endif

#endif /* TPW_STREAM_H */
