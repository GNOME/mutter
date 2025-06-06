cogl_tests = [
  [ 'test-atlas-migration', [] ],
  [ 'test-blend-strings', [] ],
  [ 'test-blend', [] ],
  [ 'test-depth-test', [] ],
  [ 'test-color', [] ],
  [ 'test-backface-culling', [] ],
  [ 'test-just-vertex-shader', [] ],
  [ 'test-pipeline-user-matrix', [] ],
  [ 'test-pipeline-uniforms', [] ],
  [ 'test-pixel-buffer', [] ],
  [ 'test-premult', [] ],
  [ 'test-snippets', [] ],
  [ 'test-wrap-modes', [] ],
  [ 'test-sub-texture', [] ],
  [ 'test-custom-attributes', [] ],
  [ 'test-offscreen', [] ],
  [ 'test-offscreen-texture-formats', [] ],
  [ 'test-journal', [] ],
  [ 'test-primitive', [] ],
  [ 'test-sparse-pipeline', [] ],
  [ 'test-read-texture-formats', ['gl3'] ],
  [ 'test-write-texture-formats', [] ],
  [ 'test-point-size', [] ],
  [ 'test-point-size-attribute', [] ],
  [ 'test-point-sprite', [] ],
  [ 'test-point-sprite-known-failure', ['gl3', 'gles2'] ],
  [ 'test-no-gl-header', [] ],
  [ 'test-layer-remove', [] ],
  [ 'test-alpha-test', [] ],
  [ 'test-map-buffer-range', [] ],
  [ 'test-npot-texture', [] ],
  [ 'test-alpha-textures', [] ],
  [ 'test-texture-get-set-data', [] ],
  [ 'test-framebuffer-get-bits', [] ],
  [ 'test-primitive-and-journal', [] ],
  [ 'test-copy-replace-texture', [] ],
  [ 'test-pipeline-cache-unrefs-texture', [] ],
  [ 'test-pipeline-shader-state', [] ],
  [ 'test-texture-rg', [] ],
]

#unported = [
#  "test-multitexture",
#  "test-npot-texture",
#  "test-readpixels",
#  "test-texture-mipmaps",
#  "test-texture-pixmap-x11",
#  "test-viewport",
#]

cogl_test_conformance_includes = [
  tests_includepath,
  cogl_includepath,
]

cogl_test_c_args = [
  cogl_debug_c_args,
  '-DTESTS_DATADIR="@0@/tests/data"'.format(cogl_srcdir),
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

test_env = environment()
test_env.set('G_TEST_SRCDIR',  mutter_srcdir / 'src' / 'tests')
test_env.set('G_TEST_BUILDDIR', mutter_builddir / 'src' / 'tests')
test_env.set('G_ENABLE_DIAGNOSTIC', '0')
test_env.set('GSETTINGS_SCHEMA_DIR', locally_compiled_schemas_dir)

cogl_test_variants = [ 'gl3', 'gles2' ]

foreach cogl_test: cogl_tests
  test_case = cogl_test[0]
  known_failures = cogl_test[1]
  test_name = 'cogl-' + test_case
  test_executable = executable(test_name,
    sources: [
      cogl_test_utils,
      test_case + '.c',
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
    variant_test_env.set('COGL_DEBUG', 'winsys')

    test(test_name + '-' + variant, test_executable,
      args: variant in known_failures ? ['-m', 'no-undefined'] : [],
      suite: ['cogl', 'cogl/conform'],
      env: variant_test_env,
      is_parallel: false,
    )

    if variant in known_failures
      test('-'.join([test_name, 'should-fail', variant]), test_executable,
        suite: ['cogl', 'cogl/conform', 'failing'],
        env: variant_test_env,
        is_parallel: false,
        should_fail: true,
      )
    endif
  endforeach
endforeach
