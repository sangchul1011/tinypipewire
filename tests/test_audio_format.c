/* SPDX-License-Identifier: MIT */

#include "tpw/tpw_stream.h"
#include "tpw_test.h"

static void noop_data_cb(tpw_stream_h stream, void* data, size_t size, void* user_data)
{
    (void)stream;
    (void)data;
    (void)size;
    (void)user_data;
}

int main(void)
{
    tpw_stream_h stream = tpw_stream_create(TPW_STREAM_TYPE_AUDIO, noop_data_cb, NULL);
    TPW_ASSERT(stream != NULL);

    /* A NULL config is rejected. */
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(stream, NULL), TPW_STREAM_ERR_INVALID_ARG);

    /* Invalid formats are rejected without starting the stream. */
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(stream, &(tpw_audio_config){ .sample_rate = 0, .channels = 2 }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(stream, &(tpw_audio_config){ .sample_rate = 48000, .channels = 0 }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(stream, &(tpw_audio_config){ .sample_rate = -1, .channels = 2 }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_start(stream), TPW_STREAM_ERR_NOT_CONFIGURED);

    /* A valid config is accepted. */
    TPW_ASSERT_EQ(tpw_stream_set_audio_config(stream, &(tpw_audio_config){ .sample_rate = 48000, .channels = 2 }), TPW_STREAM_OK);

    tpw_stream_destroy(stream);
    return 0;
}
