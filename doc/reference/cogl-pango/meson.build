
cogl_pango_toml = configure_file(
  input: 'cogl-pango.toml.in',
  output: 'cogl-pango.toml',
  configuration: toml_conf,
  install: true,
  install_dir: docs_dir / 'cogl-pango',
)

custom_target('cogl-pango-doc',
  input: [ cogl_pango_toml, libmutter_cogl_pango_gir[0] ],
  output: 'cogl-pango',
  command: [
    gidocgen,
    'generate',
    gidocgen_common_args,
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../mtk/mtk'),
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../cogl/cogl'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  depends: [
    libmutter_cogl_gir[0],
  ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir,
)
