cogl_run_tests = find_program('run-tests.sh')

cdata = configuration_data()
cdata.set('HAVE_GL', have_gl.to_int())
cdata.set('HAVE_GLES2', have_gles2.to_int())

# Until meson 0.50 setting the install parameter in 'configure_file' is ignored
# if 'install_dir' is set, then until mutter doesn't depend on such meson version
# cogl_installed_tests_libexecdir should be empty unless have_installed_tests
# is false, or this file will be installed anyway.
#
# See https://github.com/mesonbuild/meson/issues/4160
cogl_installed_tests_libexecdir = ''

if have_installed_tests
  cogl_installed_tests_libexecdir = join_paths(
    mutter_installed_tests_libexecdir, 'cogl', 'conform')
  install_data('run-tests.sh', install_dir: cogl_installed_tests_libexecdir)
endif

cogl_config_env = configure_file(
  input: 'config.env.in',
  output: 'config.env',
  configuration: cdata,
  install: have_installed_tests,
  install_dir: cogl_installed_tests_libexecdir,
)

subdir('conform')
subdir('unit')
