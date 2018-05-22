cogl_test_unit_sources = [
  'test-unit-main.c',
]

cogl_unit_test_includepath = [include_directories('../../test-fixtures')]

libmutter_cogl_test_unit = executable('test-unit',
  sources: cogl_test_unit_sources,
  c_args: cogl_debug_c_args + [
    '-DCOGL_DISABLE_DEPRECATED',
    '-DCOGL_COMPILATION',
    '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  ],
  include_directories: [cogl_includepath, cogl_unit_test_includepath],
  dependencies: [cogl_deps, libmutter_cogl_test_fixtures_dep],
  link_with: [libmutter_cogl, libmutter_cogl_path],
  install: false,
)

find_unit_tests = find_program('meson/find-unit-tests.sh')
cogl_unit_unit_tests = custom_target('cogl-tests-unit-unit-tests',
  output: 'unit-tests',
  input: libmutter_cogl,
  command: [find_unit_tests, '@INPUT@', '@OUTPUT@'],
  install: false,
)

test('cogl/unit', cogl_run_tests,
  args: [
    cogl_config_env,
    libmutter_cogl_test_unit,
    cogl_unit_unit_tests
  ],
  is_parallel: false,
)
