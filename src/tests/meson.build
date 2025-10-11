mutter_test_sources = [
  'meta-backend-test.c',
  'meta-backend-test.h',
  'meta-context-test.c',
  'meta-crtc-test.c',
  'meta-crtc-test.h',
  'meta-gpu-test.c',
  'meta-gpu-test.h',
  'meta-monitor-manager-test.c',
  'meta-monitor-manager-test.h',
  'meta-monitor-test-utils.c',
  'meta-monitor-test-utils.h',
  'meta-output-test.c',
  'meta-output-test.h',
  'meta-ref-test.c',
  'meta-ref-test.h',
  'meta-ref-test-utils.c',
  'meta-ref-test-utils.h',
  'meta-sensors-proxy-mock.c',
  'meta-sensors-proxy-mock.h',
  'meta-test-monitor.c',
  'meta-test-shell.c',
  'meta-test-shell.h',
  'meta-test-utils.c',
  'meta-test-utils.h',
]

if libevdev_dep.found()
  mutter_test_sources += [
    'meta-input-test-utils.c',
    'meta-input-test-utils.h',
  ]
endif

libmutter_test_name = 'mutter-test-' + libmutter_api_version

cogl_test_utils = files (
  'cogl-test-utils.c',
  'cogl-test-utils.h',
)

clutter_test_utils = files (
  'clutter-test-utils.c',
  'clutter-test-utils.h',
)

tests_includepath = include_directories('.')
tests_includes = mutter_includes
tests_c_args = mutter_c_args

subdir('meta-test')

tests_deps = [
  libmutter_cogl_dep,
  libmutter_clutter_dep,
  libmutter_dep,
  mutter_deps,
  locally_compiled_schemas_dep,
  wayland_client_dep,
  gvdb_dep,
  umockdev_dep,
]

if libevdev_dep.found()
  tests_deps += [
    libevdev_dep,
  ]
endif

libmutter_test = shared_library(libmutter_test_name,
                                mutter_test_sources,
                                gnu_symbol_visibility: 'hidden',
                                include_directories: tests_includes,
                                c_args: [
                                  tests_c_args,
                                  '-DG_LOG_DOMAIN="libmutter-test"',
                                ],
                                dependencies: tests_deps,
                                install_rpath: pkglibdir,
                                install_dir: libdir,
                                install: true,
)

libmutter_test_dep = declare_dependency(
  link_with: libmutter_test,
  include_directories: tests_includes,
  dependencies: tests_deps,
)

if have_introspection
  mutter_test_introspected_sources = []
  foreach source: mutter_test_sources
    if source.endswith('.c')
      mutter_test_introspected_sources += source
    endif
  endforeach

  libmutter_test_gir =  gnome.generate_gir(libmutter_test,
    sources: [
      mutter_test_introspected_sources,
      mutter_test_public_headers,
    ],
    nsversion: libmutter_api_version,
    namespace: 'MetaTest',
    symbol_prefix: 'meta',
    identifier_prefix: 'Meta',
    includes: [
      'GObject-2.0',
      'GDesktopEnums-3.0',
      'xlib-2.0',
      'xfixes-4.0',
      libmutter_mtk_gir[0],
      libmutter_cogl_gir[0],
      libmutter_clutter_gir[0],
      libmutter_gir[0],
    ],
    dependencies: [
      libmutter_test_dep,
      libmutter_dep,
      mutter_deps,
    ],
    extra_args: mutter_c_args + introspection_args,
    kwargs: introspection_common,
  )
endif

pkg.generate(libmutter_test,
  name: 'Meta (test framework)',
  filebase: 'libmutter-test-' + libmutter_api_version,
  description: 'Mutter test framework',
  subdirs: pkgname,
  requires: ['libmutter-' + libmutter_api_version],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
    'tests_datadir=${prefix}/share/mutter-' + libmutter_api_version + '/tests',
  ],
  install_dir: pcdir,
)

gnome.compile_schemas(
  depend_files: 'org.gnome.mutter.test.gschema.xml',
)

if have_cogl_tests
  subdir('cogl')
endif

if have_clutter_tests
  subdir('clutter')
endif

subdir('protocol')
subdir('drm-mock')
subdir('wayland-test-clients')

test_env = environment()

