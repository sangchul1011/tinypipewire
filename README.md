# tinypipewire

A small C library that wraps PipeWire's `pw_stream` API behind a simpler,
unified interface for capturing audio and video. It hides PipeWire's
thread-loop management, SPA POD format negotiation, and buffer
dequeue/queue plumbing, exposing only a small opaque-handle API.

Audio and camera **capture** are supported. Audio **playback** is out of
scope for this version.

## Build

Requires [Meson](https://mesonbuild.com/), Ninja, and PipeWire development
files (`libpipewire-0.3` >= 0.3.44) discoverable via pkg-config.

```sh
meson setup build
meson compile -C build
meson test -C build
```

`meson test` needs no hardware. Tests that do — they link a real camera or
microphone into a filter — live in a separate suite that is skipped unless
asked for, and report SKIP rather than failing when no such device is
attached:

```sh
meson test -C build --suite hardware
```

## API

The public interface is the single header `include/tpw/tpw_stream.h`:

```c
tpw_stream_h tpw_stream_create(tpw_stream_type type, tpw_stream_data_cb callback, void* user_data);
int tpw_stream_set_error_cb(tpw_stream_h stream, tpw_stream_error_cb callback);
int tpw_stream_set_target(tpw_stream_h stream, const char* target);
int tpw_stream_set_audio_config(tpw_stream_h stream, const tpw_audio_config* config);
int tpw_stream_set_video_config(tpw_stream_h stream, const tpw_video_config* config);
int tpw_stream_start(tpw_stream_h stream);
int tpw_stream_stop(tpw_stream_h stream);
void tpw_stream_destroy(tpw_stream_h stream);
```

Format is passed as a config struct, so new fields can be added without
changing the call signature:

```c
tpw_audio_config a = { .sample_rate = 48000, .channels = 2, .format = "F32" };
tpw_stream_set_audio_config(stream, &a);   /* format = NULL defaults to "S16" */

tpw_video_config v = { .width = 640, .height = 480, .pixel_format = "YUYV", .fps = 30 };
tpw_stream_set_video_config(stream, &v);   /* fps = 0 lets the source pick the rate */
```

One `tpw_stream_h` captures either audio or video; both types share the
same creation, control, and data-callback functions.

By default a stream auto-connects to PipeWire's default source for its
media type. `tpw_stream_set_target()` points it at a specific node by
name or serial instead (see `wpctl status` or `pw-cli ls Node`).
Call it before `tpw_stream_set_audio_config()`/`tpw_stream_set_video_config()`,
which is what actually connects the stream.

`tpw_stream_data_cb` receives a `const tpw_stream_buffer*` rather than
loose `data`/`size` parameters, so future fields can be added without
changing the callback signature. It currently carries:

```c
typedef struct {
    void* data;
    size_t size;
    int64_t pts; /* capture timestamp in nanoseconds (the driver clock
                    used by the underlying SPA node, e.g. ALSA or
                    V4L2), or -1 if the buffer had no timestamp
                    metadata. */
} tpw_stream_buffer;
```

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
int tpw_filter_push_port_data(tpw_filter_h filter, tpw_filter_port_h port, const void* data, size_t size, int64_t pts);
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

Each input port's `tpw_filter_port_buffer` also carries `pts`: the
buffer's capture timestamp in nanoseconds (from the underlying SPA
node's clock, or from the `pts` a caller passed to
`tpw_filter_push_port_data()`), or -1 if unavailable. It's always -1 on
output ports and on event ports.

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

#### DMABUF import, hold, and cycle-rate hint

For zero-copy sensor-fusion bundling — combining a camera with faster
sources and handing every input to an in-process consumer each cycle — a
filter's video **input** port can import DMABUF file descriptors instead
of a CPU-mapped buffer, and any input port can *hold* its most recent
buffer across cycles where no new data arrives:

```c
typedef enum { TPW_PORT_MEMORY_AUTO, TPW_PORT_MEMORY_DMABUF } tpw_port_memory;
typedef struct { tpw_port_memory memory; } tpw_filter_port_opts;
typedef struct { int fd; uint32_t offset; uint32_t stride; uint32_t size; } tpw_dmabuf_plane;

tpw_filter_port_h tpw_filter_add_video_port_ex(tpw_filter_h filter, tpw_filter_port_direction direction,
                                               const tpw_video_config* config, const tpw_filter_port_opts* opts);
size_t tpw_filter_port_buffer_dmabuf(const tpw_filter_port_buffer* buf, tpw_dmabuf_plane* planes, size_t max_planes);
int tpw_filter_port_set_hold(tpw_filter_port_h port, bool enable);
int tpw_filter_set_period_hint(tpw_filter_h filter, uint32_t max_period_ns);
```

- **DMABUF import** — `tpw_filter_add_video_port_ex()` with
  `opts->memory == TPW_PORT_MEMORY_DMABUF` makes a video input port
  negotiate DMABUF frames (import-only; the source allocates, the filter
  consumes the fd). `opts == NULL` is exactly `tpw_filter_add_video_port()`.
  On such a port the buffer's `data` is NULL; read the frame's planes with
  `tpw_filter_port_buffer_dmabuf()`, which returns the plane count and fills
  `fd`/`offset`/`stride`/`size` per plane. It returns 0 for a non-DMABUF
  port, never fabricating an fd. If the linked source cannot provide DMABUF,
  the port simply delivers no buffers and the condition is logged — there is
  no silent CPU-copy fallback. The `fd` is borrowed for the callback only.
