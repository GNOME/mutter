project('mutter', 'c',
  version: '49.1',
  meson_version: '>= 1.5.0',
  license: 'GPL-2.0-or-later',
)

split_version = meson.project_version().split('.')

# API version, bump each development cycle
libmutter_api_version = '18'

mutter_srcdir = meson.current_source_dir()
mutter_builddir = meson.current_build_dir()

# generic version requirements
lcms2_req = '>= 2.6'
colord_req = '>= 1.4.5'
fribidi_req = '>= 1.0.0'
glib_req = '>= 2.81.1'
gi_req = '>= 0.9.5'
graphene_req = '>= 1.10.2'
gtk3_req = '>= 3.19.8'
gtk4_req = '>= 4.14.0'
glycin_req = '>= 2.0.beta.2'
pango_req = '>= 1.46.0'
cairo_req = '>= 1.10.0'
pangocairo_req = '>= 1.20'
pixman_req = '>= 0.42'
gsettings_desktop_schemas_req = '>= 47.beta'
x11_req = '>= 1.7.0'
xcomposite_req = '>= 0.4'
xkbcommon_req = '>= 0.4.3'
xfixes_req = '>= 6'
xi_req = '>= 1.7.4'
xrandr_req = '>= 1.5.0'
libstartup_notification_req = '>= 0.7'
libcanberra_req = '>= 0.26'
libwacom_req = '>= 0.13'
atk_req = '>= 2.5.3'
harfbuzz_req = '>= 2.6'
libei_req = '>= 1.3.901'
libdisplay_info_req = '>= 0.2'

# optional version requirements
udev_req = '>= 228'
gudev_req = '>= 238'
umockdev_req = '>= 0.3.0'

# wayland version requirements
wayland_server_req = '>= 1.24'
wayland_protocols_req = '>= 1.45'

# native backend version requirements
libinput_req = '>= 1.27.0'
gbm_req = '>= 21.3'
libdrm_req = '>= 2.4.118'

# screen cast version requirements
libpipewire_req = '>= 1.2.7'

# profiler requirements
sysprof_req = '>= 3.37.2'

gnome = import('gnome')
pkg = import('pkgconfig')
i18n  = import('i18n')
fs = import('fs')
python = import('python')
cc = meson.get_compiler('c')

add_project_link_arguments(
  cc.get_supported_link_arguments(
    # meson automatically adds -rpath to targets and strips them when they
    # are installed. ld adds a RUNPATH tag for -rpath arguments by default.
    # This makes ld add a RPATH tag instead (as it did some time ago).
    # The reason why we want RPATH and not RUNPATH is that LD_LIBRARY_PATH
    # takes precedence over RUNPATH but not over RPATH. Since we usually run
    # development builds in jhbuild which sets up LD_LIBRARY_PATH this can
    # result in wrong dependencies being picked up by the linker.
    '-Wl,--disable-new-dtags',
  ),
  language : 'c',
)

prefix = get_option('prefix')

bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
libdir = prefix / get_option('libdir')
libexecdir = prefix / get_option('libexecdir')
includedir = prefix / get_option('includedir')
sysconfdir = get_option('sysconfdir')
mandir = prefix / get_option('mandir')

pkgname = '@0@-@1@'.format(meson.project_name(), libmutter_api_version)

pkgdatadir = datadir / pkgname
pkglibdir = libdir / pkgname
pkgincludedir = includedir / pkgname

pcdir = libdir / 'pkgconfig'

desktopdir = join_paths(datadir, 'applications')
icondir = join_paths(datadir, 'icons')

po_path = join_paths(meson.current_source_dir(), 'po')

gettext_package = meson.project_name()
localedir = datadir / 'locale'

libmutter_name = 'mutter-' + libmutter_api_version

mutter_installed_tests_datadir = datadir / 'installed-tests' / libmutter_name
mutter_installed_tests_libexecdir = libexecdir / 'installed-tests' / libmutter_name

subproject('gvdb')

