/* SPDX-License-Identifier: MIT */

#include <stdio.h>

#include "tpw/tpw_stream.h"

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
}

static void on_fill(tpw_stream_h stream, tpw_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    buf->size = 0;
}

static void print_targets(const char* label, tpw_stream_h stream)
{
    if (!stream) {
        fprintf(stderr, "%s: failed to create stream (is PipeWire running?)\n", label);
        return;
    }

    tpw_target_info targets[32];
    size_t n = tpw_stream_get_target_list(stream, targets, 32);

    printf("%s: %zu target(s)%s\n", label, n, n > 32 ? " (showing first 32)" : "");
    for (size_t i = 0; i < n && i < 32; i++)
        printf("  %s (serial %s) - %s\n", targets[i].name, targets[i].serial,
               targets[i].description[0] ? targets[i].description : "(no description)");

    tpw_stream_destroy(stream);
}

int main(void)
{
    print_targets("Audio/Source", tpw_stream_create(TPW_STREAM_TYPE_AUDIO, on_data, NULL));
    print_targets("Video/Source", tpw_stream_create(TPW_STREAM_TYPE_VIDEO, on_data, NULL));
    print_targets("Audio/Sink", tpw_stream_create_playback(on_fill, NULL));
    return 0;
}
