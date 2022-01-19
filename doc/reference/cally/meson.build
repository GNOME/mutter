
cally_toml = configure_file(
  input: 'cally.toml.in',
  output: 'cally.toml',
  configuration: toml_conf,
  install: true,
  install_dir: docs_dir / 'cally',
)

custom_target('cally-doc',
  input: [ cally_toml, libmutter_cally_gir[0] ],
  output: 'cally',
  command: [
    gidocgen,
    'generate',
    gidocgen_common_args,
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../cogl/cogl'),
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../cogl/cogl-pango'),
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../clutter/clutter'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  depends: [
    libmutter_cogl_gir[0],
    libmutter_cogl_pango_gir[0],
    libmutter_clutter_gir[0],
  ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir,
)