m_dep = cc.find_library('m', required: true)
graphene_dep = dependency('graphene-gobject-1.0', version: graphene_req)
glycin_dep = dependency('glycin-2', version: glycin_req)
cairo_dep = dependency('cairo', version: cairo_req)
pixman_dep = dependency('pixman-1', version: pixman_req)
gsettings_desktop_schemas_dep = dependency('gsettings-desktop-schemas',
                                           version: gsettings_desktop_schemas_req)
glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-unix-2.0', version: glib_req)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req)
gobject_dep = dependency('gobject-2.0', version: glib_req)
gthread_dep = dependency('gobject-2.0', version: glib_req)
gtk4_dep = dependency('gtk4', version: gtk4_req, required: false)
libadwaita_dep = dependency('libadwaita-1', required: false)
gmodule_no_export_dep = dependency('gmodule-no-export-2.0', version: glib_req)
gnome_settings_daemon_dep = dependency('gnome-settings-daemon', required: false)
xkbcommon_dep = dependency('xkbcommon', version: xkbcommon_req)
atk_dep = dependency('atk', version: atk_req)
colord_dep = dependency('colord', version: colord_req)
lcms2_dep = dependency('lcms2', version: lcms2_req)
libeis_dep = dependency('libeis-1.0', version: libei_req)
libei_dep = dependency('libei-1.0', version: libei_req)
gvdb_dep = dependency('gvdb')
libdisplay_info_dep = dependency('libdisplay-info', version: libdisplay_info_req)

have_wayland = get_option('wayland')
have_x11 = get_option('x11')
have_xwayland = get_option('xwayland')
have_x11_client = have_x11 or have_xwayland

if have_xwayland and not have_wayland
  error('XWayland support requires Wayland support enabled')
endif

if not have_wayland and not have_x11
  error('A Wayland/X11 backend must be enabled')
endif

have_fonts = get_option('fonts')

if have_fonts
  pango_dep = dependency('pango', version: pango_req)
  pangocairo_dep = dependency('pangocairo', version: pangocairo_req)
  harfbuzz_dep = dependency('harfbuzz', version: harfbuzz_req)
endif

if have_fonts or have_x11
  fribidi_dep = dependency('fribidi', version: fribidi_req)
endif

if have_x11_client
  if not gtk4_dep.found()
    error('X11 client support requires gtk4 @0@'.format(gtk4_req))
  endif

  x11_dep = dependency('x11', version: x11_req)
  xcomposite_dep = dependency('xcomposite', version: xcomposite_req)
  xcursor_dep = dependency('xcursor')
  xdamage_dep = dependency('xdamage')
  xext_dep = dependency('xext')
  xfixes_dep = dependency('xfixes', version: xfixes_req)
  xi_dep = dependency('xi', version: xi_req)
  xkeyboard_config_dep = dependency('xkeyboard-config-2', 'xkeyboard-config')
  x11_xcb_dep = dependency('x11-xcb')
  xrandr_dep = dependency('xrandr', version: xrandr_req)
  xcb_res_dep = dependency('xcb-res')
  xinerama_dep = dependency('xinerama')
  xau_dep = dependency('xau')

  if have_x11
    xcb_randr_dep = dependency('xcb-randr')
    xkbcommon_x11_dep = dependency('xkbcommon-x11')
    xkbfile_dep = dependency('xkbfile')
    xtst_dep = dependency('xtst')
  endif
endif

have_gnome_desktop = get_option('libgnome_desktop')
if have_gnome_desktop
  gnome_desktop_dep = dependency('gnome-desktop-4')
endif

have_sound_player = get_option('sound_player')
if have_sound_player
  libcanberra_dep = dependency('libcanberra', version: libcanberra_req)
endif

have_gl = get_option('opengl')
if have_gl
  gl_dep = dependency('gl')
  gl_libname = get_option('opengl_libname')
endif

have_egl = get_option('egl')
if have_egl
  egl_dep = dependency('egl')
endif

have_glx = get_option('glx') and have_x11
if have_glx
  if not have_gl
    error('GLX support requires OpenGL to be enabled')
  endif
endif

have_egl_xlib = have_egl and have_x11

