project('mutter', 'c',
  version: '3.29.90',
  meson_version: '>= 0.46.0',
  license: 'GPLv2+'
)

mutter_plugin_api_version = '3'

libmutter_api_version = '3'

# generic version requirements
glib_req = '>= 2.53.2'
gi_req = '>= 0.9.5'
gtk3_req = '>= 3.19.8'
gdk_pixbuf_req = '>= 2.0'
uprof_req = '>= 0.3'
pango_req = '>= 1.2.0'
cairo_req = '>= 1.10.0'
pangocairo_req = '>= 1.20'
gsettings_desktop_schemas_req = '>= 3.21.4'
json_glib_req = '>= 0.12.0'
upower_glib_req = '>= 0.99.0'
xcomposite_req = '>= 0.4'
xkbcommon_req = '>= 0.4.3'
xfixes_req = '>= 3'
xi_req = '>= 1.6.99.1'
xrandr_req = '>= 1.5.0'
libstartup_notification_req = '>= 0.7'
libcanberra_gtk3_req = '>= 0.26'
libwacom_req = '>= 0.13'
atk_req = '>= 2.5.3'

# optional version requirements
udev_req = '>= 232'

# wayland version requirements
wayland_server_req = '>= 1.13.0'
wayland_protocols_req = '>= 1.16'

# native backend version requirements
libinput_req = '>= 1.4'
gbm_req = '>= 10.3'

# screen cast version requirements
libpipewire_req = '>= 0.2.2'

gnome = import('gnome')
pkg = import('pkgconfig')
i18n  = import('i18n')
cc = meson.get_compiler('c')

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
includedir = join_paths(prefix, get_option('includedir'))
sysconfdir = get_option('sysconfdir')

pkgname = '@0@-@1@'.format(meson.project_name(), libmutter_api_version)

pkgdatadir = join_paths(datadir, pkgname)
pkglibdir = join_paths(libdir, pkgname)
pkgincludedir = join_paths(includedir, pkgname)

gettext_package = meson.project_name()
localedir = join_paths(datadir, 'locale')

top_srcdir = meson.current_source_dir()
builddir = meson.current_build_dir()

m_dep = cc.find_library('m', required: true)
gl_dep = dependency('gl')
gles2_dep = dependency('glesv2')
egl_dep = dependency('egl')
x11_dep = dependency('x11')
gtk3_dep = dependency('gtk+-3.0', version: gtk3_req)
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
pango_dep = dependency('pango', version: pango_req)
pangoft2_dep = dependency('pangoft2')
cairo_dep = dependency('cairo', version: cairo_req)
cairo_gobject_dep = dependency('cairo-gobject', version: cairo_req)
pangocairo_dep = dependency('pangocairo', version: pangocairo_req)
gsettings_desktop_schemas_dep = dependency('gsettings-desktop-schemas',
                                           version: gsettings_desktop_schemas_req)
glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-unix-2.0', version: glib_req)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req)
gobject_dep = dependency('gobject-2.0', version: glib_req)
gthread_dep = dependency('gobject-2.0', version: glib_req)
gobject_introspection_dep = dependency('gobject-introspection-1.0')
gmodule_no_export_dep = dependency('gmodule-no-export-2.0', version: glib_req)
gnome_settings_daemon_dep = dependency('gnome-settings-daemon')
json_glib_dep = dependency('json-glib-1.0', version: json_glib_req)
gnome_desktop_dep = dependency('gnome-desktop-3.0')
xcomposite_dep = dependency('xcomposite', version: xcomposite_req)
xcursor_dep = dependency('xcursor')
xdamage_dep = dependency('xdamage')
xext_dep = dependency('xext')
xfixes_dep = dependency('xfixes', version: xfixes_req)
xi_dep = dependency('xi', version: xi_req)
xtst_dep = dependency('xtst')
xkbfile_dep = dependency('xkbfile')
xkeyboard_config_dep = dependency('xkeyboard-config')
xkbcommon_dep = dependency('xkbcommon', version: xkbcommon_req)
xkbcommon_x11_dep = dependency('xkbcommon-x11')
xrender_dep = dependency('xrender')
x11_xcb_dep = dependency('x11-xcb')
xrandr_dep = dependency('xrandr', version: xrandr_req)
xcb_randr_dep = dependency('xcb-randr')
xcb_res_dep = dependency('xcb-res')
xinerama_dep = dependency('xinerama')
ice_dep = dependency('ice')
sm_dep = dependency('sm')
libstartup_notification_dep = dependency('libstartup-notification-1.0',
                                         version: libstartup_notification_req)
