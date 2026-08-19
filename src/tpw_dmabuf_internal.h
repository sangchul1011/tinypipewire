/* SPDX-License-Identifier: MIT */

#ifndef TPW_DMABUF_INTERNAL_H
#define TPW_DMABUF_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <spa/buffer/buffer.h>

#include "tpw/tpw_stream.h"

/* Whether `b`'s first plane is DMABUF-backed. This is the "buffer
 * present" test for a DMABUF port or stream, whose data pointer is
 * always NULL. Shared by tpw_filter and tpw_stream. */
bool tpw_dmabuf_buffer_present(struct spa_buffer* b);

/* Fills up to `max_planes` entries of `planes` with every DMABUF-typed
 * plane of `b` and returns the plane count. Shared by tpw_filter and
 * tpw_stream. */
size_t tpw_dmabuf_extract_planes(struct spa_buffer* b, tpw_dmabuf_plane* planes, size_t max_planes);

#endif /* TPW_DMABUF_INTERNAL_H */