have_gles2 = get_option('gles2')
if have_gles2
  gles2_dep = dependency('glesv2')
  gles2_libname = get_option('gles2_libname')

  if not have_egl
    error('GLESv2 support requires EGL to be enabled')
  endif
endif

if not have_gl and not have_gles2
  error('Neither GLES2 or OpenGL was enabled')
endif

if have_wayland
  if meson.version().version_compare('>= 1.8.0')
    wl_mod = import('wayland')
  else
    wl_mod = import('unstable-wayland')
  endif

  wayland_server_dep = dependency('wayland-server', version: wayland_server_req)
  wayland_client_dep = dependency('wayland-client', version: wayland_server_req)
  wayland_cursor_dep = dependency('wayland-cursor')
  wayland_protocols_dep = dependency('wayland-protocols',
                                     version: wayland_protocols_req)
  wayland_egl_dep = dependency('wayland-egl')

  if not have_egl
    error('Wayland support requires EGL to be enabled')
  endif
endif

have_logind = get_option('logind')
if have_logind
  logind_dep = dependency('libsystemd', required: false)

  if not logind_dep.found()
    logind_dep = dependency('libelogind', required: false)
  endif

  if not logind_dep.found()
    error('No logind provider dependency found: tried libsystemd and libelogind')
  endif
endif

have_libgudev = get_option('udev')
if have_libgudev
  if not have_logind
    error('The udev support requires logind')
  endif

  libudev_dep = dependency('libudev', version: udev_req)
  gudev_dep = dependency('gudev-1.0', version: gudev_req)
  udev_dep = dependency('udev')

  udev_dir = get_option('udev_dir')
  if udev_dir == ''
    udev_dir = udev_dep.get_variable('udevdir')
  endif
endif

have_native_backend = get_option('native_backend')
if have_native_backend
  libgbm_dep = dependency('gbm', version: gbm_req)
  libinput_dep = dependency('libinput', version: libinput_req)

  if not have_logind
    error('The native backend requires logind to be enabled')
  endif

  if not have_egl
    error('The native backend requires EGL to be enabled')
  endif

  if not have_gles2
    error('The native backend requires GLESv2 to be enabled')
  endif

  if not have_libgudev
    error('The native backend requires udev to be enabled')
  endif
endif

if have_wayland or have_native_backend
  libdrm_dep = dependency('libdrm', version: libdrm_req)
  have_drm_plane_size_hint = cc.has_type('struct drm_plane_size_hint',
                                         dependencies: libdrm_dep,
                                         prefix: '#include <drm_mode.h>')
else
  have_drm_plane_size_hint = false
endif

have_egl_device = get_option('egl_device')

have_wayland_eglstream = get_option('wayland_eglstream')
if have_wayland_eglstream
  wayland_eglstream_protocols_dep = dependency('wayland-eglstream-protocols')
  dl_dep = cc.find_library('dl', required: true)

  if not have_wayland
    error('Wayland EGLStream support requires Wayland to be enabled')
  endif
endif


have_libwacom = get_option('libwacom')
if have_libwacom
  libwacom_dep = dependency('libwacom', version: libwacom_req)
  have_libwacom_get_num_rings = cc.has_function('libwacom_get_num_rings',
    dependencies: libwacom_dep)
else
  have_libwacom_get_num_rings = false
endif

have_startup_notification = get_option('startup_notification') and have_x11_client
if have_startup_notification
  libstartup_notification_dep = dependency('libstartup-notification-1.0',
                                           version: libstartup_notification_req)
endif

have_remote_desktop = get_option('remote_desktop')
if have_remote_desktop
  libpipewire_dep = dependency('libpipewire-0.3', version: libpipewire_req)
endif

have_devkit = get_option('devkit').enable_auto_if(
  gtk4_dep.found() and
  libadwaita_dep.found() and
  have_native_backend and
  have_remote_desktop
).enabled()
if have_devkit and (not gtk4_dep.found() or not libadwaita_dep.found() or not have_native_backend or not have_remote_desktop)
  error('MDK requires gtk4 @0@, libadwaita, the native backend, and remote desktop'.format(gtk4_req))
endif

