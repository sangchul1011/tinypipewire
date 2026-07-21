/* SPDX-License-Identifier: MIT */

#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>

#include "tpw_stream_internal.h"

/* Zero-copy handoff: dequeue, hand the mapped pointer/size straight to
 * the caller's callback, then queue back once the callback returns. */
void tpw_stream_on_process(void* data)
{
    struct tpw_stream* stream = data;
    struct pw_buffer* b = pw_stream_dequeue_buffer(stream->pw_stream);
    if (!b)
        return;

    struct spa_buffer* buf = b->buffer;
    struct spa_data* d = &buf->datas[0];

    /* SPA_META_Header carries the source's capture clock (ALSA/V4L2,
     * etc.); not every source attaches it, so -1 signals "unavailable"
     * rather than guessing a timestamp. */
    struct spa_meta_header* h = spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*h));

    if (d->data && d->chunk && d->chunk->size > 0 && stream->data_cb) {
        tpw_stream_buffer sbuf = { .data = d->data, .size = d->chunk->size, .pts = h ? h->pts : -1 };
        stream->data_cb((tpw_stream_h)stream, &sbuf, stream->user_data);
    }

    pw_stream_queue_buffer(stream->pw_stream, b);
}
