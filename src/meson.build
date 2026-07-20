tinypipewire_sources = files(
  'tpw_log.c',
  'tpw_pw_core.c',
  'tpw_spa_format.c',
  'tpw_stream.c',
  'tpw_buffer.c',
  'tpw_audio_format.c',
  'tpw_video_format.c',
  'tpw_filter.c',
  'tpw_filter_port.c',
  'tpw_filter_process.c',
)

tinypipewire_lib = library('tinypipewire',
  tinypipewire_sources,
  include_directories: tpw_inc,
  dependencies: [pipewire_dep],
  install: true,
)
