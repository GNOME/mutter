
cogl_toml = configure_file(
  input: 'cogl.toml.in',
  output: 'cogl.toml',
  configuration: toml_conf,
  install: true,
  install_dir: docs_dir / 'cogl',
)

custom_target('cogl-doc',
  input: [ cogl_toml, libmutter_cogl_gir[0] ],
  output: 'cogl',
  command: [
    gidocgen,
    'generate',
    gidocgen_common_args,
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../mtk/mtk'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir,
)
