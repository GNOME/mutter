cogl_unit_tests = [
  'test-bitmask',
]

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('MUTTER_TEST_PLUGIN_PATH', '@0@'.format(default_plugin.full_path()))

foreach unit_test: cogl_unit_tests
  test_name = 'cogl-' + unit_test

  test_executable = executable(test_name,
    sources: [
      unit_test + '.c',
      cogl_test_utils,
    ],
    c_args: [
      '-D__COGL_H_INSIDE__',
      '-DCOGL_ENABLE_MUTTER_API',
      '-DCOGL_ENABLE_EXPERIMENTAL_API',
      '-DCOGL_DISABLE_DEPRECATED',
      '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
    ],
    include_directories: [
      cogl_includepath,
    ],
    dependencies: [
      libmutter_test_dep,
    ],
  )

  test(test_name, test_executable,
    suite: ['cogl', 'cogl/unit'],
    env: test_env,
    is_parallel: false,
  )
endforeach
