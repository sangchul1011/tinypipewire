/* SPDX-License-Identifier: MIT */

#include <spa/buffer/buffer.h>
#include <spa/pod/builder.h>

#include "tpw_filter_internal.h"
#include "tpw_log_internal.h"
#include "tpw_spa_format_internal.h"

size_t tpw_filter_port_buffer_dmabuf(const tpw_filter_port_buffer* buf, tpw_dmabuf_plane* planes,
                                     size_t max_planes)
{
    if (!buf)
        return 0;
    struct tpw_filter_port* port = (struct tpw_filter_port*)buf->port;
    if (!port || !port->use_dmabuf || !port->current_dmabuf_buf)
        return 0;

    struct spa_buffer* b = port->current_dmabuf_buf;
    size_t count = 0;
    for (uint32_t i = 0; i < b->n_datas; i++) {
        struct spa_data* d = &b->datas[i];
        if (d->type != SPA_DATA_DmaBuf)
            continue;
        if (planes && count < max_planes) {
            planes[count].fd = (int)d->fd;
            planes[count].offset = d->mapoffset;
            planes[count].stride = d->chunk ? (uint32_t)d->chunk->stride : 0;
            planes[count].size = d->chunk ? d->chunk->size : d->maxsize;
        }
        count++;
    }
    return count;
}

void tpw_filter_dmabuf_update_params(struct tpw_filter_port* port)
{
    if (!port || !port->use_dmabuf)
        return;

    uint8_t buffer[512];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params[1];
    /* Hold retains one buffer across cycles, so ask for one extra. */
    params[0] = tpw_spa_build_dmabuf_buffers(&b, port->hold_enabled ? 1u : 0u);

    pw_filter_update_params(port->filter->pw_filter, port, params, 1);
}

void tpw_filter_dmabuf_log_unavailable(struct tpw_filter_port* port)
{
    if (!port || !port->use_dmabuf)
        return;
    tpw_log_warning("filter '%s': a DMABUF video port could not negotiate DMABUF with its "
                    "source; the port will deliver no buffers",
                    port->filter->name ? port->filter->name : "tpw-filter");
}
