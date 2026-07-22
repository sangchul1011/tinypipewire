/* SPDX-License-Identifier: MIT */

#ifndef TPW_SPA_FORMAT_INTERNAL_H
#define TPW_SPA_FORMAT_INTERNAL_H

#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include "tpw/tpw_stream.h"

/* Maps a sample format string (e.g. "S16") to its enum spa_audio_format,
 * or SPA_AUDIO_FORMAT_UNKNOWN if unrecognized. */
enum spa_audio_format tpw_spa_lookup_audio_format(const char* name);

/* Builds the raw-audio SPA_TYPE_OBJECT_Format POD for `config`/`fmt`
 * using `b`. Shared by tpw_stream's and tpw_filter's audio format
 * setters so the SPA POD-building code exists only once. Does not
 * validate `config`. */
const struct spa_pod* tpw_spa_build_audio_format(struct spa_pod_builder* b, const tpw_audio_config* config,
                                                  enum spa_audio_format fmt);

/* Maps a pixel_format string (e.g. "RGB") to its enum spa_video_format,
 * or SPA_VIDEO_FORMAT_UNKNOWN if unrecognized. */
enum spa_video_format tpw_spa_lookup_pixel_format(const char* name);

/* Builds the raw-video SPA_TYPE_OBJECT_Format POD for `config`/`fmt`
 * using `b`. Shared by tpw_stream's and tpw_filter's video format
 * setters. Does not validate `config`. */
const struct spa_pod* tpw_spa_build_video_format(struct spa_pod_builder* b, const tpw_video_config* config,
                                                  enum spa_video_format fmt);

/* Builds the SPA_TYPE_OBJECT_Format POD for a filter signal port using
 * `b`: audio/dsp media type fixed to 32-bit float, no per-instance
 * fields since there is nothing left for a caller to configure. */
const struct spa_pod* tpw_spa_build_signal_format(struct spa_pod_builder* b);

/* Builds the SPA_TYPE_OBJECT_Format POD for a filter event port using
 * `b`: application/control media type, no per-instance fields. */
const struct spa_pod* tpw_spa_build_event_format(struct spa_pod_builder* b);

#endif /* TPW_SPA_FORMAT_INTERNAL_H */
