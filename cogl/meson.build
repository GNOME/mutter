cogl_includesubdir = join_paths(pkgname, 'cogl')
cogl_includedir = join_paths(includedir, cogl_includesubdir)
cogl_srcdir = join_paths(top_srcdir, 'cogl')
cogl_builddir = join_paths(builddir, 'cogl')

cogl_includepath = include_directories('.', 'cogl')

cdata = configuration_data()
cdata.set('HAVE_COGL_GL', have_gl)
cdata.set('HAVE_COGL_GLES2', have_gles2)
cdata.set('ENABLE_UNIT_TESTS', have_cogl_tests)

cogl_config_h = configure_file(
  input: 'cogl-config.h.meson',
  output: 'cogl-config.h',
  configuration: cdata)

# TODO: Remove this when autotools support is removed
cogl_mutter_config_h = configure_file(
  input: 'cogl-config.h.meson',
  output: 'cogl-mutter-config.h',
  configuration: cdata)

cogl_pkg_deps = [
  glib_dep,
  gobject_dep,
]

cogl_pkg_private_deps = [
  cairo_dep,
  gmodule_no_export_dep,
  gdk_pixbuf_dep,
  #uprof_dep,
]

if have_wayland
  cogl_pkg_deps += [
    wayland_server_dep,
  ]
endif

if have_egl
  cogl_pkg_deps += [
    egl_dep,
  ]
endif

if have_x11
  cogl_pkg_deps += [
    x11_dep,
  ]
  cogl_pkg_private_deps += [
    xext_dep,
    xfixes_dep,
    xdamage_dep,
    xcomposite_dep,
    xrandr_dep,
  ]
endif

if have_gl
  cogl_pkg_deps += [
    gl_dep,
  ]
endif

if have_gles2
  cogl_pkg_deps += [
    gles2_dep,
  ]
endif

cogl_deps = [
  cogl_pkg_deps,
  cogl_pkg_private_deps,
  m_dep,
]

cogl_c_args = [
  '-DCOGL_LOCALEDIR="@0@"'.format(localedir),
  '-DCOGL_COMPILATION',
]

if have_gl
  cogl_c_args += [
    '-DCOGL_GL_LIBNAME="@0@"'.format(gl_libname)
  ]
endif

if have_gles2
  cogl_c_args += [
    '-DCOGL_GLES2_LIBNAME="@0@"'.format(gles2_libname)
  ]
endif

cogl_debug_c_args = []
if buildtype.startswith('debug')
  cogl_debug_c_args += [
    '-DCOGL_GL_DEBUG',
    '-DCOGL_OBJECT_DEBUG',
    '-DCOGL_ENABLE_DEBUG',
  ]
elif buildtype == 'release'
  cogl_debug_c_args += [
    '-DG_DISABLE_CHECKS',
    '-DG_DISABLE_CAST_CHECKS',
  ]
endif

cogl_c_args += cogl_debug_c_args

if have_cogl_tests
  subdir('test-fixtures')
endif
subdir('cogl')
subdir('cogl-path')
subdir('cogl-pango')
if have_gles2
  subdir('cogl-gles2')
endif
if have_cogl_tests
  subdir('tests')
endif
