install_man('mutter.1')

rst2man = find_program('rst2man')

custom_target('gdctl.1',
  input: 'gdctl.rst',
  output: 'gdctl.1',
  command: [rst2man, '--syntax-highlight=none', '@INPUT@'],
  capture: true,
  install_dir: mandir + '/man1',
  install: true
)

custom_target('gnome-service-client.1',
  input: 'gnome-service-client.rst',
  output: 'gnome-service-client.1',
  command: [rst2man, '--syntax-highlight=none', '@INPUT@'],
  capture: true,
  install_dir: mandir + '/man1',
  install: true
)
