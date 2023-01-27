test_driver_server_header = custom_target(
  'test-driver server header',
  input: 'test-driver.xml',
  output: 'test-driver-server-protocol.h',
  command: [
    wayland_scanner,
    'server-header',
    '@INPUT@', '@OUTPUT@',
  ]
)

test_driver_client_header = custom_target(
  'test-driver client header',
  input: 'test-driver.xml',
  output: 'test-driver-client-protocol.h',
  command: [
    wayland_scanner,
    'client-header',
    '@INPUT@', '@OUTPUT@',
  ]
)

test_driver_protocol_code = custom_target(
  'test-driver protocol code',
  input: 'test-driver.xml',
  output: 'test-driver-protocol.c',
  command: [
    wayland_scanner,
    'private-code',
    '@INPUT@', '@OUTPUT@',
  ]
)
