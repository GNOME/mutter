gtkdoc_entities_cdata = configuration_data()
gtkdoc_entities_cdata.set_quoted('PACKAGE', meson.project_name())
gtkdoc_entities_cdata.set_quoted('PACKAGE_BUGREPORT', 'https://gitlab.gnome.org/GNOME/mutter/issues/new')
gtkdoc_entities_cdata.set_quoted('PACKAGE_NAME', meson.project_name())
gtkdoc_entities_cdata.set_quoted('PACKAGE_STRING', '@0@ - @1@'.format(meson.project_name(), meson.project_version()))
gtkdoc_entities_cdata.set_quoted('PACKAGE_TARNAME', 'mutter.tar.xz')
gtkdoc_entities_cdata.set_quoted('PACKAGE_URL', 'https://gitlab.gnome.org/GNOME/mutter/')
gtkdoc_entities_cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
configure_file(
  input: 'gtkdocentities.ent.in',
  output: 'gtkdocentities.ent',
  configuration: gtkdoc_entities_cdata,
)
