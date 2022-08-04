cogl_tests = [
  'test-atlas-migration',
  'test-blend-strings',
  'test-blend',
  'test-depth-test',
  'test-color-hsl',
  'test-backface-culling',
  'test-just-vertex-shader',
  'test-pipeline-user-matrix',
  'test-pipeline-uniforms',
]

cogl_test_conformance_includes = [
  tests_includepath,
  cogl_includepath,
]

cogl_test_c_args = [
  cogl_debug_c_args,
  '-DCOGL_ENABLE_EXPERIMENTAL_API',
  '-DCOGL_DISABLE_DEPRECATED',
  '-DCOGL_DISABLE_DEPRECATION_WARNINGS',
  '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('MUTTER_TEST_PLUGIN_PATH', '@0@'.format(default_plugin.full_path()))

cogl_test_variants = [ 'gl', 'gl3', 'gles2' ]

foreach cogl_test: cogl_tests
  test_name = 'cogl-' + cogl_test
  test_executable = executable(test_name,
    sources: [
      cogl_test_utils,
      cogl_test + '.c',
    ],
    c_args: cogl_test_c_args,
    include_directories: cogl_test_conformance_includes,
    dependencies: [
      libmutter_test_dep,
    ],
    install_rpath: pkglibdir,
  )

  foreach variant: cogl_test_variants
    variant_test_env = test_env
    variant_test_env.set('COGL_DRIVER', variant)

    test(test_name + '-' + variant, test_executable,
      suite: ['cogl', 'cogl/conform'],
      env: variant_test_env,
      is_parallel: false,
    )
  endforeach
endforeach