gudev_dep = dependency('gudev-1.0', version: udev_req)
libudev_dep = dependency('libudev', version: udev_req)
libcanberra_gtk3_dep = dependency('libcanberra-gtk3', version: libcanberra_gtk3_req)
libwacom_dep = dependency('libwacom', version: libwacom_req)
atk_dep = dependency('atk', version: atk_req)

# For now always require X11 support
have_x11 = true

if get_option('opengl')
  have_gl = true
  gl_libname = get_option('opengl_libname')
else
  have_gl = false
endif

if get_option('gles2')
  have_gles2 = true
  gles2_libname = get_option('gles2_libname')
else
  have_gles2 = false
endif

have_egl = get_option('egl')
have_glx = get_option('glx')
have_egl_xlib = have_egl and have_x11

if get_option('wayland')
  wayland_server_dep = dependency('wayland-server', version: wayland_server_req)
  wayland_protocols_dep = dependency('wayland-protocols',
                                     version: wayland_protocols_req)
  wayland_egl_dep = dependency('wayland-egl')
  have_wayland = true
else
  have_wayland = false
endif

if get_option('native_backend')
  libdrm_dep = dependency('libdrm')
  libinput_dep = dependency('libinput', version: libinput_req)
  libgbm_dep = dependency('gbm', version: gbm_req)

  libsystemd_dep = dependency('libsystemd')
  if libsystemd_dep.found()
    logind_provider_dep = libsystemd_dep
  else
    logind_provider_dep = dependency('libelogind')
  endif

  have_native_backend = true
else
  have_native_backend = false
endif

if get_option('remote_desktop')
  libpipewire_dep = dependency('libpipewire-0.2', version: libpipewire_req)
  have_remote_desktop = true
else
  have_remote_desktop = false
endif

if get_option('egl_device')
  wayland_eglstream_protocols_dep = dependency('wayland-eglstream-protocols')

  have_egl_device = true
else
  have_egl_device = false
endif


have_libgudev = get_option('udev')
have_libwacom = get_option('libwacom')
have_pango_ft2 = get_option('pango_ft2')

if get_option('introspection')
  gobject_introspection_dep = dependency('gobject-introspection-1.0')

  have_introspection = true
else
  have_introspection = false
endif

have_cogl_tests = get_option('cogl_tests')
have_clutter_tests = get_option('clutter_tests')
have_tests = get_option('tests')

required_functions = [
  'ffs',
  'clz',
]
foreach function : required_functions
  if not cc.has_function(function)
    error('Required function ' + function + ' missing')
  endif
endforeach

add_project_arguments('-D_GNU_SOURCE', language: 'c')

debug_c_args = []
buildtype = get_option('buildtype')
if buildtype.startswith('debug')
  debug_c_args += '-DG_ENABLE_DEBUG'
endif
add_project_arguments(debug_c_args, language: 'c')

cc.compiles('void main (void) { __builtin_ffsl (0); __builtin_popcountl (0); }')

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', gettext_package)
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

cdata.set('HAVE_WAYLAND', have_wayland)
cdata.set('HAVE_NATIVE_BACKEND', have_native_backend)
cdata.set('HAVE_REMOTE_DESKTOP', have_remote_desktop)
cdata.set('HAVE_EGL_DEVICE', have_egl_device)
cdata.set('HAVE_LIBGUDEV', have_libgudev)
cdata.set('HAVE_INTROSPECTION', have_introspection)

xkb_base = xkeyboard_config_dep.get_pkgconfig_variable('xkb_base')
cdata.set('XKB_BASE', '"@0@"'.format(xkb_base))

xwayland_path = get_option('xwayland_path')
if xwayland_path == ''
  xwayland_path = find_program('Xwayland').path()
endif
cdata.set('XWAYLAND_PATH', '"@0@"'.format(xwayland_path))

xwayland_grab_default_access_rules = get_option('xwayland_grab_default_access_rules')
cdata.set('XWAYLAND_GRAB_DEFAULT_ACCESS_RULES',
          '"@0@"'.format(xwayland_grab_default_access_rules))

cdata.set('MUTTER_PLUGIN_DIR', '"@0@"'.format(join_paths(pkglibdir, 'plugins')))
cdata.set('MUTTER_LOCALEDIR', '"@0@"'.format(localedir))
cdata.set('MUTTER_LIBEXECDIR', '"@0@"'.format(libexecdir))
cdata.set('MUTTER_PKGDATADIR', '"@0@"'.format(pkgdatadir))

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

subdir('cogl')
subdir('clutter')
subdir('data')
subdir('src')
subdir('po')
subdir('doc/man')
