install_data(
  'gdctl',
  install_dir: bindir,
)

install_data(
  'gnome-service-client',
  install_dir: bindir,
)

if have_bash_completion
  bash_completion = dependency('bash-completion', required: false)
  if bash_completion.found()
    bash_completion_dir = bash_completion.get_variable(
      pkgconfig: 'completionsdir',
      pkgconfig_define: ['datadir', datadir],
    )
  else
    bash_completion_dir = get_option('sysconfdir') / 'bash_completion.d'
  endif

  register_python_argcomplete = find_program('register-python-argcomplete')

  custom_target(
    'gdctl-bash-completion',
    output: 'gdctl',
    command: [
      register_python_argcomplete,
      'gdctl',
      '--complete-arguments',
      '-o nosort',
    ],
    capture: true,
    install_dir: bash_completion_dir,
    install: true,
  )
endif

gdctl = find_program('gdctl')
get_state_tool = find_program('get-state.py')