have_introspection = get_option('introspection')
if have_introspection
  gobject_introspection_dep = dependency('gobject-introspection-1.0', version: gi_req)

  introspection_args = [
    '--quiet',
    '-U_GNU_SOURCE',
  ]

  if gobject_introspection_dep.version().version_compare('>=1.83.2')
    introspection_args += [
     '--doc-format=gi-docgen',
    ]
  endif

  introspection_common = {
    'install_dir_gir': pkglibdir,
    'install_dir_typelib': pkglibdir,
    'install': true,
    'fatal_warnings': get_option('werror'),
  }
endif

# Check for timerfd_create(2)
have_timerfd = cc.links('''
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
int main (int argc, char ** argv) {
  struct itimerspec ts = {{0}};
  int fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  timerfd_settime (fd, TFD_TIMER_ABSTIME, &ts, NULL);
  return 0;
}
''', name : 'timerfd_create(2) system call')

have_documentation = get_option('docs')
have_tests = get_option('tests').enable_auto_if(have_native_backend).enabled()
have_mutter_tests = false
have_cogl_tests = false
have_clutter_tests = false
have_kvm_tests = false
have_tty_tests = false
have_installed_tests = false
have_x11_tests = false
have_bash_completion = get_option('bash_completion')

if have_tests
  gtk3_dep = dependency('gtk+-3.0', version: gtk3_req)
  dbusmock_dep = python.find_installation('python3', modules: ['dbusmock'], required: false)
  libevdev_dep = dependency('libevdev', required: false)
  umockdev_dep = dependency('umockdev-1.0', version: umockdev_req)

  if not have_native_backend
    error('Tests require the native backend to be enabled')
  endif

  have_mutter_tests = get_option('mutter_tests')
  if have_mutter_tests
    if not have_wayland
      error('Tests require Wayland to be enabled')
    endif
    if not have_x11_client
      error('Tests requires an X11 client')
    endif
    if not dbusmock_dep.found()
      error('Tests require python-dbusmock')
    endif
  endif
  have_kvm_tests = get_option('kvm_tests')
  if have_kvm_tests
    if not have_native_backend
      error('KVM tests need the native backend tests')
    endif
    if host_machine.cpu_family() != 'x86_64'
      error('KVM tests are only supported on x86_64')
    endif
  endif
  have_tty_tests = get_option('tty_tests')
  if have_tty_tests
    if not have_native_backend
      error('TTY tests need the native backend tests')
    endif
  endif

  have_cogl_tests = get_option('cogl_tests')
  have_clutter_tests = get_option('clutter_tests')
  have_installed_tests = get_option('installed_tests')

  meta_dbus_runner = find_program('src/tests/meta-dbus-runner.py')
  default_test_wrappers = [
    meta_dbus_runner,
    '--',
  ]

  if get_option('catch')
    catch = find_program('catch')
    default_test_wrappers += [
      catch,
    ]
  endif

  test_setup_env = environment()
  test_setup_env.set('MUTTER_TEST_LOG_DIR', mutter_builddir / 'meson-logs')

  add_test_setup('default',
    is_default: true,
    exe_wrapper: default_test_wrappers,
    env: test_setup_env,
  )

  add_test_setup('plain')

  if have_x11
    xvfb = find_program('xvfb-run', required: false)
    if xvfb.found()
      have_x11_tests = true
    endif
  endif
endif

have_profiler = get_option('profiler')
if have_profiler
  # libsysprof-capture support
  libsysprof_capture_dep = dependency('sysprof-capture-4',
    required: true,
    default_options: [
      'examples=false',
      'gtk=false',
      'tests=false',
      'tools=false',
      'libsysprof=false',
      'sysprofd=none',
      'help=false',
    ],
    fallback: ['sysprof', 'libsysprof_capture_dep'],
    version: sysprof_req,
  )

  if libsysprof_capture_dep.type_name() == 'pkgconfig'
    sysprof_dep = dependency('sysprof-6', 'sysprof-4')
    sysprof_dbus_interfaces_dir = sysprof_dep.get_variable('datadir') / 'dbus-1' / 'interfaces'
  else
    sysprof_dbus_interfaces_dir = mutter_srcdir / 'subprojects' / 'sysprof' / 'src'
  endif
