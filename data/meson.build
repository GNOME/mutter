msgfmt = find_program('msgfmt')

desktop_conf = configuration_data()
desktop_conf.set('bindir', bindir)
desktop_conf.set('libexecdir', libexecdir)

if have_devkit
  i18n.merge_file(
    input: configure_file(
      input: 'org.gnome.Mutter.Mdk.desktop.in.in',
      output: 'org.gnome.Mutter.Mdk.desktop.in',
      configuration: desktop_conf
    ),
    output: 'org.gnome.Mutter.Mdk.desktop',
    po_dir: po_path,
    install: true,
    install_dir: desktopdir,
    type: 'desktop',
  )
endif

# Unconditionally use this dir to avoid a circular dep with gnomecc
gnome_keybindings_keysdir = datadir / 'gnome-control-center' / 'keybindings'

keybinding_xml_files = [
  '50-mutter-navigation.xml',
  '50-mutter-system.xml',
  '50-mutter-windows.xml',
]

if have_wayland
  keybinding_xml_files += [
    '50-mutter-wayland.xml',
  ]
endif

install_data(keybinding_xml_files,
  install_dir: gnome_keybindings_keysdir,
)

xwayland_grab_default_access_rules = get_option('xwayland_grab_default_access_rules')

gschema_config = configuration_data()
gschema_config.set('GETTEXT_DOMAIN', meson.project_name())
gschema_config.set('XWAYLAND_GRAB_DEFAULT_ACCESS_RULES',
                   xwayland_grab_default_access_rules)

schemadir = datadir / 'glib-2.0' / 'schemas'
schema_xmls = []
schema_xmls += configure_file(
  input: 'org.gnome.mutter.gschema.xml.in',
  output: 'org.gnome.mutter.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir
)

schema_xmls += configure_file(
  input: 'org.gnome.mutter.wayland.gschema.xml.in',
  output: 'org.gnome.mutter.wayland.gschema.xml',
  configuration: gschema_config,
  install_dir: schemadir
)

locally_compiled_schemas_dir = meson.current_build_dir()

locally_compiled_schemas = custom_target(
  output: 'gschemas.compiled',
  depend_files: schema_xmls,
  command: ['glib-compile-schemas', locally_compiled_schemas_dir],
)

locally_compiled_schemas_dep = declare_dependency(
  sources: locally_compiled_schemas
)

install_data(['mutter-schemas.convert'],
  install_dir: datadir / 'GConf/gsettings',
)

if have_libgudev
  install_data(['61-mutter.rules'],
    install_dir: udev_dir / 'rules.d',
  )
endif

subdir('dbus-interfaces')

if have_devkit
  subdir('icons')
endif
