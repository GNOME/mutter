clutter_tests_micro_bench_c_args = [
  '-DG_DISABLE_SINGLE_INCLUDES',
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
  '-DCLUTTER_DISABLE_DEPRECATION_WARNINGS',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]
clutter_tests_micro_bench_c_args += clutter_debug_c_args

clutter_tests_micro_bench_tests = [
  'test-text',
  'test-picking',
  'test-text-perf',
  'test-random-text',
  'test-cogl-perf',
]

foreach test : clutter_tests_micro_bench_tests
  executable(test,
    sources: [
      '@0@.c'.format(test),
      clutter_test_utils,
    ],
    include_directories: clutter_includes,
    c_args: clutter_tests_micro_bench_c_args,
    dependencies: [
      clutter_deps,
      libmutter_clutter_dep,
      libmutter_dep,
    ],
    install: false,
  )
endforeach