test_env_variables = {
  'G_TEST_SRCDIR': mutter_srcdir / 'src' / 'tests',
  'G_TEST_BUILDDIR': mutter_builddir / 'src' / 'tests',
  'XDG_CONFIG_HOME': mutter_builddir / '.config',
  'MUTTER_TEST_PLUGIN_PATH': '@0@'.format(default_plugin.full_path()),
  'MUTTER_REF_TEST_RESULT_DIR': mutter_builddir / 'meson-logs' / 'tests' / 'ref-tests',
  'MUTTER_GDCTL_TEST_RESULT_DIR': mutter_builddir / 'meson-logs' / 'tests' / 'gdctl',
  'GSETTINGS_SCHEMA_DIR': ':'.join([mutter_builddir / 'src' / 'tests',
                                    locally_compiled_schemas_dir]),
}

foreach name, value: test_env_variables
  test_env.set(name, value)
endforeach

test_client = executable('mutter-test-client',
  sources: ['test-client.c'],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-test-client"',
  ],
  dependencies: [
    gtk3_dep,
    gio_unix_dep,
    x11_dep,
    xext_dep,
    xrandr_dep,
    graphene_dep,
    gsettings_desktop_schemas_dep,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

test_runner = executable('mutter-test-runner',
  sources: [
    'test-runner.c',
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-test-runner"',
  ],
  dependencies: libmutter_test_dep,
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

tests_datadir = pkgdatadir / 'tests'

install_data(
  [
    'mutter_dbusrunner.py',
    'socket-launch.sh',
  ],
  install_dir: tests_datadir,
)
install_subdir('dbusmock-templates',
  install_dir: tests_datadir,
)
install_subdir('gdctl',
  install_dir: tests_datadir,
)

if have_installed_tests
  configure_file(
    input: 'mutter-installed-dbus-session.py.in',
    output: 'mutter-installed-dbus-session.py',
    configuration: {'tests_datadir': tests_datadir},
    install: true,
    install_mode: 'rwxr-xr-x',
    install_dir: mutter_installed_tests_libexecdir,
  )
endif

wayland_test_utils = [
  'meta-wayland-test-driver.c',
  'meta-wayland-test-driver.h',
  'meta-wayland-test-utils.c',
  'meta-wayland-test-utils.h',
  test_driver_server_header,
  test_driver_protocol_code,
]

monitor_tests_common = [
  'monitor-tests-common.c',
  'monitor-tests-common.h',
]

monitor_tests_common_deps = [
  test_client,
  x11_frames,
]

# The test_cases is a list of dictionaries each one representing one test case.
# It consists of the following keys:
#  - 'name' - the test name
#  - 'suite' - the test suite
#  - 'sources' - a list of source code files

test_cases = []
privileged_test_cases = []

subdir('mtk')

backlight_client = executable('monitor-backlight-client',
  sources: [
    'monitor-backlight-client.c',
    built_dbus_sources['meta-dbus-display-config'],
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-monitor-backlight-test-client"',
  ],
  dependencies: [
    gio_dep,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

test_cases += [
  {
    'name': 'unit',
    'suite': 'unit',
    'sources': [
      'unit-tests.c',
      'boxes-tests.c',
      'boxes-tests.h',
      'monitor-store-unit-tests.c',
      'monitor-store-unit-tests.h',
      'monitor-transform-tests.c',
      'monitor-transform-tests.h',
      'orientation-manager-unit-tests.c',
      'hdr-metadata-unit-tests.c',
      'button-transform-tests.c',
    ],
    'depends': [
      test_client,
    ],
  },
  {
    'name': 'monitor-color-tests',
    'suite': 'backend',
    'sources': [
      'monitor-color-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-storage-tests',
    'suite': 'backend',
    'sources': [
      'monitor-storage-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-config-tests',
    'suite': 'backend',
    'sources': [
      'monitor-config-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-orientation-tests',
    'suite': 'backend',
    'sources': [
      'monitor-orientation-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-custom-config-tests',
    'suite': 'backend',
    'sources': [
      'monitor-custom-config-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-scale-tests',
    'suite': 'backend',
    'sources': [
      'monitor-scale-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-config-migration-tests',
    'suite': 'backend',
    'sources': [
      'monitor-config-migration-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-config-policy-tests',
    'suite': 'backend',
    'sources': [
      'monitor-config-policy-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-dbus-tests',
    'suite': 'backend',
    'sources': [
      'monitor-dbus-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-abstraction-tests',
    'suite': 'backend',
    'sources': [
      'monitor-abstraction-tests.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'tiling-hotplug-test',
    'suite': 'backend',
    'sources': [
      'tiling-hotplug-test.c',
      monitor_tests_common,
    ],
    'depends': [
      monitor_tests_common_deps,
    ],
  },
  {
    'name': 'monitor-utils',
    'suite': 'unit',
    'sources': [ 'monitor-util-tests.c', ],
  },
  {
    'name': 'monitor-backlight',
    'suite': 'backend',
    'sources': [ 'monitor-backlight-tests.c', ],
    'depends': [
      backlight_client,
    ],
  },
  {
    'name': 'monitor-gdctl',
    'suite': 'backend',
    'sources': [ 'monitor-gdctl-tests.c', ],
    'args': gdctl.full_path(),
    'installed_args': bindir / 'gdctl',
  },
  {
    'name': 'stage-views',
    'suite': 'compositor',
    'sources': [ 'stage-view-tests.c', ],
    'depends': [
      test_client,
      x11_frames,
    ],
  },
  {
    'name': 'edid',
    'suite': 'unit',
    'sources': [ 'edid-tests.c', ],
  },
  {
    'name': 'color-management',
    'suite': 'unit',
    'sources': [ 'color-management-tests.c', ],
  },
  {
    'name': 'color-management-profile-conflict',
    'suite': 'unit',
    'sources': [ 'color-management-profile-conflict-test.c', ],
  },
  {
    'name': 'keybindings',
    'suite': 'input',
    'sources': [ 'keybindings.c' ],
    'installed_runner_args': [
      '--compile-schemas',
    ],
  },
  {
    'name': 'stage',
    'suite': 'backend',
    'sources': [ 'stage-tests.c', ],
  },
  {
    'name': 'debug-control',
    'suite': 'core',
    'sources': [ 'debug-control-tests.c', ],
  },
  {
    'name': 'window-created',
    'suite': 'core',
    'sources': [ 'window-created-tests.c', ],
  },
]

remote_desktop_utils = [
  'remote-desktop-utils.c',
  'remote-desktop-utils.h',
  '../backends/meta-fd-source.c',
  '../backends/meta-fd-source.h',
  built_dbus_sources['meta-dbus-remote-desktop'],
  built_dbus_sources['meta-dbus-screen-cast'],
]
remote_desktop_utils_deps = [
  gio_dep,
  libei_dep,
  libpipewire_dep,
]

screen_cast_client = executable('mutter-screen-cast-client',
  sources: [
    'screen-cast-client.c',
    remote_desktop_utils,
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-screen-cast-client"',
  ],
  dependencies: [
    remote_desktop_utils_deps,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

screen_cast_client_driver = executable('mutter-screen-cast-client-driver',
  sources: [
    'screen-cast-client-driver.c',
    built_dbus_sources['meta-dbus-remote-desktop'],
    built_dbus_sources['meta-dbus-screen-cast'],
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-screen-cast-client-driver"',
  ],
  dependencies: [
    gio_dep,
    libpipewire_dep,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

input_capture_client = executable('mutter-input-capture-test-client',
  sources: [
    'input-capture-test-client.c',
    '../backends/meta-fd-source.c',
    '../backends/meta-fd-source.h',
    built_dbus_sources['meta-dbus-input-capture'],
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-input-capture-test-client"',
  ],
  dependencies: [
    gio_unix_dep,
    libei_dep,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

reftest_deps = [
  cairo_dep,
  glib_dep,
]

cursor_screen_cast_client = executable('mutter-cursor-tests-screen-cast-client',
  sources: [
    'cursor-tests-screen-cast-client.c',
    'meta-ref-test-utils.c',
    'meta-ref-test-utils.h',
    remote_desktop_utils,
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-cursor-tests-screen-cast-client"',
  ],
  dependencies: [
    remote_desktop_utils_deps,
    reftest_deps,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
  install_rpath: pkglibdir,
)

remote_desktop_tests_client = executable('mutter-remote-desktop-tests-client',
  sources: [
    'remote-desktop-tests-client.c',
    remote_desktop_utils,
  ],
  include_directories: tests_includes,
  c_args: [
    tests_c_args,
    '-DG_LOG_DOMAIN="mutter-remote-desktop-tests-client"',
  ],
  dependencies: [
    remote_desktop_utils_deps,
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

# Native backend tests
test_cases += [
  {
    'name': 'kms-utils',
    'suite': 'backends/native',
    'sources': [ 'kms-utils-unit-tests.c', ],
  },
  {
    'name': 'native-virtual-monitor',
    'suite': 'backends/native',
    'sources': [
      'native-virtual-monitor.c',
    ],
  },
  {
    'name': 'native-screen-cast',
    'suite': 'backends/native',
    'sources': [
      'native-screen-cast.c',
    ],
    'depends': [
      screen_cast_client,
      screen_cast_client_driver,
    ],
  },
  {
    'name': 'bezier',
    'suite': 'backends/native',
    'sources': [
      'native-bezier-tests.c',
    ],
  },
  {
    'name': 'pointer-constraints',
    'suite': 'backends/native',
    'sources': [ 'native-pointer-constraints.c' ],
    'depends': [ test_client ],
  },
  {
    'name': 'ref-test-sanity',
    'suite': 'backends/native',
    'sources': [ 'ref-test-sanity.c' ],
  },
  {
    'name': 'persistent-virtual-monitor',
    'suite': 'backends/native',
    'sources': [ 'native-persistent-virtual-monitor.c' ],
  },
  {
    'name': 'input-capture',
    'suite': 'backends/native',
    'sources': [ 'input-capture-tests.c' ],
  },
  {
    'name': 'thread',
    'suite': 'backends/native',
    'sources': [
      'native-thread.c',
      'meta-thread-test.c',
      'meta-thread-test.h',
      'meta-thread-impl-test.c',
      'meta-thread-impl-test.h',
    ],
  },
  {
    'name': 'cursor-overlay-tests',
    'suite': 'backends/native',
    'sources': [
      'cursor-overlay-tests.c',
      wayland_test_utils,
    ],
  },
  {
    'name': 'cursor-tests',
    'suite': 'backends/native',
    'sources': [
      'cursor-tests.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('cursor-tests-client'),
      cursor_screen_cast_client,
    ]
  },
  {
    'name': 'surface-scale-tests',
    'suite': 'backends/native',
    'sources': [
      'surface-scale-tests.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('cursor-tests-client'),
    ],
  },
  {
    'name': 'remote-desktop-tests',
    'suite': 'backends/native',
    'sources': [
      'remote-desktop-tests.c',
    ],
    'depends': [
      remote_desktop_tests_client,
    ],
  },
  {
    'name': 'keyboard-map-tests',
    'suite': 'backends/native',
    'sources': [
      'keyboard-map-tests.c',
    ],
  }
]

# KMS tests
kms_test_variants = []
kms_mode_atomic_variables = {'MUTTER_DEBUG_FORCE_KMS_MODE': 'atomic'}
kms_mode_simple_variables = {'MUTTER_DEBUG_FORCE_KMS_MODE': 'simple'}
kms_thread_kernel_variables = {'MUTTER_DEBUG_KMS_THREAD_TYPE': 'kernel'}
kms_thread_user_variables = {'MUTTER_DEBUG_KMS_THREAD_TYPE': 'user'}
kms_mode_variants = [
  ['atomic', kms_mode_atomic_variables],
  ['simple', kms_mode_simple_variables],
]
kms_thread_variants = [
  ['kernel-thread', kms_thread_kernel_variables],
  ['user-thread', kms_thread_user_variables],
]

foreach kms_mode_variant: kms_mode_variants
  kms_mode_name = kms_mode_variant[0]
  kms_mode_variables = kms_mode_variant[1]

  foreach kms_thread_variant: kms_thread_variants
    kms_thread_name = kms_thread_variant[0]
    kms_thread_variables = kms_thread_variant[1]
    suffix = '-@0@-@1@'.format(kms_thread_name, kms_mode_name)

    kms_test_env_variables = \
      test_env_variables + \
      kms_mode_variables + \
      kms_thread_variables
    kms_test_variants += [[suffix, kms_test_env_variables]]
  endforeach
endforeach

kms_test_cases = [
  {
    'name': 'kms-force-atomic-sanity',
    'suite': 'backends/native/kms',
    'sources': [
      'native-kms-force-atomic-sanity.c',
    ],
    'variants': [['', test_env_variables + kms_mode_atomic_variables]],
  },
  {
    'name': 'kms-force-simple-sanity',
    'suite': 'backends/native/kms',
    'sources': [
      'native-kms-force-simple-sanity.c',
    ],
    'variants': [['', test_env_variables + kms_mode_simple_variables]],
  },
  {
    'name': 'kms-force-kernel-thread-sanity',
    'suite': 'backends/native/kms',
    'sources': [
      'native-kms-force-kernel-thread-sanity.c',
    ],
    'variants': [['', test_env_variables + kms_thread_kernel_variables]],
  },
  {
    'name': 'kms-force-user-thread-sanity',
    'suite': 'backends/native/kms',
    'sources': [
      'native-kms-force-user-thread-sanity.c',
    ],
    'variants': [['', test_env_variables + kms_thread_user_variables]],
  },
  {
    'name': 'kms',
    'suite': 'backends/native/kms',
    'sources': [
      #'meta-kms-test-utils.c',
      #'meta-kms-test-utils.h',
      'native-kms.c',
    ],
    'variants': [['', test_env_variables ]],
  },
  {
    'name': 'kms-render',
    'suite': 'backends/native/kms',
    'sources': [
      'native-kms-render.c',
      wayland_test_utils,
    ],
    'variants': kms_test_variants,
  },
  {
    'name': 'kms-device',
    'suite': 'backends/native/kms',
    'sources': [
      'meta-kms-test-utils.c',
      'meta-kms-test-utils.h',
      'native-kms-device.c',
    ],
    'variants': kms_test_variants,
  },
  {
    'name': 'kms-update',
    'suite': 'backends/native/kms',
    'sources': [
      'meta-kms-test-utils.c',
      'meta-kms-test-utils.h',
      'native-kms-updates.c',
    ],
    'variants': kms_test_variants,
  },
  {
    'name': 'kms-headless-start',
    'suite': 'backend/native/kms',
    'sources': [
      'meta-kms-test-utils.c',
      'meta-kms-test-utils.h',
      'native-kms-headless-start.c',
    ],
  },
  {
    'name': 'kms-hotplug',
    'suite': 'backends/native/kms',
    'sources': [
      'meta-kms-test-utils.c',
      'meta-kms-test-utils.h',
      'native-kms-hotplug.c',
    ],
    'variants': kms_test_variants,
  },
  {
    'name': 'kms-cursor-force-disable',
    'suite': 'backends/native',
    'sources': [
      'native-kms-cursor-force-disable.c',
      wayland_test_utils,
    ],
  },
  {
    'name': 'kms-cursor-hotplug',
    'suite': 'backends/native',
    'sources': [
      'native-kms-cursor-hotplug.c',
      wayland_test_utils,
    ],
  },
  {
    'name': 'drm-lease-monitor',
    'suite': 'backends/native',
    'sources': [
      'drm-lease-monitor-test.c',
    ],
  },
]

privileged_test_cases += kms_test_cases

if libevdev_dep.found()
  privileged_test_cases += [
    {
      'name': 'a11y-mouse-keys-test',
      'suite': 'a11y',
      'sources': [
        'a11y-mouse-keys-test.c',
      ],
    },
  ]
endif

# Wayland tests
wayland_test_cases = [
  {
    'name': 'wayland-client-tests',
    'suite': 'wayland',
    'sources': [
      'wayland-client-tests.c',
    ],
  },
  {
    'name': 'wayland-color-management',
    'suite': 'wayland',
    'sources': [
      'wayland-color-management-test.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('color-management'),
    ],
  },
  {
    'name': 'wayland-drm-lease',
    'suite': 'wayland',
    'sources': [
      'wayland-drm-lease-tests.c',
      wayland_test_utils,
    ],
    'variants': [['', test_env_variables + {'MUTTER_DEBUG_LEASE_CONNECTORS': 'Virtual-1'}]],
    'depends': [
      test_client_executables.get('drm-lease'),
    ],
  },
  {
    'name': 'wayland-keyboard',
    'suite': 'wayland',
    'sources': [
      'wayland-keyboard-tests.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('keyboard'),
    ],
  },
  {
    'name': 'wayland-fullscreen',
    'suite': 'wayland',
    'sources': [
      'wayland-fullscreen-test.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('fullscreen'),
    ],
  },
  {
    'name': 'wayland-fractional-scale',
    'suite': 'wayland',
    'sources': [
      'wayland-fractional-scale-test.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('fractional-scale'),
    ],
  },
  {
    'name': 'window-config',
    'suite': 'wayland',
    'sources': [
      'window-config-tests.c',
      wayland_test_utils,
    ],
  },
  {
    'name': 'viewport-tests',
    'suite': 'wayland',
    'sources': [
      'viewport-tests.c',
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('buffer-less-viewport-client'),
    ],
  },
  {
    'name': 'wayland-unit',
    'suite': 'wayland',
    'timeout': 300,
    'sources': [
      'wayland-unit-tests.c',
      dummy_client_header,
      dummy_server_header,
      dummy_protocol_code,
      wayland_test_utils,
    ],
    'depends': [
      test_client,
      test_client_executables.get('buffer-transform'),
      test_client_executables.get('cursor-shape'),
      test_client_executables.get('dma-buf-scanout'),
      test_client_executables.get('fractional-scale'),
      test_client_executables.get('fullscreen'),
      test_client_executables.get('idle-inhibit'),
      test_client_executables.get('invalid-size-limits-on-map-client'),
      test_client_executables.get('invalid-subsurfaces'),
      test_client_executables.get('invalid-xdg-shell-actions'),
      test_client_executables.get('invalid-geometry'),
      test_client_executables.get('kms-cursor-hotplug-helper'),
      test_client_executables.get('service-client'),
      test_client_executables.get('shm-destroy-before-release'),
      test_client_executables.get('single-pixel-buffer'),
      test_client_executables.get('subsurface-corner-cases'),
      test_client_executables.get('subsurface-parent-unmapped'),
      test_client_executables.get('subsurface-remap-toplevel'),
      test_client_executables.get('subsurface-reparenting'),
      test_client_executables.get('xdg-activation'),
      test_client_executables.get('xdg-activation-before-mapped'),
      test_client_executables.get('xdg-apply-limits'),
      test_client_executables.get('xdg-foreign'),
      test_client_executables.get('xdg-toplevel-bounds'),
      test_client_executables.get('xdg-session-management'),
      test_client_executables.get('xdg-session-management-replace'),
      test_client_executables.get('xdg-toplevel-tag'),
      test_client_executables.get('ycbcr'),
    ],
  },
]

test_cases += wayland_test_cases
privileged_test_cases += wayland_test_cases

if have_xwayland
  x11_compositor_checker = executable('x11-compositor-checker',
    sources: ['x11-compositor-checker.c'],
    include_directories: tests_includes,
    c_args: [
      tests_c_args,
    ],
    dependencies: [
      x11_dep,
    ],
    install: have_installed_tests,
    install_dir: mutter_installed_tests_libexecdir,
    install_rpath: pkglibdir,
  )

  test_cases += [
    {
      'name': 'wayland-x11-interop',
      'suite': 'wayland',
      'sources': [
        'wayland-x11-interop-tests.c',
        wayland_test_client_utils,
        wayland_test_utils,
        built_dbus_sources['meta-dbus-service-channel'],
      ],
      'depends': [
        test_client,
        x11_frames,
      ],
    },
    {
      'name': 'xwayland',
      'suite': 'wayland',
      'depends': [
        test_client,
        x11_compositor_checker,
        x11_frames,
      ],
      'sources': [
        'xwayland-tests.c',
      ],
    },
  ]
endif

# Miscellaneous tests
test_cases += [
  {
    'name': 'service-channel',
    'suite': 'wayland',
    'sources': [
      'service-channel-tests.c',
      wayland_test_client_utils,
      wayland_test_utils,
    ],
    'depends': [
      test_client_executables.get('service-client'),
    ],
  },
]

foreach test_case: test_cases
  test_executable = executable('mutter-' + test_case['name'],
    sources: test_case['sources'],
    include_directories: tests_includes,
    c_args: [
      tests_c_args,
      '-DG_LOG_DOMAIN="mutter-@0@-test"'.format(test_case['name']),
    ],
    dependencies: libmutter_test_dep,
    install: have_installed_tests,
    install_dir: mutter_installed_tests_libexecdir,
    install_rpath: pkglibdir,
  )

  test_depends = [ default_plugin ] + test_case.get('depends', [])

  args = test_case.get('args', [])

  message('test @0@ args: @1@'.format(test_case['name'], args))
  test(test_case['name'], test_executable,
    suite: ['core', 'mutter/' + test_case['suite']],
    args: args,
    env: test_env,
    depends: test_depends,
    is_parallel: false,
    timeout: test_case.get('timeout', 60),
  )
endforeach

stacking_tests = [
  'basic-x11',
  'basic-wayland',
  'client-side-decorated',
  'closed-transient',
  'closed-transient-no-default-focus',
  'closed-transient-no-input-no-take-focus-parent',
  'closed-transient-no-input-no-take-focus-parents',
  'closed-transient-no-input-parent',
  'closed-transient-no-input-parent-delayed-focus-default-cancelled',
  'closed-transient-no-input-parents',
  'closed-transient-no-input-parents-queued-default-focus-destroyed',
  'closed-transient-only-take-focus-parents',
  'minimized',
  'unminimize',
  'mixed-windows',
  'set-parent',
  'override-redirect',
  'set-override-redirect-parent',
  'set-parent-exported',
  'restore-size',
  'unmaximize-new-size',
  'fullscreen-maximize',
  'unfullscreen-strut-change',
  'strut-changes',
  'restore-position',
  'default-size',
  'modals',
  'map-fixed-size',
  'client-resize-respect-constraints',
  'client-resize-focus-change',
  'map-on-hotplug',
  'workspace-basic',
  'workspace-test',
  'workspace-maximized',
  'always-on-top',
  'always-on-top-map-new-maximized',
  'always-on-top-map-new-partial',
  'focus-default-window-globally-active-input',
  'workspace-unmanaging-window',
  'click-to-focus-and-raise',
  'workspace-only-on-primary-focus',
  'overview-focus',
  'suspend-state-timeout',
  'cloned-windows',
  'sloppy-focus',
  'sloppy-focus-pointer-rest',
  'sloppy-focus-auto-raise',
  'sticky',
  'sticky-modals',
  'sticky-transients',
  'strut-monitor-changes',
  'window-placement',
  'x11-move',
  'map-maximized',
  'map-fullscreen',
  'fullscreen-on-monitor',
  'tiling',
  'interactive-move',
  'map-on-monitor',
  'popups',
  'popup-positions',
  'popup-focus',
  'auto-maximize',
  'auto-maximize-dialog',
  'drag-restore-maximized',
  'drag-maximized-to-monitor',
  'drag-then-unmaximize',
  'drag-cancel',
  'drag-restore-initial',
  'monitors-changed-position',
  'multi-monitor-placement',
  'unmaximize-placement',
  'resize-after-unmaximize',
  'maximize-unmaximize-race',
  'partially-maximized',
  'move-to-monitor',
  'keyboard-resize',
]

foreach stacking_test: stacking_tests
  test(stacking_test, test_runner,
    suite: ['core', 'mutter/stacking'],
    env: test_env,
    args: [
      files('stacking' / stacking_test + '.metatest'),
    ],
    is_parallel: false,
    timeout: 60,
    should_fail: stacking_test.startswith('fixme-'),
  )
endforeach

if have_kvm_tests or have_tty_tests
  privileged_tests = []
  foreach test_case: privileged_test_cases
    if test_case.has_key('variants')
      variants = test_case['variants']
    else
      variants = [['', test_env_variables, []]]
    endif

    test_executable = executable('mutter-privileged-' + test_case['name'],
      sources: test_case['sources'],
      include_directories: tests_includes,
      c_args: [
        tests_c_args,
        '-DG_LOG_DOMAIN="mutter-@0@-test"'.format(test_case['name']),
        '-DMUTTER_PRIVILEGED_TEST=1',
      ],
      dependencies: [
        libmutter_test_dep,
        drm_mock_dep
      ],
      install: have_installed_tests,
      install_dir: mutter_installed_tests_libexecdir,
      install_rpath: pkglibdir,
    )

    foreach variant: variants
      variables = variant[1] + {
        'META_DBUS_RUNNER_DISABLE_UMOCKDEV': '1',
        'META_DBUS_RUNNER_DISABLE_LOGIND_PASSTHROUGH': '1',
      }

      variant_env = environment()
      foreach name, value: variables
        variant_env.set(name, value)
      endforeach
      privileged_tests += [
        {
          'name': '@0@@1@'.format(test_case['name'], variant[0]),
          'suite': test_case['suite'],
          'executable': test_executable,
          'variables': variables,
          'env': variant_env,
        },
      ]
    endforeach
  endforeach

  if have_kvm_tests
    subdir('kvm')
  endif

  if have_tty_tests
    foreach test_case: privileged_tests
      test('tty-' + test_case['name'], test_case['executable'],
        suite: ['core', 'mutter/tty', 'mutter/tty/' + test_case['suite']],
        env: test_case['env'],
        is_parallel: false,
        timeout: 60,
      )
    endforeach
  endif
endif

if have_x11_tests
  xvfb_args = [
    '-a',
    '-s',
    '+iglx -noreset',
  ]

  zenity = find_program('zenity', required: true)

  foreach mode: ['', 'sync']
    x11_env = test_env
    x11_env.set('ZENITY', zenity.full_path())
    if mode == 'sync'
      x11_env.set('MUTTER_SYNC', '1')
    endif
    test('x11' + (mode != '' ? '-' + mode : ''), xvfb,
      args: [
        xvfb_args,
        find_program('x11-test.sh').full_path(),
        mutter.full_path(),
      ],
      depends: [
        mutter,
        default_plugin,
        x11_frames,
      ],
      env: x11_env,
      suite: ['core', 'mutter/x11'],
      is_parallel: false,
      timeout: 60,
    )
  endforeach
endif

tools_test_env = test_env
tools_test_env.set('G_DEBUG', 'fatal-warnings')

test('get-state-tool-gio', mutter,
  args: [
    '--headless',
    '--mutter-plugin=@0@'.format(default_plugin.full_path()),
    '--virtual-monitor', '800x600',
    '--wayland-display', 'get-state-gio',
    get_state_tool.full_path(),
  ],
  depends: [
    default_plugin,
  ],
  suite: ['tools'],
  env: tools_test_env,
  is_parallel: false,
  timeout: 60,
)

test('get-state-tool-gdbus', mutter,
  args: [
    '--headless',
    '--mutter-plugin=@0@'.format(default_plugin.full_path()),
    '--virtual-monitor', '800x600',
    '--wayland-display', 'get-state-gdbus',
    '--',
    get_state_tool.full_path(),
    '--gdbus',
  ],
  depends: [
    default_plugin,
  ],
  suite: ['tools'],
  env: tools_test_env,
  is_parallel: false,
  timeout: 60,
)

# Installed tests

if have_installed_tests
  install_subdir('stacking', install_dir: tests_datadir)

  foreach stacking_test: stacking_tests
    stacking_installed_tests_cdata = configuration_data()
    stacking_installed_tests_cdata.set('apiversion', libmutter_api_version)
    stacking_installed_tests_cdata.set('libexecdir', libexecdir)
    stacking_installed_tests_cdata.set('pkgdatadir', pkgdatadir)
    stacking_installed_tests_cdata.set('tests_datadir', tests_datadir)
    stacking_installed_tests_cdata.set('stacking_test', stacking_test)

    configure_file(
      input: 'stacking.test.in',
      output: stacking_test + '.test',
      configuration: stacking_installed_tests_cdata,
      install: true,
      install_dir: mutter_installed_tests_datadir,
    )
  endforeach

  install_subdir('monitor-configs', install_dir: tests_datadir)
  install_subdir('migration', install_dir: tests_datadir)
  install_subdir('ref-tests', install_dir: tests_datadir)
  install_subdir('icc-profiles', install_dir: tests_datadir)
  install_subdir('xcursors', install_dir: tests_datadir)

  install_data('org.gnome.mutter.test.gschema.xml', install_dir: tests_datadir)

  foreach test_case: test_cases
    if test_case.has_key('installed_runner_args')
      runner_args = test_case['installed_runner_args']
    else
      runner_args = []
    endif

    if test_case.has_key('launch')
      foreach service: test_case['launch']
        runner_args += '--launch=@0@'.format(service)
      endforeach
    endif

    if test_case.has_key('installed_args')
      args = test_case.get('installed_args', [])
    else
      args = test_case.get('args', [])
    endif

    installed_tests_cdata = configuration_data()
    installed_tests_cdata.set('apiversion', libmutter_api_version)
    installed_tests_cdata.set('libexecdir', libexecdir)
    installed_tests_cdata.set('pkgdatadir', pkgdatadir)
    installed_tests_cdata.set('reftestresultdir', '/tmp/mutter-ref-test-results')
    installed_tests_cdata.set('gdctltestresultdir', '/tmp/mutter-gdctl-test-results')
    installed_tests_cdata.set('testname', test_case['name'])
    installed_tests_cdata.set('testexecutable', 'mutter-' + test_case['name'])
    installed_tests_cdata.set('testargs', ' '.join(args))
    installed_tests_cdata.set('runnerargs', ' '.join(runner_args))

    configure_file(
      input: 'generic.test.in',
      output: test_case['name'] + '.test',
      configuration: installed_tests_cdata,
      install: true,
      install_dir: mutter_installed_tests_datadir,
    )
  endforeach

endif
