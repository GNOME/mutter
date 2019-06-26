tests_includepath = mutter_includes
tests_c_args = mutter_c_args

tests_deps = [
  mutter_deps,
  libmutter_dep,
  libmutter_cogl_dep,
  libmutter_clutter_dep,
]

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
test_env.set('G_TEST_SRCDIR', join_paths(top_srcdir, 'src'))
test_env.set('G_TEST_BUILDDIR', builddir)
test_env.set('MUTTER_TEST_PLUGIN_PATH', '@0@'.format(default_plugin.full_path()))

test_client = executable('mutter-test-client',
  sources: ['test-client.c'],
  include_directories: tests_includepath,
  c_args: tests_c_args,
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
    'test-utils.c',
    'test-utils.h',
    'test-runner.c',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

unit_tests = executable('mutter-test-unit-tests',
  sources: [
    'test-utils.c',
    'test-utils.h',
    'unit-tests.c',
    'boxes-tests.c',
    'boxes-tests.h',
    'meta-backend-test.c',
    'meta-backend-test.h',
    'meta-monitor-manager-test.c',
    'meta-monitor-manager-test.h',
    'monitor-config-migration-unit-tests.c',
    'monitor-config-migration-unit-tests.h',
    'monitor-store-unit-tests.c',
    'monitor-store-unit-tests.h',
    'monitor-test-utils.c',
    'monitor-test-utils.h',
    'monitor-unit-tests.c',
    'monitor-unit-tests.h',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

headless_start_test = executable('mutter-headless-start-test',
  sources: [
    'headless-start-test.c',
    'meta-backend-test.c',
    'meta-backend-test.h',
    'meta-monitor-manager-test.c',
    'meta-monitor-manager-test.h',
    'test-utils.c',
    'test-utils.h',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  dependencies: [tests_deps],
  install: have_installed_tests,
  install_dir: mutter_installed_tests_libexecdir,
)

stacking_tests = files([
  'stacking/basic-x11.metatest',
  'stacking/basic-wayland.metatest',
  'stacking/closed-transient-no-input-no-take-focus-parent.metatest',
  'stacking/closed-transient-no-input-no-take-focus-parents.metatest',
  'stacking/closed-transient-no-input-parent.metatest',
  'stacking/closed-transient-no-input-parent-delayed-focus-default-cancelled.metatest',
  'stacking/minimized.metatest',
  'stacking/mixed-windows.metatest',
  'stacking/set-parent.metatest',
  'stacking/override-redirect.metatest',
])

test('mutter/stacking', test_runner,
  env: test_env,
  args: [
    stacking_tests,
  ],
  is_parallel: false,
  timeout: 60,
)

test('mutter/unit', unit_tests,
  env: test_env,
  is_parallel: false,
  timeout: 60,
)

test('mutter/unit/headless-start', headless_start_test,
  env: test_env,
  is_parallel: false,
  timeout: 60,
)
