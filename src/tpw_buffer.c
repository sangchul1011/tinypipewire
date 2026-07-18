/* SPDX-License-Identifier: MIT */

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

    if (d->data && d->chunk && d->chunk->size > 0 && stream->data_cb)
        stream->data_cb((tpw_stream_h)stream, d->data, d->chunk->size, stream->user_data);

    pw_stream_queue_buffer(stream->pw_stream, b);
}
