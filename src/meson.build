tinypipewire_sources = files(
  'tpw_stream.c',
  'tpw_buffer.c',
  'tpw_audio_format.c',
  'tpw_video_format.c',
)

tinypipewire_lib = library('tinypipewire',
  tinypipewire_sources,
  include_directories: tpw_inc,
  dependencies: [pipewire_dep],
  install: true,
)
