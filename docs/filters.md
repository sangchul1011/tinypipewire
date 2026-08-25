# Filters

`include/tpw/tpw_filter.h` adds a second handle, `tpw_filter_h`, for
combining multiple audio/video sources into one processed output, or for
building a node other PipeWire clients can route into.

- [The filter API](#the-filter-api)
- [Signal and event ports](#signal-and-event-ports)
- [DMABUF import, hold, and cycle-rate hint](#dmabuf-import-hold-and-cycle-rate-hint)
- [Linking a port to a real device](#linking-a-port-to-a-real-device)

## The filter API

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
a filter's input port directly, with no PipeWire-level link involved. It
is callable from any thread, including from inside the processing
callback itself; the bytes are copied and delivered on the next cycle,
and only the most recent push per port is kept.
`tpw_filter_port_get_type()` reports which kind a given port handle was
added as.

Each input port's `tpw_filter_port_buffer` also carries `pts`: the
buffer's capture timestamp in nanoseconds (from the underlying SPA
node's clock, or from the `pts` a caller passed to
`tpw_filter_push_port_data()`), or -1 if unavailable. It's always -1 on
output ports and on event ports.

## Signal and event ports

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
  constructs or parses a PipeWire/SPA POD directly. An item another
  PipeWire client wrote in a control kind this library does not recognize
  still arrives, as `TPW_EVENT_UNKNOWN` with its raw undecoded bytes;
  that kind is read-only and is rejected if pushed.

## DMABUF import, hold, and cycle-rate hint

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

### Driving the bundle with a real audio device

For the sensor-fusion pattern — a fast source setting the cycle while a
slower DMABUF camera is held between its frames — the fast source must be a
real, hardware-clocked PipeWire node, not application-pushed data
(`tpw_filter_push_port_data()` stages values but does not drive the graph).
A capture device is the practical driver.

Link it through **signal ports** (with [`tpw_filter_port_link()`](#linking-a-port-to-a-real-device)):
a signal port is a mono 32-bit-float DSP channel, which is exactly what
PipeWire exposes a capture device as, so a device's per-channel ports
(`capture_FL`, `capture_FR`, …) link to signal ports natively — one signal
port per channel for a multi-channel device. The device, being
hardware-clocked, then drives the filter's processing cycle, and a DMABUF
video input with hold enabled is re-presented (same fd, `fresh == false`) on
the cycles between camera frames.

Two things to watch:

- The filter's **audio** port (`tpw_filter_add_audio_port`) carries
  interleaved raw audio and does *not* link directly into a capture device's
  DSP graph — use signal ports for device input.
- The driver is chosen per *node*, not per port (a stereo device's `FL`/`FR`
  are one node), with exactly one driver per graph and audio nodes typically
  preferred over video, so the rest follow its clock.

## Linking a port to a real device

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
port — see [the note above](#driving-the-bundle-with-a-real-audio-device).

## See also

- [Streams](streams.md) — single-source capture and audio playback
- [Logging](logging.md) — redirecting the library's diagnostics
