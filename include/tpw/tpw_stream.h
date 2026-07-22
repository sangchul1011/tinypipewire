/* SPDX-License-Identifier: MIT */

#ifndef TPW_STREAM_H
#define TPW_STREAM_H

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

/* Reports that `stream`'s source became unavailable while running. */
typedef void (*tpw_stream_error_cb)(tpw_stream_h stream, int error_code, void* user_data);

/* Creates a stream of `type`. Owns and manages its own PipeWire
 * thread-loop/context/core internally. Fails fast (returns NULL) if
 * PipeWire cannot be reached. */
tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data);

/* Registers (or clears, with NULL) the optional async-error callback. */
int tpw_stream_set_error_cb(tpw_stream_h stream, tpw_stream_error_cb callback);

/* Sets (or clears, with NULL) the PipeWire node this stream should
 * connect to, by name or serial (as shown by `wpctl status` or
 * `pw-cli ls Node`). Must be called before
 * tpw_stream_set_audio_config()/tpw_stream_set_video_config(), which is
 * what actually connects the stream. If never called, the stream
 * auto-connects to PipeWire's default source for its media type. */
int tpw_stream_set_target(tpw_stream_h stream, const char* target);

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

/* Configures audio format before starting an audio stream. */
int tpw_stream_set_audio_config(tpw_stream_h stream, const tpw_audio_config* config);

/* Configures video format before starting a video stream. */
int tpw_stream_set_video_config(tpw_stream_h stream, const tpw_video_config* config);

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
