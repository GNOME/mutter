#tests_srcdir = join_paths(top_srcdir, 'srcs')
#tests_builddir = join_paths(builddir, 'src/tests')
tests_includepath = mutter_includepath
tests_c_args = mutter_c_args

tests_deps = [
  mutter_deps,
  libmutter_cogl_dep,
  libmutter_clutter_dep,
]

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
    xext_dep,
  ],
  install: false,
)

test_runner = executable('mutter-test-runner',
  sources: [
    'test-utils.c',
    'test-utils.h',
    'test-runner.c',
  ],
  include_directories: tests_includepath,
  c_args: tests_c_args,
  link_with: [libmutter],
  dependencies: [tests_deps],
  install: false,
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
  link_with: [libmutter],
  dependencies: [tests_deps],
  install: false,
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
  link_with: [libmutter],
  dependencies: [tests_deps],
  install: false,
)

stacking_tests = files([
  'stacking/basic-x11.metatest',
  'stacking/basic-wayland.metatest',
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
