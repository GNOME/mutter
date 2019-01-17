subdir('man')
subdir('xml')

# Reference manual
version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_xml = configure_file(
  input: 'version.xml.in',
  output: 'version.xml',
  configuration: version_conf
)

gnome.gtkdoc(meson.project_name(),
  main_sgml: 'mutter-docs.sgml',
  # gobject_typesfile: 'mutter.types',
  # scan_args: '--rebuild-types',
  # c_args: mutter_c_args,
  dependencies: [libmutter_dep, mutter_deps ],
  src_dir: include_directories('../src'),
)
