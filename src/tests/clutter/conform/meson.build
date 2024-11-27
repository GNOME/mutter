clutter_tests_conform_c_args = [
  '-DG_LOG_DOMAIN="Clutter-Conform"',
  '-DCOGL_ENABLE_MUTTER_API',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]
clutter_tests_conform_c_args += clutter_debug_c_args

clutter_tests_conform_link_args = [
  '-Wl,--export-dynamic',
]

clutter_conform_tests_actor_tests = [
  'actor-clone',
  'actor-color-state',
  'actor-destroy',
  'actor-event-hold',
  'actor-graph',
  'actor-invariants',
  'actor-iter',
  'actor-layout',
  'actor-meta',
  'actor-offscreen-redirect',
  'actor-pick',
  'actor-pivot-point',
  'actor-shader-effect',
  'actor-size',
]

clutter_conform_tests_classes_tests = []

if have_fonts
  clutter_conform_tests_actor_tests += [
    'actor-paint-opacity',
  ]

  clutter_conform_tests_classes_tests += [
    'text',
  ]
endif

clutter_conform_tests_general_tests = [
  'binding-pool',
  'event-delivery',
  'color-state-transform',
  'frame-clock',
  'frame-clock-passive',
  'frame-clock-timeline',
  'grab',
  'gesture',
  'gesture-relationship',
  'interval',
  'pipeline-cache',
  'timeline',
  'timeline-interpolate',
  'timeline-progress',
  'timeline-rewind',
]

clutter_conform_tests = []
clutter_conform_tests +=  clutter_conform_tests_actor_tests
clutter_conform_tests += clutter_conform_tests_classes_tests
clutter_conform_tests += clutter_conform_tests_general_tests

test_env = environment()
test_env.set('G_TEST_SRCDIR', mutter_srcdir / 'src' / 'tests')
test_env.set('G_TEST_BUILDDIR', mutter_builddir / 'src' / 'tests')
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('CLUTTER_ENABLE_DIAGNOSTIC', '0')
test_env.set('GSETTINGS_SCHEMA_DIR', locally_compiled_schemas_dir)
test_env.set('TEST_DATADIR', mutter_srcdir / 'src' / 'tests')

foreach test : clutter_conform_tests
  test_executable = executable('@0@'.format(test),
    sources: [
      '@0@.c'.format(test),
      clutter_test_utils,
    ],
    include_directories: clutter_includes,
    c_args: clutter_tests_conform_c_args,
    link_args: clutter_tests_conform_link_args,
    dependencies: [
      libmutter_test_dep,
    ],
    install: false,
  )

  test(test, test_executable,
    suite: ['clutter', 'clutter/conform'],
    env: test_env,
    is_parallel: false,
  )
endforeach
