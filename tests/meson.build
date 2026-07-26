unit_tests = [
  'test_stream_lifecycle',
  'test_stream_target',
  'test_audio_format',
  'test_video_format',
  'test_error_notification',
  'test_filter_ports',
  'test_filter_process',
  'test_filter_error_notification',
  'test_filter_push_data',
  'test_log',
  'test_filter_signal_port',
  'test_filter_event_port',
  'test_filter_port_type',
  'test_filter_dmabuf',
  'test_filter_hold',
  'test_filter_period_hint',
  'test_filter_port_link',
]

# test_error_notification.c is a whitebox test: it reaches into
# tpw_stream_internal.h to simulate a state transition (see that file).
src_inc = include_directories('../src')

foreach name : unit_tests
  exe = executable(name, name + '.c',
    dependencies: [tinypipewire_dep],
    include_directories: src_inc,
  )
  test(name, exe)
endforeach

# Tests needing a real camera or microphone. They exit 77 (SKIP) when no
# such device is present, and are kept out of a plain `meson test` run:
# use `meson test --suite hardware` to run them.
hardware_tests = [
  'test_filter_port_link_hw',
]

foreach name : hardware_tests
  exe = executable(name, [name + '.c', 'tpw_test_hw_discover.c'],
    dependencies: [tinypipewire_dep],
    include_directories: src_inc,
  )
  test(name, exe, suite: 'hardware', timeout: 60)
endforeach