endif

required_functions = [
  'ffs',
  'clz',
  'memmem',
]
foreach function : required_functions
  if not cc.has_function(function)
    error('Required function ' + function + ' missing')
  endif
endforeach

if host_machine.cpu_family() == 'x86'
  add_project_arguments('-ffloat-store', language: 'c')
endif
add_project_arguments('-D_GNU_SOURCE', language: 'c')

buildtype = get_option('buildtype')
if buildtype != 'plain'
  mutter_c_args = [
    '-fno-omit-frame-pointer',
    '-mno-omit-leaf-frame-pointer',
    '-fno-strict-aliasing',
    '-Wpointer-arith',
    '-Wmissing-declarations',
    '-Wformat=2',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wundef',
    '-Wunused',
    '-Wcast-align',
    '-Wmissing-noreturn',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wlogical-op',
    '-Wignored-qualifiers',
    '-Werror=redundant-decls',
    '-Werror=implicit',
    '-Werror=nonnull',
    '-Werror=init-self',
    '-Werror=main',
    '-Werror=missing-braces',
    '-Werror=sequence-point',
    '-Werror=return-type',
    '-Werror=trigraphs',
    '-Werror=array-bounds',
    '-Werror=write-strings',
    '-Werror=address',
    '-Werror=int-to-pointer-cast',
    '-Werror=pointer-to-int-cast',
    '-Werror=empty-body',
    '-Werror=write-strings',
    '-Werror=strict-aliasing',
    '-Wno-sign-compare',
    '-Wno-cast-function-type',
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wno-type-limits',
    '-Wshadow',
    '-Wfloat-conversion',
  ]

  if get_option('debug')
    mutter_c_args += [
      '-DG_ENABLE_DEBUG',
    ]
  else
    mutter_c_args += [
      '-DG_DISABLE_ASSERT',
      '-DG_DISABLE_CAST_CHECKS',
    ]
  endif

  supported_mutter_c_args = cc.get_supported_arguments(mutter_c_args)
  add_project_arguments(supported_mutter_c_args, language: 'c')
endif

cc.compiles('void main (void) { __builtin_ffsl (0); __builtin_popcountl (0); }')

have_eventfd = cc.has_header('sys/eventfd.h')

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', gettext_package)
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_NAME', meson.project_name())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

cdata.set('HAVE_EGL', have_egl)
cdata.set('HAVE_GLX', have_glx)
cdata.set('HAVE_EGL_PLATFORM_XLIB', have_egl_xlib)
cdata.set('HAVE_GL', have_gl)
cdata.set('HAVE_GLES2', have_gles2)
cdata.set('HAVE_WAYLAND', have_wayland)
cdata.set('HAVE_XWAYLAND', have_xwayland)
cdata.set('HAVE_X11', have_x11)
cdata.set('HAVE_X11_CLIENT', have_x11_client)
cdata.set('HAVE_LOGIND', have_logind)
cdata.set('HAVE_NATIVE_BACKEND', have_native_backend)
cdata.set('HAVE_REMOTE_DESKTOP', have_remote_desktop)
cdata.set('HAVE_DEVKIT', have_devkit)
cdata.set('HAVE_GNOME_DESKTOP', have_gnome_desktop)
cdata.set('HAVE_SOUND_PLAYER', have_sound_player)
cdata.set('HAVE_EGL_DEVICE', have_egl_device)
cdata.set('HAVE_WAYLAND_EGLSTREAM', have_wayland_eglstream)
cdata.set('HAVE_LIBGUDEV', have_libgudev)
cdata.set('HAVE_LIBWACOM', have_libwacom)
cdata.set('HAVE_LIBWACOM_GET_NUM_RINGS', have_libwacom_get_num_rings)
cdata.set('HAVE_STARTUP_NOTIFICATION', have_startup_notification)
cdata.set('HAVE_INTROSPECTION', have_introspection)
cdata.set('HAVE_PROFILER', have_profiler)
cdata.set('HAVE_TIMERFD', have_timerfd)
cdata.set('HAVE_EVENTFD', have_eventfd)
cdata.set('HAVE_FONTS', have_fonts)
cdata.set('HAVE_DRM_PLANE_SIZE_HINT', have_drm_plane_size_hint)
cdata.set('HAVE_XKBCOMMON_KANA_COMPOSE_LEDS',
  xkbcommon_dep.version().version_compare('>= 1.8.0'))
