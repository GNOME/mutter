wayland_test_client_installed_tests_libexecdir = \
  mutter_installed_tests_libexecdir / 'wayland-test-clients'

wayland_test_client_sources = files([
  'wayland-test-client-utils.c',
  'wayland-test-client-utils.h',
])

wayland_test_client_utils = [
  wayland_test_client_sources,
  wayland_protocol_client_headers,
  wayland_protocol_sources,
  test_driver_client_header,
  test_driver_protocol_code,
]

wayland_test_clients = [
  {
    'name': 'buffer-transform',
  },
  {
    'name': 'dma-buf-scanout',
  },
  {
    'name': 'fractional-scale',
  },
  {
    'name': 'fullscreen',
  },
  {
    'name': 'idle-inhibit',
  },
  {
    'name': 'invalid-subsurfaces',
  },
  {
    'name': 'invalid-xdg-shell-actions',
  },
  {
    'name': 'kms-cursor-hotplug-helper',
    'extra_deps': [
      wayland_cursor_dep,
    ],
  },
  {
    'name': 'service-client',
    'extra_sources': [
      built_dbus_sources['meta-dbus-service-channel'],
    ],
    'extra_deps': [
      gio_unix_dep,
    ],
  },
  {
    'name': 'single-pixel-buffer',
  },
  {
    'name': 'subsurface-corner-cases',
  },
  {
    'name': 'subsurface-parent-unmapped',
  },
  {
    'name': 'subsurface-remap-toplevel',
  },
  {
    'name': 'subsurface-reparenting',
  },
  {
    'name': 'xdg-activation',
  },
  {
    'name': 'xdg-apply-limits',
  },
  {
    'name': 'xdg-foreign',
  },
  {
    'name': 'xdg-toplevel-bounds',
  },
  {
    'name': 'ycbcr',
  },
]

test_client_executables = {}

foreach test : wayland_test_clients
  test_name = test['name']
  deps = [
    glib_dep,
    gobject_dep,
    wayland_client_dep,
    libdrm_dep,
    libgbm_dep,
    m_dep,
  ]
  if test.has_key('extra_deps')
    deps += test['extra_deps']
  endif

  test_client_sources = [
    '@0@.c'.format(test_name),
    wayland_test_client_utils,
  ]
  if test.has_key('extra_sources')
    test_client_sources += test['extra_sources']
  endif

  test_executable = executable(test_name,
    sources: test_client_sources,
    include_directories: tests_includes,
    c_args: tests_c_args,
    dependencies: deps,
    install: have_installed_tests,
    install_dir: wayland_test_client_installed_tests_libexecdir,
  )

  test_client_executables += { test['name']: test_executable }
endforeach
