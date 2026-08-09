/* SPDX-License-Identifier: MIT */

#include <string.h>
#include <time.h>

#include "tpw_log_internal.h"
#include "tpw_stream_internal.h"

/* A condition that repeats every cycle would otherwise log every cycle,
 * turning a problem on the real-time thread into a much worse one. */
#define TPW_LOG_INTERVAL_NS 1000000000ull

static uint64_t tpw_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* A zero timestamp means nothing has been logged yet, so the first
 * occurrence of a run always reports. */
static bool tpw_rate_limited(uint64_t* last_log_ns, uint64_t* suppressed, uint64_t now_ns)
{
    if (*last_log_ns != 0 && now_ns - *last_log_ns < TPW_LOG_INTERVAL_NS) {
        (*suppressed)++;
        return false;
    }

    *last_log_ns = now_ns;
    return true;
}

bool tpw_stream_playback_note_overrun(struct tpw_stream* stream, uint64_t now_ns)
{
    return tpw_rate_limited(&stream->overrun_last_log_ns, &stream->overrun_suppressed, now_ns);
}

/* Nanoseconds one full cycle of `capacity` bytes occupies, or 0 when the
 * format is not yet known well enough to say. */
static uint64_t tpw_stream_cycle_budget_ns(const struct tpw_stream* stream, size_t capacity)
{
    if (stream->bytes_per_frame == 0 || stream->format.audio.sample_rate <= 0)
        return 0;

    uint64_t frames = capacity / stream->bytes_per_frame;
    return frames * 1000000000ull / (uint64_t)stream->format.audio.sample_rate;
}

size_t tpw_stream_playback_fill(struct tpw_stream* stream, void* data, size_t capacity, int64_t pts)
{
    tpw_stream_playback_buffer buf = { .data = data, .capacity = capacity, .pts = pts, .size = 0 };

    uint64_t started = tpw_monotonic_ns();
    stream->playback_cb((tpw_stream_h)stream, &buf, stream->user_data);
    uint64_t finished = tpw_monotonic_ns();

    size_t size = buf.size;
    if (size > capacity) {
        tpw_log_warning("stream: playback callback reported %zu bytes for a %zu byte cycle; clamped",
                        size, capacity);
        size = capacity;
    }
    if (stream->bytes_per_frame > 0)
        size -= size % stream->bytes_per_frame;

    if (size < capacity)
        memset((unsigned char*)data + size, 0, capacity - size);

    uint64_t budget = tpw_stream_cycle_budget_ns(stream, capacity);
    if (budget > 0 && finished - started > budget && tpw_stream_playback_note_overrun(stream, finished)) {
        tpw_log_warning("stream: playback callback overran its %llu ns cycle (%llu more suppressed)",
                        (unsigned long long)budget, (unsigned long long)stream->overrun_suppressed);
        stream->overrun_suppressed = 0;
    }

    return size;
}

/* When this cycle's first sample is expected to be heard: the graph's own
 * time, plus the delay to the device and whatever is already queued. */
static int64_t tpw_stream_playback_pts(struct tpw_stream* stream)
{
    struct pw_time t;
    if (pw_stream_get_time_n(stream->pw_stream, &t, sizeof(t)) < 0 || t.rate.denom == 0)
        return -1;

    int64_t ahead = t.delay + (int64_t)t.queued + (int64_t)t.buffered;
    return t.now + ahead * (int64_t)t.rate.num * 1000000000 / (int64_t)t.rate.denom;
}

void tpw_stream_on_process_playback(void* data)
{
    struct tpw_stream* stream = data;
    struct pw_buffer* b = pw_stream_dequeue_buffer(stream->pw_stream);
    if (!b)
        return;

    /* No writable region to fill — an unmappable buffer type, for instance.
     * Say so, or the stream is silent with nothing to explain it. */
    struct spa_data* d = &b->buffer->datas[0];
    if (!d->data || d->maxsize == 0 || !d->chunk || !stream->playback_cb) {
        if (tpw_rate_limited(&stream->unusable_last_log_ns, &stream->unusable_suppressed,
                             tpw_monotonic_ns())) {
            tpw_log_warning("stream: playback buffer offers no writable region (type %u); "
                            "emitting nothing (%llu more suppressed)",
                            d->type, (unsigned long long)stream->unusable_suppressed);
            stream->unusable_suppressed = 0;
        }
        pw_stream_queue_buffer(stream->pw_stream, b);
        return;
    }

    /* What the device asked for, falling back to the whole region when the
     * graph states no per-cycle request. */
    size_t capacity = d->maxsize;
    if (b->requested > 0 && stream->bytes_per_frame > 0) {
        size_t wanted = (size_t)b->requested * stream->bytes_per_frame;
        if (wanted < capacity)
            capacity = wanted;
    }

    tpw_stream_playback_fill(stream, d->data, capacity, tpw_stream_playback_pts(stream));

    /* A full cycle always goes out: the callback's bytes first, silence for
     * whatever it left, so a short fill never starves the device. */
    d->chunk->offset = 0;
    d->chunk->stride = (int32_t)stream->bytes_per_frame;
    d->chunk->size = (uint32_t)capacity;
    b->size = stream->bytes_per_frame > 0 ? capacity / stream->bytes_per_frame : 0;

    pw_stream_queue_buffer(stream->pw_stream, b);
}
