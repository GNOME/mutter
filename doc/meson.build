# manpage
subdir('man')

# Reference manual
subdir('xml')

version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_xml = configure_file(
  input: 'version.xml.in',
  output: 'version.xml',
  configuration: version_conf
)

reference_ignored_headers = [
  'config.h',

  'bell.h',
  'boxes-private.h',
  'compositor-private.h',
  'core.h',
  'constraints.h',
  'display-private.h',
  'frame.h',
  'frames.h',
  'group-private.h',
  'keybindings-private.h',
  'main.h',
  'meta-backend-private.h',
  'meta-background-private.h',
  'meta-barrier-private.h',
  'meta-dnd-actor-private.h',
  'meta-dnd-private.h',
  'meta-feedback-actor-private.h',
  'meta-gesture-tracker-private.h',
  'meta-idle-monitor-private.h',
  'meta-input-mapper-private.h',
  'meta-input-settings-private.h',
  'meta-monitor-manager-private.h',
  'meta-remote-access-controller-private.h',
  'meta-settings-private.h',
  'meta-shaped-texture-private.h',
  'meta-stage-private.h',
  'meta-wayland-private.h',
  'meta-window-actor-private.h',
  'meta-window-group-private.h',
  'meta-xwayland-private.h',
  'meta-xwayland-selection-private.h',
  'theme-private.h',
  'util-private.h',
  'window-private.h',
  'window-x11-private.h',
  'workspace-private.h',
  # Test headers
  'meta-backend-test.h',
  'meta-monitor-manager-test.h',
  'meta-wayland-egl-stream.h',
  'meta-wayland-inhibit-shortcuts.h',
  'meta-wayland-text-input.h',
  'meta-xwayland-grab-keyboard.h',
]

# Make sure we ignore the wayland headers
if have_wayland
  foreach proto : wayland_protocols
    protocol_name = proto[0]
    protocol_type = proto[1]
    if protocol_type == 'stable'
      output_base = protocol_name
    elif protocol_type == 'private'
      output_base = protocol_name
    elif protocol_type == 'third-party'
      output_base = protocol_name
    else
      protocol_version = proto[2]
      output_base = '@0@-@1@-@2@'.format(protocol_name,
                                         protocol_type,
                                         protocol_version)
    endif

    proto_header = '@0@-server-protocol.h'.format(output_base)
    reference_ignored_headers += proto_header
  endforeach
endif


gnome.gtkdoc(meson.project_name(),
  main_sgml: 'mutter-docs.sgml',
  src_dir: [
    include_directories('../src'),
  ],
  dependencies: _libmutter_public_symbols_dep,
  gobject_typesfile: 'mutter.types',
  scan_args: [
    '--rebuild-types',
    '--ignore-headers=' + ' '.join(reference_ignored_headers),
    '--ignore-decorators=META_EXPORT|META_EXPORT_TEST',
  ],
  install: true,
)
