clutter_tests_conform_srcdir = join_paths(clutter_srcdir, 'tests/conform')
clutter_tests_conform_builddir = join_paths(clutter_builddir, 'tests/conform')

clutter_tests_conform_c_args = [
  '-DG_LOG_DOMAIN="Clutter-Conform"',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
]
clutter_tests_conform_c_args += clutter_debug_c_args

clutter_tests_conform_link_args = [
  '-Wl,--export-dynamic',
]

clutter_conform_tests_actor_tests = [
  'actor-anchors',
  'actor-destroy',
  'actor-graph',
  'actor-invariants',
  'actor-iter',
  'actor-layout',
  'actor-meta',
  'actor-offscreen-limit-max-size',
  'actor-offscreen-redirect',
  'actor-paint-opacity',
  'actor-pick',
  'actor-shader-effect',
  'actor-size',
]

clutter_conform_tests_classes_tests = [
  'text',
]

clutter_conform_tests_general_tests = [
  'binding-pool',
  'color',
  'events-touch',
  'interval',
  'model',
  'script-parser',
  'units',
]

clutter_conform_tests_deprecated_tests = [
  'animator',
  'behaviours',
  'group',
  'rectangle',
  'texture',
]

clutter_conform_tests = []
clutter_conform_tests +=  clutter_conform_tests_actor_tests
clutter_conform_tests += clutter_conform_tests_classes_tests
clutter_conform_tests += clutter_conform_tests_general_tests
clutter_conform_tests += clutter_conform_tests_deprecated_tests

test_env = environment()
test_env.set('G_TEST_SRCDIR', clutter_tests_conform_srcdir)
test_env.set('G_TEST_BUILDDIR', clutter_tests_conform_builddir)
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('CLUTTER_ENABLE_DIAGNOSTIC', '0')
test_env.set('CLUTTER_SCALE', '1')

foreach test : clutter_conform_tests
  test_executable = executable('@0@'.format(test),
    sources: [
      '@0@.c'.format(test),
    ],
    include_directories: clutter_includes,
    c_args: clutter_tests_conform_c_args,
    link_args: clutter_tests_conform_link_args,
    dependencies: [
      clutter_deps,
      libmutter_clutter_dep,
      libmutter_cogl_path_dep
    ],
    install: false,
  )

  test(test, test_executable,
    suite: ['clutter', 'clutter/conform'],
    env: test_env
  )

  if have_headless_tests
    test(test, xvfb,
      args: test_executable,
      suite: ['clutter-headless', 'clutter-headless/conform', 'headless'],
      env: test_env,
      is_parallel: false,
    )
  endif
endforeach
