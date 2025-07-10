gschema_config = configuration_data()
gschema_config.set('GETTEXT_DOMAIN', meson.project_name())

message('gschema')
configure_file(
  input: 'org.gnome.mutter.devkit.gschema.xml.in',
  output: 'org.gnome.mutter.devkit.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir,
  install: true,
)
