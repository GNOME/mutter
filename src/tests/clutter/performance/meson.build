clutter_tests_performance_c_args = [
  '-DTESTS_DATA_DIR="@0@"'.format(clutter_tests_interactive_srcdir),
  '-DG_DISABLE_SINGLE_INCLUDES',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

clutter_tests_performance_c_args += clutter_debug_c_args

clutter_tests_performance_tests = [
  'test-picking',
  'test-text-perf',
]

foreach test : clutter_tests_performance_tests
  executable(test,
    sources: [
      '@0@.c'.format(test),
      'test-common.h',
      clutter_test_utils,
    ],
    include_directories: [
      clutter_includes,
      clutter_tests_includes,
    ],
    c_args: clutter_tests_performance_c_args,
    dependencies: [
      libmutter_test_dep,
    ],
    install: false,
  )
endforeach
