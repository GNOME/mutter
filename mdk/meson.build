mutter_devkit_resources = gnome.compile_resources('mutter-devkit-resources',
  'mutter-devkit.gresource.xml',
  source_dir: '.',
)

mdk_c_args = mutter_c_args
mdk_c_args += [
  '-DG_LOG_DOMAIN="mdk"',
]

executable('mutter-devkit',
  sources: [
    'mdk-main.c',
    mutter_devkit_resources,
  ],
  include_directories: [
    top_includepath,
  ],
  c_args: mdk_c_args,
  dependencies: [gtk4_dep],
  install_dir: libexecdir,
  install: true,
)
