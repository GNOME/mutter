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
    libmutter_cogl_dep,
    libmutter_cogl_path_dep,
    libmutter_cogl_test_fixtures_dep,
  ],
  install: false,
)

find_unit_tests = find_program('meson/find-unit-tests.sh')
cogl_unit_tests = run_command(
  find_unit_tests, libmutter_cogl.full_path(), '/dev/stdout',
  check: true,
).stdout().strip().split('\n')

foreach test_target: cogl_unit_tests
  name_parts = []
  foreach part: test_target.split('_')
    if part != 'unit' and part != 'test'
      name_parts += [part]
    endif
  endforeach

  test_name = '-'.join(name_parts)
  test(test_name, cogl_run_tests,
    suite: ['cogl', 'cogl/unit'],
    args: [
      cogl_config_env,
      libmutter_cogl_test_unit,
      test_target
    ],
    is_parallel: false,
  )
endforeach
