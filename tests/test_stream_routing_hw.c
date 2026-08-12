/* SPDX-License-Identifier: MIT */

/* Proves the library did the wiring, which is the property this feature
 * actually guarantees. A session manager may be running throughout: what
 * shows it stayed out is that the stream carries nothing until the
 * application asks, and reaches exactly the named device the moment it does.
 *
 * Runs once against a source with a capture stream and once against a sink
 * with a playback stream, since the call is meant to be direction-agnostic.
 * Exits 77 when no device of that kind is present. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tpw/tpw_stream.h>

#include "tpw_test.h"
#include "tpw_test_hw_discover.h"

#define TEST_SKIP 77

#define RATE 48000
#define CHANNELS 2

/* Long enough for the graph to settle after each wiring change. */
#define SETTLE_USEC (400 * 1000)

static unsigned g_buffers;
static int g_error;

static void on_data(tpw_stream_h stream, const tpw_stream_buffer* buf, void* user_data)
{
    (void)stream;
    (void)buf;
    (void)user_data;
    g_buffers++;
}

static void on_fill(tpw_stream_h stream, tpw_stream_playback_buffer* buf, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_buffers++;
    buf->size = 0; /* silence: this test is about wiring, not audio */
}

static void on_error(tpw_stream_h stream, int code, void* user_data)
{
    (void)stream;
    (void)user_data;
    g_error = code;
}

/* Counts links against `node`, read from the graph rather than from the
 * library's own bookkeeping. `pw-link -l` lists only ports that have links,
 * and prints each link twice — once as a port heading, once as an arrow line
 * under the peer — so only the arrow lines are counted. */
static int links_to(const char* node)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pw-link -l 2>/dev/null | grep '%s' | grep -c '|' || true", node);

    FILE* p = popen(cmd, "r");
    if (!p)
        return -1;
    int n = -1;
    if (fscanf(p, "%d", &n) != 1)
        n = -1;
    pclose(p);
    return n;
}

/* One direction's worth of the whole lifecycle. */
static void exercise(tpw_stream_h s, const char* device)
{
    tpw_audio_config cfg = { .sample_rate = RATE, .channels = CHANNELS, .format = "S16" };

    TPW_ASSERT_EQ(tpw_stream_set_error_cb(s, on_error), TPW_STREAM_OK);
    TPW_ASSERT_EQ(tpw_stream_set_autoconnect(s, false), TPW_STREAM_OK);
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(s, &cfg), TPW_STREAM_OK);
    TPW_ASSERT_EQ(tpw_stream_start(s), TPW_STREAM_OK);

    /* Nothing wired us. This is the assertion the feature exists for. */
    usleep(SETTLE_USEC);
    int before = links_to(device);
    g_buffers = 0;
    usleep(SETTLE_USEC);
    TPW_ASSERT_EQ(g_buffers, 0u);

    TPW_ASSERT_EQ(tpw_stream_link(s, device), TPW_STREAM_OK);
    usleep(SETTLE_USEC);
    int after = links_to(device);
    printf("  links %d -> %d (expected +%d)\n", before, after, CHANNELS);
    TPW_ASSERT(after == before + CHANNELS);

    /* Data flows once, and only once, we asked for it. */
    g_buffers = 0;
    usleep(SETTLE_USEC);
    TPW_ASSERT(g_buffers > 0);

    /* Stopping must NOT release the wiring — a stopped stream resumes on the
     * same device rather than silently running unconnected. */
    TPW_ASSERT_EQ(tpw_stream_stop(s), TPW_STREAM_OK);
    usleep(SETTLE_USEC);
    TPW_ASSERT_EQ(links_to(device), after);

    TPW_ASSERT_EQ(tpw_stream_start(s), TPW_STREAM_OK);
    g_buffers = 0;
    usleep(SETTLE_USEC);
    TPW_ASSERT(g_buffers > 0);
    TPW_ASSERT_EQ(links_to(device), after);

    /* Linking again while linked is refused, and leaves the links alone. */
    TPW_ASSERT_EQ(tpw_stream_link(s, device), TPW_STREAM_ERR_INVALID_ARG);
    TPW_ASSERT_EQ(links_to(device), after);

    /* Releasing puts the graph back where it started. */
    TPW_ASSERT_EQ(tpw_stream_unlink(s), TPW_STREAM_OK);
    usleep(SETTLE_USEC);
    TPW_ASSERT_EQ(links_to(device), before);
    TPW_ASSERT_EQ(tpw_stream_unlink(s), TPW_STREAM_ERR_INVALID_ARG);

    /* And it can be wired again afterwards. */
    TPW_ASSERT_EQ(tpw_stream_link(s, device), TPW_STREAM_OK);
    usleep(SETTLE_USEC);
    TPW_ASSERT_EQ(links_to(device), after);

    TPW_ASSERT_EQ(g_error, 0);

    tpw_stream_stop(s);
    tpw_stream_destroy(s);

    /* Destroy releases what stop kept. */
    usleep(SETTLE_USEC);
    TPW_ASSERT_EQ(links_to(device), before);
}

int main(void)
{
    char source[256], sink[256];
    bool have_source = tpw_test_find_node("Audio/Source", source, sizeof(source));
    bool have_sink = tpw_test_find_node("Audio/Sink", sink, sizeof(sink));

    if (!have_source && !have_sink) {
        printf("no audio device present, skipping\n");
        return TEST_SKIP;
    }

    if (have_source) {
        printf("capture -> %s\n", source);
        tpw_stream_h s = tpw_stream_create(TPW_STREAM_TYPE_AUDIO, on_data, NULL);
        if (!s) {
            printf("no PipeWire connection, skipping\n");
            return TEST_SKIP;
        }
        g_error = 0;
        exercise(s, source);
    }

    if (have_sink) {
        printf("playback -> %s\n", sink);
        tpw_stream_h s = tpw_stream_create_playback(on_fill, NULL);
        if (!s) {
            printf("no PipeWire connection, skipping\n");
            return TEST_SKIP;
        }
        g_error = 0;
        exercise(s, sink);
    }

    printf("test_stream_routing_hw: passed\n");
    return 0;
}
