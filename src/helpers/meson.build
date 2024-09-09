# restart helper
if have_x11
  executable('mutter-restart-helper',
    sources: [
      files('meta-restart-helper.c'),
    ],
    include_directories: top_includepath,
    c_args: [
      mutter_c_args,
      '-DG_LOG_DOMAIN="mutter-restart-helper"',
    ],
    dependencies: [
      x11_dep,
      xcomposite_dep,
    ],
    install_dir: libexecdir,
    install: true,
  )
endif