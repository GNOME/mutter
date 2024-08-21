clutter_includesubdir = pkgname / 'clutter'
clutter_includedir = includedir / clutter_includesubdir

clutter_includepath = include_directories('.', 'clutter')
clutter_includes = [clutter_includepath, cogl_includepath]

clutter_c_args = [
  '-DCLUTTER_SYSCONFDIR="@0@"'.format(prefix / sysconfdir),
  '-DCLUTTER_COMPILATION=1',
  '-DCOGL_ENABLE_MUTTER_API',
  '-DG_LOG_DOMAIN="Clutter"',
]

clutter_debug_c_args = []
if buildtype != 'plain'
  if get_option('debug')
    clutter_debug_c_args += [
      '-DCLUTTER_ENABLE_DEBUG',
    ]
  else
    clutter_debug_c_args += [
      '-DG_DISABLE_ASSERT',
      '-DG_DISABLE_CAST_CHECKS',
    ]
  endif
endif
clutter_debug_c_args = cc.get_supported_arguments(clutter_debug_c_args)
clutter_c_args += clutter_debug_c_args

clutter_pkg_deps = [
  atk_dep,
  cairo_dep,
  glib_dep,
  gobject_dep,
  gio_dep,
  pango_dep,
  harfbuzz_dep,
]

clutter_pkg_private_deps = [
  fribidi_dep,
  gthread_dep,
  gmodule_no_export_dep,
  pangocairo_dep,
]

clutter_deps = [
  clutter_pkg_deps,
  clutter_pkg_private_deps,
  gsettings_desktop_schemas_dep,
  libmutter_cogl_dep,
  m_dep
]

subdir('clutter')

