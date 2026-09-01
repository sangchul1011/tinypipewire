/* SPDX-License-Identifier: MIT */

/* Wires a camera and a microphone straight into a filter with
 * tpw_filter_port_link(), needing no pw-link call. The camera goes to a
 * DMABUF video port and the microphone to a signal port. */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tpw/tpw_filter.h>

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

static const char* link_result_text(int res)
{
    switch (res) {
    case TPW_STREAM_OK:
        return "linked";
    case TPW_STREAM_ERR_INVALID_ARG:
        return "no such target, or the port cannot be linked (output port?)";
    case TPW_STREAM_ERR_INVALID_FORMAT:
        return "target found, but the formats do not negotiate";
    case TPW_STREAM_ERR_NOT_CONFIGURED:
        return "the filter is not started — link after tpw_filter_start()";
    case TPW_STREAM_ERR_CONNECT_FAILED:
        return "the link could not be created or timed out";
    default:
        return "unknown error";
    }
}

static void on_process(tpw_filter_h filter, tpw_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    unsigned* cycles = user_data;

    /* Report only occasionally; the graph runs far faster than a log. */
    if ((*cycles)++ % 100 != 0)
        return;

    for (size_t i = 0; i < n_buffers; i++) {
        tpw_dmabuf_plane plane;
        if (tpw_filter_port_get_dmabuf_planes(&buffers[i], &plane, 1) > 0)
            printf("  port %zu: dmabuf fd=%d stride=%u fresh=%d seq=%llu\n", i, plane.fd, plane.stride,
                   (int)buffers[i].fresh, (unsigned long long)buffers[i].seq);
        else if (buffers[i].data)
            printf("  port %zu: %zu bytes fresh=%d\n", i, buffers[i].size, (int)buffers[i].fresh);
    }
}

static void on_error(tpw_filter_h filter, tpw_filter_port_h port, int error_code, void* user_data)
{
    (void)filter;
    (void)port;
    (void)user_data;
    if (error_code == TPW_STREAM_ERR_SOURCE_UNAVAILABLE)
        printf("a linked source went away; its port is now unlinked\n");
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <video-node> [audio-node]\n"
                        "  node names come from `wpctl status` or `pw-cli ls Node`;\n"
                        "  an object.serial or \"node:port\" works too.\n", argv[0]);
        return 1;
    }
    const char* video_target = argv[1];
    const char* audio_target = (argc > 2) ? argv[2] : NULL;

    signal(SIGINT, on_sigint);

    unsigned cycles = 0;
    tpw_filter_h filter = tpw_filter_create("tpw-port-link", on_process, &cycles);
    if (!filter) {
        fprintf(stderr, "failed to create the filter (is PipeWire running?)\n");
        return 1;
    }
    tpw_filter_set_error_cb(filter, on_error);

    tpw_video_config video_cfg = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };
    tpw_filter_port_opts dmabuf_opts = { .memory = TPW_PORT_MEMORY_DMABUF };
    tpw_filter_port_h video_in =
        tpw_filter_add_video_port_ex(filter, TPW_FILTER_PORT_INPUT, &video_cfg, &dmabuf_opts);
    tpw_filter_port_h audio_in =
        audio_target ? tpw_filter_add_signal_port(filter, TPW_FILTER_PORT_INPUT) : NULL;
    if (!video_in || (audio_target && !audio_in)) {
        fprintf(stderr, "failed to add the filter's ports\n");
        tpw_filter_destroy(filter);
        return 1;
    }

    /* Hold re-presents the camera's last frame on cycles it produced
     * nothing, so a faster audio port does not starve the bundle. */
    tpw_filter_port_set_hold(video_in, true);

    if (tpw_filter_start(filter) != TPW_STREAM_OK) {
        fprintf(stderr, "failed to start the filter\n");
        tpw_filter_destroy(filter);
        return 1;
    }

    /* Linking comes after start: the target is looked up in the running
     * graph, so the filter's own node has to exist there first. */
    int res = tpw_filter_port_link(video_in, video_target);
    printf("link video port -> '%s': %s\n", video_target, link_result_text(res));

    if (audio_in) {
        res = tpw_filter_port_link(audio_in, audio_target);
        printf("link signal port -> '%s': %s\n", audio_target, link_result_text(res));
    }

    printf("running; press Ctrl-C to stop\n");
    while (g_running)
        usleep(200 * 1000);

    /* Explicit for illustration — stop() and destroy() release every link
     * on their own, and are the only cleanup a caller actually needs. */
    tpw_filter_port_unlink(video_in);
    if (audio_in)
        tpw_filter_port_unlink(audio_in);

    tpw_filter_stop(filter, false);
    tpw_filter_destroy(filter);
    return 0;
}
