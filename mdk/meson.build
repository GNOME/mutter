mdk_sources = [
  'mdk-context.c',
  'mdk-context.h',
  'mdk-monitor.c',
  'mdk-monitor.h',
  'mdk-pipewire.c',
  'mdk-pipewire.h',
  'mdk-session.c',
  'mdk-session.h',
  'mdk-stream.c',
  'mdk-stream.h',
  'mdk-types.h',
  'mdk-main.c',
]

mdk_c_args = mutter_c_args
mdk_c_args += [
  '-DG_LOG_DOMAIN="mdk"',
]

mdk_built_sources = []

mutter_devkit_resources = gnome.compile_resources('mutter-devkit-resources',
  'mutter-devkit.gresource.xml',
  source_dir: '.',
)
mdk_built_sources = mutter_devkit_resources

dbus_remote_desktop_built_sources = gnome.gdbus_codegen('mdk-dbus-remote-desktop',
    join_paths(dbus_interfaces_dir, 'org.gnome.Mutter.RemoteDesktop.xml'),
    interface_prefix: 'org.gnome.Mutter.',
    namespace: 'MdkDBus',
  )
mdk_built_sources += dbus_remote_desktop_built_sources

dbus_screen_cast_built_sources = gnome.gdbus_codegen('mdk-dbus-screen-cast',
    join_paths(dbus_interfaces_dir, 'org.gnome.Mutter.ScreenCast.xml'),
    interface_prefix: 'org.gnome.Mutter.',
    namespace: 'MdkDBus',
  )
mdk_built_sources += dbus_screen_cast_built_sources

executable('mutter-devkit',
  sources: [
    'mdk-main.c',
    mdk_sources,
    mdk_built_sources,
  ],
  include_directories: [
    top_includepath,
  ],
  c_args: mdk_c_args,
  dependencies: [
    gtk4_dep,
    libpipewire_dep,
  ],
  install_dir: libexecdir,
  install: true,
)
