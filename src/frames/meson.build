x11_frames_sources = [
  'main.c',
  'meta-frame.c',
  'meta-frame-content.c',
  'meta-frame-header.c',
  'meta-window-tracker.c',
]

x11_frames = executable('mutter-x11-frames',
  sources: x11_frames_sources,
  dependencies: [
    gmodule_no_export_dep,
    gsettings_desktop_schemas_dep,
    gtk4_dep,
    x11_dep,
    xfixes_dep,
    xi_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="mutter-x11-frames"',
  ],
  include_directories: top_includepath,
  install: true,
  install_dir: get_option('libexecdir'),
)