cdata.set('HAVE_EIS_EVENT_REF',
  libeis_dep.version().version_compare('>= 1.5.0'))

if have_x11_client
  xkb_base = xkeyboard_config_dep.get_variable('xkb_base')
  cdata.set_quoted('XKB_BASE', xkb_base)
endif

if cc.has_header_symbol('sys/prctl.h', 'prctl')
  cdata.set('HAVE_SYS_PRCTL', 1)
endif

have_xwayland_initfd = false
have_xwayland_listenfd = false
have_xwayland_terminate_delay = false
have_xwayland_byte_swapped_clients = false
have_xwayland_enable_ei_portal = false
if have_xwayland
  xwayland_dep = dependency('xwayland', required: false)

  xwayland_path = get_option('xwayland_path')
  if xwayland_path == ''
    if xwayland_dep.found()
      xwayland_path = xwayland_dep.get_variable('xwayland')
    else
      xwayland_path = find_program('Xwayland').full_path()
    endif
  endif
  cdata.set_quoted('XWAYLAND_PATH', xwayland_path)

  # For Xwayland authority file generation.
  if cc.has_header_symbol('sys/random.h', 'getrandom')
    cdata.set('HAVE_SYS_RANDOM', 1)
  elif cc.has_header_symbol('linux/random.h', 'getrandom')
    cdata.set('HAVE_LINUX_RANDOM', 1)
  else
    error('Required function getrandom not found')
  endif

  # For Xwayland -initfd usage
  use_initfd = get_option('xwayland_initfd')
  if xwayland_dep.found()
    xwayland_supports_initfd = xwayland_dep.get_variable('have_initfd') == 'true'
  else
    xwayland_options = run_command(xwayland_path, '-help', check: false)
    xwayland_supports_initfd = xwayland_options.stderr().contains('-initfd')
  endif

  if use_initfd.auto()
    have_xwayland_initfd = xwayland_supports_initfd
  else
    have_xwayland_initfd = use_initfd.enabled()
    if have_xwayland_initfd and not xwayland_supports_initfd
      error('XWayland -initfd support requested but not available')
    endif
  endif

  if (have_xwayland_initfd)
    cdata.set('HAVE_XWAYLAND_INITFD', 1)
  endif

  # For Xwayland -listenfd usage
  if xwayland_dep.found()
    have_xwayland_listenfd = xwayland_dep.get_variable('have_listenfd') == 'true'
  endif

  if (have_xwayland_listenfd)
    cdata.set('HAVE_XWAYLAND_LISTENFD', 1)
  endif

  # For Xwayland -listenfd usage
  if xwayland_dep.found()
    have_xwayland_terminate_delay = xwayland_dep.get_variable('have_terminate_delay',
                                                              default_value: 'false') == 'true'
  endif

  if (have_xwayland_terminate_delay)
    cdata.set('HAVE_XWAYLAND_TERMINATE_DELAY', 1)
  endif

  # For Xwayland +/-byteswappedclients usage
  if xwayland_dep.found()
    have_xwayland_byte_swapped_clients = xwayland_dep.get_variable('have_byteswappedclients',
                                                                   default_value: 'false') == 'true'
  endif

  if (have_xwayland_byte_swapped_clients)
    cdata.set('HAVE_XWAYLAND_BYTE_SWAPPED_CLIENTS', 1)
  endif

  # For Xwayland -enable-portal usage
  if xwayland_dep.found()
    have_xwayland_enable_ei_portal = xwayland_dep.get_variable('have_enable_ei_portal',
                                                               default_value: 'false') == 'true'
  endif

  if (have_xwayland_enable_ei_portal)
    cdata.set('HAVE_XWAYLAND_ENABLE_EI_PORTAL', 1)
  endif
