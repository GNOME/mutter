clutter_includesubdir = join_paths(pkgname, 'clutter')
clutter_includedir = join_paths(includedir, clutter_includesubdir)
clutter_srcdir = join_paths(top_srcdir, 'clutter')
clutter_builddir = join_paths(builddir, 'clutter')

clutter_includepath = include_directories('.', 'clutter')
clutter_includes = [clutter_includepath, cogl_includepath]

clutter_c_args = [
  '-DCLUTTER_SYSCONFDIR="@0@"'.format(join_paths(prefix, sysconfdir)),
  '-DCLUTTER_COMPILATION=1',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
  '-DG_LOG_DOMAIN="Clutter"',
]

clutter_debug_c_args = []
if buildtype.startswith('debug')
  clutter_debug_c_args += '-DG_DISABLE_CAST_CHECKS'
  if buildtype == 'debug'
    clutter_debug_c_args += '-DCLUTTER_ENABLE_DEBUG'
  endif
elif buildtype == 'release'
  clutter_debug_c_args += [
    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CHECKS',
    '-DG_DISABLE_CAST_CHECKS',
  ]
endif

clutter_c_args += clutter_debug_c_args

clutter_pkg_deps = [
  atk_dep,
  cairo_gobject_dep,
  glib_dep,
  gobject_dep,
  gio_dep,
  json_glib_dep,
  pango_dep,
]

clutter_pkg_private_deps = [
  gdk_pixbuf_dep,
  gthread_dep,
  gmodule_no_export_dep,
  pangocairo_dep,
]

if have_pango_ft2
  clutter_pkg_private_deps += [
    pangoft2_dep,
  ]
endif

if have_wayland
  clutter_pkg_private_deps += [
    wayland_egl_dep,
    wayland_server_dep,
  ]
endif

if have_x11
  clutter_pkg_deps += [
    x11_dep,
  ]
  clutter_pkg_private_deps += [
    xext_dep,
    xdamage_dep,
    xcomposite_dep,
    xtst_dep,
    xi_dep,
  ]
endif

if have_native_backend
  clutter_pkg_private_deps += [
    libudev_dep,
    libinput_dep,
    xkbcommon_dep,
  ]
endif

if have_libwacom
  clutter_pkg_private_deps += [
    libwacom_dep,
  ]
endif

clutter_deps = [
  clutter_pkg_deps,
  clutter_pkg_private_deps,
  libmutter_cogl_dep,
  m_dep
]

subdir('clutter')

if have_clutter_tests
  subdir('tests')
endif
