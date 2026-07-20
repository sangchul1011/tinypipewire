/* SPDX-License-Identifier: MIT */

#include <stdlib.h>

#include "tpw_filter_internal.h"

#define TPW_FILTER_STACK_PORTS 8

void tpw_filter_on_process(void* data, struct spa_io_position* position)
{
    struct tpw_filter* filter = data;
    (void)position;

    if (filter->n_ports == 0)
        return;

    tpw_filter_port_buffer stack_buffers[TPW_FILTER_STACK_PORTS];
    struct pw_buffer* stack_dequeued[TPW_FILTER_STACK_PORTS];
    bool heap_alloc = filter->n_ports > TPW_FILTER_STACK_PORTS;

    tpw_filter_port_buffer* buffers = stack_buffers;
    struct pw_buffer** dequeued = stack_dequeued;
    if (heap_alloc) {
        buffers = calloc(filter->n_ports, sizeof(*buffers));
        dequeued = calloc(filter->n_ports, sizeof(*dequeued));
        if (!buffers || !dequeued) {
            free(buffers);
            free(dequeued);
            return;
        }
    }

    for (size_t i = 0; i < filter->n_ports; i++) {
        struct tpw_filter_port* port = filter->ports[i];
        buffers[i].port = (tpw_filter_port_h)port;
        buffers[i].data = NULL;
        buffers[i].size = 0;
        buffers[i].capacity = 0;
        dequeued[i] = NULL;

        if (port->direction == TPW_FILTER_PORT_INPUT) {
            if (port->pushed_data) {
                /* Application-pushed data takes priority over the graph
                 * this cycle; cleared below once the callback returns. */
                buffers[i].data = port->pushed_data;
                buffers[i].size = port->pushed_size;
                continue;
            }
            struct pw_buffer* b = pw_filter_dequeue_buffer(port);
            if (b && b->buffer->datas[0].data) {
                struct spa_data* d = &b->buffer->datas[0];
                buffers[i].data = d->data;
                buffers[i].size = d->chunk ? d->chunk->size : 0;
                dequeued[i] = b;
            }
        } else {
            struct pw_buffer* b = pw_filter_dequeue_buffer(port);
            if (b && b->buffer->datas[0].data) {
                struct spa_data* d = &b->buffer->datas[0];
                buffers[i].data = d->data;
                buffers[i].capacity = d->maxsize;
                dequeued[i] = b;
            }
        }
    }

    if (filter->process_cb)
        filter->process_cb((tpw_filter_h)filter, buffers, filter->n_ports, filter->user_data);

    for (size_t i = 0; i < filter->n_ports; i++) {
        struct tpw_filter_port* port = filter->ports[i];

        if (port->direction == TPW_FILTER_PORT_INPUT && port->pushed_data && !dequeued[i]) {
            free(port->pushed_data);
            port->pushed_data = NULL;
            port->pushed_size = 0;
            port->pushed_capacity = 0;
            continue;
        }

        if (!dequeued[i])
            continue;

        if (port->direction == TPW_FILTER_PORT_OUTPUT) {
            struct spa_data* d = &dequeued[i]->buffer->datas[0];
            if (d->chunk) {
                d->chunk->size = (uint32_t)buffers[i].size;
                d->chunk->offset = 0;
                d->chunk->stride = 0;
            }
        }
        pw_filter_queue_buffer(port, dequeued[i]);
    }

    if (heap_alloc) {
        free(buffers);
        free(dequeued);
    }
}