endif

optional_functions = [
  'mkostemp',
  'posix_fallocate',
  'memfd_create',
]

foreach function : optional_functions
  if cc.has_function(function)
    cdata.set('HAVE_' + function.to_upper(), 1)
  else
    message('Optional function ' + function + ' missing')
  endif
endforeach

xwayland_grab_default_access_rules = get_option('xwayland_grab_default_access_rules')
cdata.set_quoted('XWAYLAND_GRAB_DEFAULT_ACCESS_RULES',
                 xwayland_grab_default_access_rules)

cdata.set_quoted('MUTTER_PLUGIN_DIR', pkglibdir / 'plugins')
cdata.set_quoted('MUTTER_LOCALEDIR', localedir)
cdata.set_quoted('MUTTER_LIBEXECDIR', libexecdir)
cdata.set_quoted('MUTTER_PKGDATADIR', pkgdatadir)

config_h = configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata
)

top_includepath = include_directories('.')

subdir('mtk')
subdir('cogl')
subdir('clutter')
subdir('data')
subdir('tools')
subdir('src')
if have_devkit
  subdir('mdk')
endif
subdir('po')
subdir('doc/man')
if have_documentation
  subdir('doc/reference')
endif

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: have_devkit,
)

meson.add_dist_script('meson/check-version.py', meson.project_version(), 'NEWS')

summary('prefix', prefix, section: 'Directories')
summary('libexecdir', libexecdir, section: 'Directories')
summary('pkgdatadir', pkgdatadir, section: 'Directories')
summary('mandir', mandir, section: 'Directories')

summary('buildtype', get_option('buildtype'), section: 'Build Configuration')
summary('debug', get_option('debug'), section: 'Build Configuration')

summary('Bash completion', have_bash_completion, section: 'Shell integration')

summary('OpenGL', have_gl, section: 'Rendering APIs')
summary('GLES2', have_gles2, section: 'Rendering APIs')
summary('EGL', have_egl, section: 'Rendering APIs')
summary('GLX', have_glx, section: 'Rendering APIs')

summary('Wayland', have_wayland, section: 'Options')
summary('Wayland EGLStream', have_wayland_eglstream, section: 'Options')
summary('X11', have_x11, section: 'Options')
summary('XWayland', have_xwayland, section: 'Options')
summary('Native Backend', have_native_backend, section: 'Options')
summary('EGL Device', have_egl_device, section: 'Options')
summary('Remote desktop', have_remote_desktop, section: 'Options')
summary('Development Kit', have_devkit, section: 'Options')
summary('libgnome-desktop', have_gnome_desktop, section: 'Options')
summary('Sound player', have_sound_player, section: 'Options')
summary('gudev', have_libgudev, section: 'Options')
summary('Wacom', have_libwacom, section: 'Options')
summary('Fonts', have_fonts, section: 'Options')
summary('Startup notification', have_startup_notification, section: 'Options')
summary('Introspection', have_introspection, section: 'Options')
summary('Documentation', have_documentation, section: 'Options')
summary('Profiler', have_profiler, section: 'Options')
summary('Xwayland initfd', have_xwayland_initfd, section: 'Options')
summary('Xwayland listenfd', have_xwayland_listenfd, section: 'Options')
summary('Xwayland terminate delay', have_xwayland_terminate_delay, section: 'Options')
summary('Xwayland byte-swapped clients', have_xwayland_byte_swapped_clients, section: 'Options')
summary('Xwayland enable EI portal', have_xwayland_enable_ei_portal, section: 'Options')

summary('Enabled', have_tests, section: 'Tests')
summary('Mutter tests', have_mutter_tests, section: 'Tests')
summary('Cogl tests', have_cogl_tests, section: 'Tests')
summary('Clutter tests', have_clutter_tests, section: 'Tests')
summary('KVM tests', have_kvm_tests, section: 'Tests')
summary('X11 backend', have_x11_tests, section: 'Tests')
summary('Installed tests', have_installed_tests, section: 'Tests')
summary('Coverage', get_option('b_coverage'), section: 'Tests')
