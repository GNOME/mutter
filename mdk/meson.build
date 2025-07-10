mdk_sources = [
  'mdk-context.c',
  'mdk-context.h',
  'mdk-device.c',
  'mdk-device.h',
  'mdk-ei.c',
  'mdk-ei.h',
  'mdk-keyboard.c',
  'mdk-keyboard.h',
  'mdk-launcher-action-item.c',
  'mdk-launcher-action-item.h',
  'mdk-launcher-adder.c',
  'mdk-launcher-adder.h',
  'mdk-launcher-entry.c',
  'mdk-launcher-entry.h',
  'mdk-launcher.c',
  'mdk-launcher.h',
  'mdk-launchers-editor.c',
  'mdk-launchers-editor.h',
  'mdk-main-window.c',
  'mdk-main-window.h',
  'mdk-main.c',
  'mdk-monitor.c',
  'mdk-monitor.h',
  'mdk-pipewire.c',
  'mdk-pipewire.h',
  'mdk-pointer.c',
  'mdk-pointer.h',
  'mdk-seat.c',
  'mdk-seat.h',
  'mdk-session.c',
  'mdk-session.h',
  'mdk-stream.c',
  'mdk-stream.h',
  'mdk-touch.c',
  'mdk-touch.h',
  'mdk-types.h',
  'mdk-window.c',
  'mdk-window.h',
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

dbus_devkit_built_sources = gnome.gdbus_codegen('mdk-dbus-devkit',
    join_paths(dbus_interfaces_dir, 'org.gnome.Mutter.Devkit.xml'),
    interface_prefix: 'org.gnome.Mutter.',
    namespace: 'MdkDBus',
  )
mdk_built_sources += dbus_devkit_built_sources

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
    gio_unix_dep,
    gmodule_no_export_dep,
    gtk4_dep,
    libadwaita_dep,
    libpipewire_dep,
    libdrm_dep,
    libei_dep,
  ],
  install_dir: libexecdir,
  install: true,
)

subdir('data')