- **Hold + freshness** — `tpw_filter_port_set_hold(port, true)` (before
  start) makes an input port re-present its single most recent buffer (the
  same DMABUF fd) on cycles with no new data, so a slow camera stays in
  every bundle alongside a faster source. Each `tpw_filter_port_buffer`
  carries `bool fresh` (true only for a newly arrived buffer) and
  `uint64_t seq` (advances only on new data), so the callback can tell a
  held buffer from a fresh one and count how long it has been held.
- **Cycle-rate hint** — `tpw_filter_set_period_hint()` (before start)
  expresses a preferred maximum bundling period in nanoseconds as a latency
  preference; the PipeWire graph still chooses the driving clock (a faster
  source pulls the cycle finer). `0` clears it.

**Driving the bundle with a real audio device.** For the sensor-fusion
pattern — a fast source setting the cycle while a slower DMABUF camera is
held between its frames — the fast source must be a real, hardware-clocked
PipeWire node, not application-pushed data (`tpw_filter_push_port_data()`
stages values but does not drive the graph). A capture device is the
practical driver. Link it through **signal ports** (with
`tpw_filter_port_link()`, below): a signal port is a mono 32-bit-float DSP
channel, which is exactly what PipeWire exposes a capture device as, so a
device's per-channel ports (`capture_FL`, `capture_FR`, …) link to signal
ports natively — one signal port per channel for a multi-channel device.
The device, being hardware-clocked,
then drives the filter's processing cycle, and a DMABUF video input with
hold enabled is re-presented (same fd, `fresh == false`) on the cycles
between camera frames. Note two things: the filter's **audio** port
(`tpw_filter_add_audio_port`) carries interleaved raw audio and does *not*
link directly into a capture device's DSP graph — use signal ports for
device input; and the driver is chosen per *node*, not per port (a stereo
device's `FL`/`FR` are one node), with exactly one driver per graph and
audio nodes typically preferred over video, so the rest follow its clock.

#### Linking a port to a real device

A filter input port can be connected straight to a capture device with a
PipeWire **core link** — no `pw-link` call and no session-manager routing
policy, so an application wires its own graph:

```c
int tpw_filter_port_link(tpw_filter_port_h port, const char* target);
int tpw_filter_port_unlink(tpw_filter_port_h port);
```

- **Target syntax** — `target` is a node name, an `object.serial` (a string
  of digits), or `"node:port"` to pin an exact output port on that node.
  Names are the ones `wpctl status` and `pw-cli ls Node` print. When only a
  node is named, PipeWire picks a compatible output port on it, so a stereo
  microphone resolves to `capture_FL` and a camera to `capture_1` without
  the caller naming them.
- **Call it after `tpw_filter_start()`** — this is the one port call that
  is *not* pre-start. The target is looked up in the running graph, so the
  filter's own node has to exist there first. Calling it earlier returns
  `TPW_STREAM_ERR_NOT_CONFIGURED`.
- **Input ports only**, one link at a time. Linking an already-linked port
  returns `TPW_STREAM_ERR_INVALID_ARG`; unlink first to re-target it.
- **The filter owns its links** — `tpw_filter_stop()` and
  `tpw_filter_destroy()` release every link, so `tpw_filter_port_unlink()`
  is only needed to re-target a port while the filter keeps running.
- **Failures are clean and synchronous** — the call blocks until the link
  negotiates. An unknown target gives `TPW_STREAM_ERR_INVALID_ARG` and a
  format that cannot negotiate gives `TPW_STREAM_ERR_INVALID_FORMAT`; in
  neither case is a partial link left behind. If a linked device later
  disappears, the filter's error callback reports
  `TPW_STREAM_ERR_SOURCE_UNAVAILABLE` for that port.

Remember that an audio device links to a **signal** port, not an audio
port — see the note above.

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
- `examples/filter_dmabuf_bundle.c` — bundle a DMABUF camera input (with
  hold) and a faster signal input, printing each frame's fd and freshness
- `examples/filter_port_link.c` — link a camera and a microphone straight
  into a filter by node name, with no `pw-link` step

Run them after building:

```sh
./build/examples/audio_capture
./build/examples/video_capture
./build/examples/filter_mix
./build/examples/filter_signal_port
./build/examples/filter_event_port
# takes node names from `wpctl status`
./build/examples/filter_port_link <video-node> [audio-node]
```

## Utilities

- `utils/tpw_record.c` — record the default (or a chosen) audio or
  video source to a file:

  ```sh
  ./build/utils/tpw_record -o out.wav                      # audio, default source, until Ctrl+C
  ./build/utils/tpw_record -o out.pcm -f pcm -d 10          # raw PCM, 10 seconds
  ./build/utils/tpw_record -o out.wav --device alsa_input.usb-...  # pick a source (see `wpctl status`)
  ./build/utils/tpw_record -o out.wav --sample-rate 44100 --channels 1 --bits 24 -d 5  # 44.1kHz mono, 24-bit

  ./build/utils/tpw_record -o out.raw -t video -d 5         # video, raw I420 frames, 5 seconds
  ./build/utils/tpw_record -o out.y4m -t video -f y4m --fps 30 -d 5  # playable YUV4MPEG2
  ./build/utils/tpw_record -o out.raw -t video --width 1280 --height 720 -d 5  # 720p raw I420
  ./build/utils/tpw_record -o out.raw -t video --device v4l2_input.usb-... -d 5  # pick a camera (see `wpctl status`)
  ```

## License

MIT — see [LICENSE](LICENSE).
