mutter_test_sources = [
  'meta-backend-test.c',
  'meta-backend-test.h',
  'meta-context-test.c',
  'meta-gpu-test.c',
  'meta-gpu-test.h',
  'meta-monitor-manager-test.c',
  'meta-monitor-manager-test.h',
  'meta-monitor-test-utils.c',
  'meta-monitor-test-utils.h',
  'meta-ref-test.c',
  'meta-ref-test.h',
  'meta-sensors-proxy-mock.c',
  'meta-sensors-proxy-mock.h',
  'meta-test-utils.c',
  'meta-test-utils.h',
]

libmutter_test_name = 'mutter-test-' + libmutter_api_version

clutter_test_utils = files (
  'clutter-test-utils.c',
  'clutter-test-utils.h',
)

tests_includepath = include_directories('.')
tests_includes = mutter_includes
tests_c_args = mutter_c_args

tests_deps = [
  mutter_deps,
  libmutter_dep,
  libmutter_cogl_dep,
  libmutter_clutter_dep,
]

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

pkg.generate(libmutter_test,
  name: 'Meta (test framework)',
  filebase: 'libmutter-test-' + libmutter_api_version,
  description: 'Mutter test framework',
  subdirs: pkgname,
  requires: ['libmutter-' + libmutter_api_version],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
  install_dir: pcdir,
)

if have_clutter_tests
  subdir('clutter')
endif

subdir('meta-test')
subdir('wayland-test-clients')

if have_installed_tests
  stacking_files_datadir = join_paths(pkgdatadir, 'tests')

  installed_tests_cdata = configuration_data()
  installed_tests_cdata.set('libexecdir', libexecdir)
  installed_tests_cdata.set('apiversion', libmutter_api_version)

  configure_file(
    input: 'mutter-all.test.in',
    output: 'mutter-all.test',
    configuration: installed_tests_cdata,
    install: true,
    install_dir: mutter_installed_tests_datadir,
  )

  install_subdir('stacking', install_dir: stacking_files_datadir)
endif

test_env = environment()

test_env_variables = {
  'G_TEST_SRCDIR': join_paths(mutter_srcdir, 'src'),
  'G_TEST_BUILDDIR': mutter_builddir,
  'MUTTER_TEST_PLUGIN_PATH': '@0@'.format(default_plugin.full_path()),
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
  ],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
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
)

meta_dbus_runner = find_program('meta-dbus-runner.py')

if have_installed_tests
  install_data('meta-dbus-runner.py',
    install_dir: mutter_installed_tests_libexecdir,
  )
  install_subdir('dbusmock-templates',
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

# The test_cases is a list of dictionaries each one representing one test case.
# It consists of the following keys:
#  - 'name' - the test name
#  - 'suite' - the test suite
#  - 'sources' - a list of source code files

test_cases = []
privileged_test_cases = []

test_cases += [
  {
    'name': 'unit',
    'suite': 'unit',
    'sources': [
      'unit-tests.c',
      'boxes-tests.c',
      'boxes-tests.h',
      'monitor-config-migration-unit-tests.c',
      'monitor-config-migration-unit-tests.h',
      'monitor-store-unit-tests.c',
      'monitor-store-unit-tests.h',
      'monitor-transform-tests.c',
      'monitor-transform-tests.h',
      'orientation-manager-unit-tests.c',
    ],
  },
  {
    'name': 'monitor-unit',
    'suite': 'backend',
    'sources': [ 'monitor-unit-tests.c', ],
  },
  {
    'name': 'headless-start',
    'suite': 'backend',
    'sources': [ 'headless-start-test.c', ],
  },
  {
    'name': 'stage-views',
    'suite': 'compositor',
    'sources': [ 'stage-view-tests.c', ],
  },
  {
    'name': 'anonymous-file',
    'suite': 'unit',
    'sources': [ 'anonymous-file.c', ],
  },
]

if have_native_tests
  screen_cast_client = executable('mutter-screen-cast-client',
    sources: [
      'screen-cast-client.c',
      dbus_remote_desktop_built_sources,
      dbus_screen_cast_built_sources,
    ],
    include_directories: tests_includes,
    c_args: [
      tests_c_args,
      '-DG_LOG_DOMAIN="mutter-screen-cast-client"',
    ],
    dependencies: [
      gio_dep,
      libpipewire_dep,
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
      'name': 'native-unit',
      'suite': 'backends/native',
      'sources': [
        'native-headless.c',
        'native-screen-cast.c',
        'native-screen-cast.h',
        'native-virtual-monitor.c',
        'native-virtual-monitor.h',
      ],
    },
    {
      'name': 'pointer-constraints',
      'suite': 'backends/native',
      'sources': [ 'native-pointer-constraints.c' ],
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
  ]

  # Privileged tests
  privileged_test_cases += [
    {
      'name': 'kms-render',
      'suite': 'backends/native/kms',
      'sources': [ 'native-kms-render.c' ],
    },
    {
      'name': 'kms-device',
      'suite': 'backends/native/kms',
      'sources': [
        'meta-kms-test-utils.c',
        'meta-kms-test-utils.h',
        'native-kms-device.c',
      ],
    },
    {
      'name': 'kms-update',
      'suite': 'backends/native/kms',
      'sources': [
        'meta-kms-test-utils.c',
        'meta-kms-test-utils.h',
        'native-kms-updates.c',
      ],
    },
  ]

  # Wayland tests
  test_cases += [
    {
      'name': 'wayland-unit',
      'suite': 'wayland',
      'sources': [
        'wayland-unit-tests.c',
        wayland_test_utils,
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
    )

    test(test_case['name'], test_executable,
      suite: ['core', 'mutter/' + test_case['suite']],
      env: test_env,
      is_parallel: false,
      timeout: 60,
    )
  endforeach
endif

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
  'mixed-windows',
  'set-parent',
  'override-redirect',
  'set-override-redirect-parent',
  'set-parent-exported',
  'restore-size',
  'unmaximize-new-size',
  'fullscreen-maximize',
  'unfullscreen-strut-change',
  'restore-position',
  'default-size',
  'modals',
  'map-fixed-size',
  'client-resize-respect-constraints',
]

foreach stacking_test: stacking_tests
  test(stacking_test, test_runner,
    suite: ['core', 'mutter/stacking'],
    env: test_env,
    args: [
      files(join_paths('stacking', stacking_test + '.metatest')),
    ],
    is_parallel: false,
    timeout: 60,
  )
endforeach

if have_kvm_tests or have_tty_tests
  privileged_tests = []
  foreach test_case: privileged_test_cases
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
    )
    privileged_tests += [
      {
        'name': test_case['name'],
        'suite': test_case['suite'],
        'executable': test_executable,
      },
    ]
  endforeach

  if have_kvm_tests
    subdir('kvm')
  endif

  if have_tty_tests
    foreach test_case: privileged_tests
      test('tty-' + test_case['name'], test_case['executable'],
        suite: ['core', 'mutter/tty', 'mutter/tty/' + test_case['suite']],
        env: test_env,
        is_parallel: false,
        timeout: 60,
      )
    endforeach
  endif
endif
