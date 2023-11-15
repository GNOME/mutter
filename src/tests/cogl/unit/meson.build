all_variants = ['gl3', 'gles2']
any_variant = ['any']

cogl_unit_tests = [
  ['test-bitmask', true, any_variant],
  ['test-pipeline-cache', true, all_variants],
  ['test-pipeline-state-known-failure', false, all_variants],
  ['test-pipeline-state', true, all_variants],
  ['test-pipeline-glsl', true, all_variants],
  ['test-pipeline-vertend-glsl', true, all_variants],
]

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('G_ENABLE_DIAGNOSTIC', '0')

foreach unit_test: cogl_unit_tests
  test_name = 'cogl-' + unit_test[0]
  should_pass = unit_test[1]
  variants = unit_test[2]

  test_executable = executable(test_name,
    sources: [
      unit_test[0] + '.c',
      cogl_test_utils,
    ],
    c_args: [
      '-D__COGL_H_INSIDE__',
      '-DCOGL_ENABLE_MUTTER_API',
    ],
    include_directories: [
      cogl_includepath,
    ],
    dependencies: [
      libmutter_test_dep,
    ],
  )

  foreach variant: variants
    if variant == 'any'
      variant_test_env = test_env
    else
      variant_test_env = test_env
      variant_test_env.set('COGL_DRIVER', variant)
    endif

    test('@0@-@1@'.format(test_name, variant), test_executable,
      suite: ['cogl', 'cogl/unit'],
      env: variant_test_env,
      is_parallel: false,
      should_fail: not should_pass,
    )
  endforeach
endforeach
