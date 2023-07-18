mtk_toml = configure_file(
  input: 'mtk.toml.in',
  output: 'mtk.toml',
  configuration: toml_conf,
  install: true,
  install_dir: docs_dir / 'mtk',
)

custom_target('mtk-doc',
  input: [ mtk_toml, libmutter_mtk_gir[0] ],
  output: 'mtk',
  command: [
    gidocgen,
    'generate',
    gidocgen_common_args,
    '--config=@INPUT0@',
    '--output-dir=@OUTPUT@',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT1@',
  ],
  build_by_default: true,
  install: true,
  install_dir: docs_dir,
)
