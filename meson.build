project('tinypipewire', 'c',
  version: '0.4.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

# SPA headers use glibc extensions (newlocale, ssize_t) that require _GNU_SOURCE.
add_project_arguments('-D_GNU_SOURCE', language: 'c')

pipewire_dep = dependency('libpipewire-0.3', version: '>=0.3.44')

tpw_inc = include_directories('include')

subdir('src')

tinypipewire_dep = declare_dependency(
  link_with: tinypipewire_lib,
  include_directories: tpw_inc,
  dependencies: [pipewire_dep],
)

install_headers(
  'include/tpw/tpw_stream.h',
  'include/tpw/tpw_filter.h',
  'include/tpw/tpw_log.h',
  subdir: 'tpw',
)

if get_option('tests')
  # Hardware tests need a real capture device, so a plain `meson test` skips
  # the whole suite; run it with `meson test --suite hardware`.
  add_test_setup('default', exclude_suites: ['hardware'], is_default: true)
  subdir('tests')
endif

if get_option('examples')
  subdir('examples')
endif

if get_option('utils')
  subdir('utils')
endif
