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
    return 0;
}
