/* SPDX-License-Identifier: MIT */

#ifndef TPW_STREAM_INTERNAL_H
#define TPW_STREAM_INTERNAL_H

#include <stdbool.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include "tpw/tpw_stream.h"
#include "tpw_pw_core_internal.h"

enum tpw_stream_state {
    TPW_STREAM_STATE_CREATED,
    TPW_STREAM_STATE_FORMAT_SET,
    TPW_STREAM_STATE_RUNNING,
    TPW_STREAM_STATE_STOPPED,
};

/* Which way data flows. Decided by which create function was called and
 * fixed for the stream's life; capture is the pre-playback default. */
enum tpw_stream_direction {
    TPW_STREAM_DIRECTION_CAPTURE,
    TPW_STREAM_DIRECTION_PLAYBACK,
};

struct tpw_audio_format_state {
    int sample_rate;
    int channels;
    enum spa_audio_format format;
};

struct tpw_video_format_state {
    int width;
    int height;
    enum spa_video_format format;
};

/* One audio/video capture session, or one audio playback session. */
struct tpw_stream {
    tpw_stream_type type;
    enum tpw_stream_direction direction;
    enum tpw_stream_state state;
    bool format_set;
    union {
        struct tpw_audio_format_state audio;
        struct tpw_video_format_state video;
    } format;

    /* Playback only: frame stride, and the rate-limit state for the
     * overrun log. Both are meaningless on a capture stream. */
    size_t bytes_per_frame;
    uint64_t overrun_last_log_ns;
    uint64_t overrun_suppressed;

    tpw_stream_data_cb data_cb;
    tpw_stream_playback_cb playback_cb;
    tpw_stream_error_cb error_cb;
    void* user_data;

    char* target; /* PW_KEY_TARGET_OBJECT, or NULL for auto-connect */

    struct tpw_pw_core_conn conn;
    struct pw_stream* pw_stream;

    struct spa_hook stream_listener;
};

/* (Re)connects the underlying pw_stream with the given negotiated format
 * params. Destroys any previously connected pw_stream first. Must be
 * called with stream->loop NOT locked by the caller. */
int tpw_stream_internal_connect(struct tpw_stream* stream, const struct spa_pod** params, uint32_t n_params);

/* .process callback registered on the underlying pw_stream; dequeues a
 * buffer, hands it to the caller's data_cb, and queues it back. */
void tpw_stream_on_process(void* data);

/* Bytes one frame occupies: the sample size of `format` times `channels`.
 * Returns 0 for an unsupported format or a non-positive channel count. */
size_t tpw_audio_bytes_per_frame(enum spa_audio_format format, int channels);

/* Playback .process callback; dequeues a buffer, sizes the cycle from
 * pw_buffer.requested, fills it via tpw_stream_playback_fill(), publishes. */
void tpw_stream_on_process_playback(void* data);

/* Runs the playback callback over `data`/`capacity`, then clamps, floors to
 * whole frames and silences the tail. Returns the leading application bytes;
 * the caller publishes the whole capacity, whose tail this has zeroed. */
size_t tpw_stream_playback_fill(struct tpw_stream* stream, void* data, size_t capacity, int64_t pts);

/* Records one overrun at `now_ns` and reports whether it should be logged;
 * false means it was folded into the suppressed count instead. */
bool tpw_stream_playback_note_overrun(struct tpw_stream* stream, uint64_t now_ns);

/* .state_changed callback registered on the underlying pw_stream; detects
 * the source-lost transition and invokes error_cb. Exposed (non-static) so
 * tests can simulate a state transition without a real device. */
void tpw_stream_on_state_changed(void* data, enum pw_stream_state old, enum pw_stream_state state,
                                  const char* error);

#endif /* TPW_STREAM_INTERNAL_H */
