tpw_version_parts = meson.project_version().split('.')

tpw_version_cfg = configuration_data()
tpw_version_cfg.set('TPW_VERSION_MAJOR', tpw_version_parts[0])
tpw_version_cfg.set('TPW_VERSION_MINOR', tpw_version_parts[1])
tpw_version_cfg.set('TPW_VERSION_PATCH', tpw_version_parts[2])

configure_file(
  input: 'tpw_version.h.in',
  output: 'tpw_version.h',
  configuration: tpw_version_cfg,
  install: true,
  install_dir: get_option('includedir') / 'tpw',
)
