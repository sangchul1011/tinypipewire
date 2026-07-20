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
tpw_filter_port_h tpw_filter_add_signal_port(tpw_filter_h filter, tpw_filter_port_direction direction);
tpw_filter_port_h tpw_filter_add_event_port(tpw_filter_h filter, tpw_filter_port_direction direction);
tpw_stream_type tpw_filter_port_get_type(tpw_filter_port_h port);
int tpw_filter_push_port_data(tpw_filter_h filter, tpw_filter_port_h port, const void* data, size_t size);
int tpw_filter_start(tpw_filter_h filter);
int tpw_filter_stop(tpw_filter_h filter);
void tpw_filter_destroy(tpw_filter_h filter);
```

A filter starts empty; ports are added one at a time (each as input or
output, reusing the same config structs as `tpw_stream` for audio/video),
and its `tpw_filter_process_cb` is invoked once per cycle with every
port's buffer together, so the callback can read multiple inputs and
write one output in a single synchronized point. `tpw_filter_push_port_data()`
lets application code (for example, a `tpw_stream` capture callback) feed
a filter's input port directly, with no PipeWire-level link involved.
`tpw_filter_port_get_type()` reports which kind a given port handle was
added as.

Beyond audio/video, a filter can also carry two more port kinds, freely
mixed with the others on the same filter and delivered through the same
callback:

- **Signal ports** (`tpw_filter_add_signal_port`) carry one continuous
  channel of raw 32-bit float values — for example, a sensor reading —
  one value per frame of each cycle, through the same `data`/`size`
  buffer fields audio/video ports already use. No format configuration
  is needed.
- **Event ports** (`tpw_filter_add_event_port`) carry zero or more
  discrete, time-stamped `tpw_event` items per cycle — MIDI or OSC
  messages (real wire-format bytes, for interop with other PipeWire
  MIDI/OSC clients) or property/key-value changes — read and written
  through a small accessor API instead of a raw buffer:

  ```c
  size_t tpw_filter_port_event_count(tpw_filter_port_h port);
  int tpw_filter_port_get_event(tpw_filter_port_h port, size_t index, tpw_event* out);
  int tpw_filter_port_push_event(tpw_filter_port_h port, const tpw_event* event);
  ```

  On an input event port, `tpw_filter_port_push_event()` stages an event
  for delivery on the next cycle (the event-port equivalent of
  `tpw_filter_push_port_data()`); on an output event port, it must be
  called from within the processing callback and publishes the event
  when that cycle ends. Neither the caller nor the library ever
  constructs or parses a PipeWire/SPA POD directly.

### Logging

`include/tpw/tpw_log.h` lets an application redirect or filter the
library's internal diagnostic messages instead of only seeing raw
PipeWire stderr output:

```c
typedef enum { TPW_LOG_ERROR, TPW_LOG_WARNING, TPW_LOG_INFO, TPW_LOG_DEBUG, TPW_LOG_VERBOSE } tpw_log_level;
typedef void (*tpw_log_cb)(tpw_log_level level, const char* file, int line, const char* message, void* user_data);

void tpw_log_set_callback(tpw_log_cb callback, void* user_data);
void tpw_log_set_level(tpw_log_level level);
```

With no callback registered, messages are written to stderr, tagged
with the source file (basename) and line that logged them, mirroring
PipeWire's own log output. The minimum level defaults to
`TPW_LOG_WARNING`; call `tpw_log_set_level()` to see
`TPW_LOG_INFO`/`TPW_LOG_DEBUG`/`TPW_LOG_VERBOSE` messages too, or to
route everything through your own logger:

```c
void my_logger(tpw_log_level level, const char* file, int line, const char* message, void* user_data) {
    fprintf(stderr, "[myapp] %s:%d: %s\n", file, line, message);
}
tpw_log_set_callback(my_logger, NULL);
```

## Examples

- `examples/audio_capture.c` — capture from the default audio source and
  print each buffer's size
- `examples/video_capture.c` — capture from the default camera source and
  print each frame's size
- `examples/filter_mix.c` — mix two audio input ports into one audio
  output port
- `examples/filter_signal_port.c` — feed a synthetic signal port
  alongside an audio port into one filter
- `examples/filter_event_port.c` — echo events from an event input port
  back out through an event output port

Run them after building:

```sh
./build/examples/audio_capture
./build/examples/video_capture
./build/examples/filter_mix
./build/examples/filter_signal_port
./build/examples/filter_event_port
```

## License

MIT — see [LICENSE](LICENSE).
