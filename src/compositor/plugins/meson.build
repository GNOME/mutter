default_plugin_c_args = [
  '-fPIC',
  '-DG_LOG_DOMAIN="mutter"',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

default_plugin = shared_module('default',
  sources: ['default.c'],
  include_directories: mutter_includes,
  c_args: default_plugin_c_args,
  dependencies: [
    glib_dep,
    gsettings_desktop_schemas_dep,
    libmutter_clutter_dep,
  ],
  install_dir: pkglibdir / 'plugins',
  install_rpath: pkglibdir,
  install: true,
)
