cogl_run_tests = find_program('run-tests.sh')

cdata = configuration_data()
cdata.set('HAVE_GL', have_gl.to_int())
cdata.set('HAVE_GLES2', have_gles2.to_int())

cogl_config_env = configure_file(
  input: 'config.env.in',
  output: 'config.env',
  configuration: cdata)

subdir('conform')
subdir('unit')
