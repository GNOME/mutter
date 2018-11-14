cogl_test_unit_sources = [
  'test-unit-main.c',
]

cogl_test_unit_includes = [
  cogl_includepath,
  cogl_test_fixtures_includepath,
]

libmutter_cogl_test_unit = executable('test-unit',
  sources: cogl_test_unit_sources,
  c_args: cogl_debug_c_args + [
    '-DCOGL_DISABLE_DEPRECATED',
    '-DCOGL_COMPILATION',
    '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  ],
  include_directories: cogl_test_unit_includes,
  dependencies: [
    cogl_deps,
    libmutter_cogl_dep,
    libmutter_cogl_path_dep,
    libmutter_cogl_test_fixtures_dep,
  ],
  install: false,
)

find_unit_tests = find_program('meson/find-unit-tests.sh')
cogl_unit_unit_tests = custom_target('cogl-tests-unit-unit-tests',
  output: 'unit-tests',
  input: libmutter_cogl,
  command: [find_unit_tests, '@INPUT@', '@OUTPUT@'],
  install: false,
)

test('unit', cogl_run_tests,
  suite: ['cogl'],
  args: [
    cogl_config_env,
    libmutter_cogl_test_unit,
    cogl_unit_unit_tests
  ],
  is_parallel: false,
)
