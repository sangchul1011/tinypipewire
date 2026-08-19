/* SPDX-License-Identifier: MIT */

#include "tpw_dmabuf_internal.h"

bool tpw_dmabuf_buffer_present(struct spa_buffer* b)
{
    if (!b || b->n_datas == 0)
        return false;
    return b->datas[0].type == SPA_DATA_DmaBuf;
}

size_t tpw_dmabuf_extract_planes(struct spa_buffer* b, tpw_dmabuf_plane* planes, size_t max_planes)
{
    if (!b)
        return 0;

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
