# tinypipewire

A small C library that wraps PipeWire's `pw_stream` API behind a simpler,
unified interface for capturing audio and video. It hides PipeWire's
thread-loop management, SPA POD format negotiation, and buffer
dequeue/queue plumbing, exposing only a small opaque-handle API.

Audio and camera **capture** are supported. Audio **playback** is out of
scope for this version.

## Build

Requires [Meson](https://mesonbuild.com/), Ninja, and PipeWire development
files (`libpipewire-0.3` >= 0.3.40) discoverable via pkg-config.

```sh
meson setup build
meson compile -C build
meson test -C build
```

## API

The public interface is the single header `include/tpw/tpw_stream.h`:

```c
tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data);
int tpw_stream_set_error_cb(tpw_stream_h stream, tpw_stream_error_cb callback);
int tpw_stream_set_audio_config(tpw_stream_h stream, const tpw_audio_config* config);
int tpw_stream_set_video_config(tpw_stream_h stream, const tpw_video_config* config);
int tpw_stream_start(tpw_stream_h stream);
int tpw_stream_stop(tpw_stream_h stream);
void tpw_stream_destroy(tpw_stream_h stream);
```

Format is passed as a config struct, so new fields can be added without
changing the call signature:

```c
tpw_audio_config a = { .sample_rate = 48000, .channels = 2 };
tpw_stream_set_audio_config(stream, &a);

tpw_video_config v = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };
tpw_stream_set_video_config(stream, &v);   /* fps = 0 lets the source pick the rate */
```

One `tpw_stream_h` captures either audio or video; both types share the
same creation, control, and data-callback functions.

### Filters

`include/tpw/tpw_filter.h` adds a second handle, `tpw_filter_h`, for
combining multiple audio/video sources into one processed output, or for
building a node other PipeWire clients can route into:

```c
tpw_filter_h tpw_filter_create(const char* name, tpw_filter_process_cb callback, void* user_data);
int tpw_filter_set_error_cb(tpw_filter_h filter, tpw_filter_error_cb callback);
tpw_filter_port_h tpw_filter_add_audio_port(tpw_filter_h filter, tpw_filter_port_direction direction, const tpw_audio_config* config);
tpw_filter_port_h tpw_filter_add_video_port(tpw_filter_h filter, tpw_filter_port_direction direction, const tpw_video_config* config);
int tpw_filter_push_port_data(tpw_filter_h filter, tpw_filter_port_h port, const void* data, size_t size);
int tpw_filter_start(tpw_filter_h filter);
int tpw_filter_stop(tpw_filter_h filter);
void tpw_filter_destroy(tpw_filter_h filter);
```

A filter starts empty; ports are added one at a time (each as input or
output, audio or video, reusing the same config structs as `tpw_stream`),
and its `tpw_filter_process_cb` is invoked once per cycle with every
port's buffer together, so the callback can read multiple inputs and
write one output in a single synchronized point. `tpw_filter_push_port_data()`
lets application code (for example, a `tpw_stream` capture callback) feed
a filter's input port directly, with no PipeWire-level link involved.

## Examples

- `examples/audio_capture.c` — capture from the default audio source and
  print each buffer's size
- `examples/video_capture.c` — capture from the default camera source and
  print each frame's size
- `examples/filter_mix.c` — mix two audio input ports into one audio
  output port

Run them after building:

```sh
./build/examples/audio_capture
./build/examples/video_capture
./build/examples/filter_mix
```

## License

MIT — see [LICENSE](LICENSE).
