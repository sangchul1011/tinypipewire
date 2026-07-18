unit_tests = [
  'test_stream_lifecycle',
  'test_audio_format',
  'test_video_format',
  'test_error_notification',
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
