helpers_cdata = configuration_data()
helpers_cdata.set('libexecdir', libexecdir)

# backlight helper
configure_file(
  input: 'org.gnome.mutter.backlight-helper.policy.in',
  output: 'org.gnome.mutter.backlight-helper.policy',
  configuration: helpers_cdata,
  install_dir: join_paths(datadir, 'polkit-1', 'actions')
)

executable('mutter-backlight-helper',
  sources: [
    files('meta-backlight-helper.c'),
  ],
  include_directories: top_includepath,
  install_rpath: pkglibdir,
  install_dir: libexecdir,
  install: true,
)

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