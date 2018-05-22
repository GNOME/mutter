default_plugin_c_args = [
  '-fPIC',
  '-DG_LOG_DOMAIN="mutter"',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

default_plugin = shared_module('default',
  sources: ['default.c'],
  include_directories: mutter_includepath,
  c_args: default_plugin_c_args,
  #link_args: ['-Wl,-no-undefined'],
  #link_args: ['-Wl,-module', '-Wl,-avoid-version', '-Wl,-no-undefined'],
  dependencies: [
    glib_dep,
    gtk3_dep,
    json_glib_dep,
    gsettings_desktop_schemas_dep,
    libmutter_clutter_dep,
  ],
  install_dir: join_paths(pkglibdir, 'plugins'),
  install: true,
)
