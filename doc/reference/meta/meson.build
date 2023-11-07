
meta_toml = configure_file(
  input: 'meta.toml.in',
  output: 'meta.toml',
  configuration: toml_conf,
  install: true,
  install_dir: docs_dir / 'meta',
)

custom_target('meta-doc',
  input: [ meta_toml, libmutter_gir[0] ],
  output: 'meta',
  command: [
    gidocgen,
    'generate',
    gidocgen_common_args,
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../mtk/mtk'),
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../clutter/clutter'),
    '--add-include-path=@0@'.format(meson.current_build_dir() / '../../../cogl/cogl'),
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  depends: [
    libmutter_mtk_gir[0],
    libmutter_clutter_gir[0],
    libmutter_cogl_gir[0],
  ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir,
)
