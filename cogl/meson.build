cogl_includesubdir = pkgname / 'cogl'
cogl_includedir = includedir / cogl_includesubdir
cogl_srcdir = meson.current_source_dir()

cogl_includepath = [mtk_includepath, include_directories('.', 'cogl')]

cogl_pkg_deps = [
  glib_dep,
  gio_dep,
  gobject_dep,
  graphene_dep,
]

cogl_pkg_private_deps = [
  gmodule_no_export_dep,
  libmutter_mtk_dep,
]

if have_profiler
  cogl_pkg_private_deps += [
    libsysprof_capture_dep,
  ]
endif

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

if have_x11_client
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
if buildtype != 'plain'
  if get_option('debug')
    cogl_debug_c_args += [
      '-DCOGL_GL_DEBUG',
      '-DCOGL_OBJECT_DEBUG',
      '-DCOGL_ENABLE_DEBUG',
    ]
  else
    cogl_debug_c_args += [
      '-DG_DISABLE_CAST_CHECKS',
    ]
  endif
endif
cogl_debug_c_args = cc.get_supported_arguments(cogl_debug_c_args)
cogl_c_args += cogl_debug_c_args

subdir('cogl')
subdir('cogl-pango')
