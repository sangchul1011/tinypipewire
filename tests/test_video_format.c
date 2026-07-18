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
    tpw_stream_h stream = tpw_stream_create(TPW_STREAM_TYPE_VIDEO, noop_data_cb, NULL);
    TPW_ASSERT(stream != NULL);

    /* A NULL config is rejected. */
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, NULL), TPW_STREAM_ERR_INVALID_ARG);

    /* Invalid dimensions, unrecognized pixel formats, and negative fps are rejected. */
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, &(tpw_video_config){ .width = 0, .height = 480, .pixel_format = "RGB" }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, &(tpw_video_config){ .width = 640, .height = -1, .pixel_format = "RGB" }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, &(tpw_video_config){ .width = 640, .height = 480, .pixel_format = "NOT_A_FORMAT" }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, &(tpw_video_config){ .width = 640, .height = 480, .pixel_format = "RGB", .fps = -1 }), TPW_STREAM_ERR_INVALID_FORMAT);
    TPW_ASSERT_EQ(tpw_stream_start(stream), TPW_STREAM_ERR_NOT_CONFIGURED);

    /* A valid config (with an explicit frame rate) is accepted. */
    TPW_ASSERT_EQ(tpw_stream_set_video_config(stream, &(tpw_video_config){ .width = 640, .height = 480, .pixel_format = "RGB", .fps = 30 }), TPW_STREAM_OK);

    tpw_stream_destroy(stream);
    return 0;
}
