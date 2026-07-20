/* SPDX-License-Identifier: MIT */

#include <unistd.h>

#include "tpw/tpw_filter.h"
#include "tpw_test.h"

static int g_cycles = 0;
static size_t g_last_n_buffers = 0;

static void process_cb(tpw_filter_h filter, tpw_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)user_data;
    g_cycles++;
    g_last_n_buffers = n_buffers;
    /* Output buffers are left untouched (size stays 0); this must not
     * be treated as an error by the library. */
}

static int g_mixed_cycles = 0;
static size_t g_mixed_n_buffers = 0;

static void mixed_process_cb(tpw_filter_h filter, tpw_filter_port_buffer* buffers, size_t n_buffers, void* user_data)
{
    (void)filter;
    (void)buffers;
    (void)user_data;
    g_mixed_cycles++;
    g_mixed_n_buffers = n_buffers;
}

int main(void)
{
    tpw_filter_h filter = tpw_filter_create("tpw-test-process", process_cb, NULL);
    TPW_ASSERT(filter != NULL);

    tpw_audio_config cfg = { .sample_rate = 48000, .channels = 2 };
    TPW_ASSERT(tpw_filter_add_audio_port(filter, TPW_FILTER_PORT_INPUT, &cfg) != NULL);
    TPW_ASSERT(tpw_filter_add_audio_port(filter, TPW_FILTER_PORT_INPUT, &cfg) != NULL);
    TPW_ASSERT(tpw_filter_add_audio_port(filter, TPW_FILTER_PORT_OUTPUT, &cfg) != NULL);

    TPW_ASSERT_EQ(tpw_filter_start(filter), TPW_STREAM_OK);
    sleep(1);

    /* All three ports' buffers must arrive together, every cycle. */
    TPW_ASSERT(g_cycles > 0);
    TPW_ASSERT_EQ(g_last_n_buffers, (size_t)3);

    tpw_filter_stop(filter);
    tpw_filter_destroy(filter);

    /* One port of each of the four supported kinds on a single filter
     * must still be delivered together in one callback invocation per
     * cycle (audio/video/signal/event mixing). */
    tpw_filter_h mixed = tpw_filter_create("tpw-test-process-mixed", mixed_process_cb, NULL);
    TPW_ASSERT(mixed != NULL);

    TPW_ASSERT(tpw_filter_add_audio_port(mixed, TPW_FILTER_PORT_INPUT, &cfg) != NULL);
    tpw_video_config vcfg = { .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 };
    TPW_ASSERT(tpw_filter_add_video_port(mixed, TPW_FILTER_PORT_INPUT, &vcfg) != NULL);
    TPW_ASSERT(tpw_filter_add_signal_port(mixed, TPW_FILTER_PORT_INPUT) != NULL);
    TPW_ASSERT(tpw_filter_add_event_port(mixed, TPW_FILTER_PORT_INPUT) != NULL);

    TPW_ASSERT_EQ(tpw_filter_start(mixed), TPW_STREAM_OK);
    sleep(1);

    TPW_ASSERT(g_mixed_cycles > 0);
    TPW_ASSERT_EQ(g_mixed_n_buffers, (size_t)4);

    tpw_filter_stop(mixed);
    tpw_filter_destroy(mixed);
    return 0;
}
