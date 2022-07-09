generated = wl_mod.scan_xml(
  'test-driver.xml',
  client: true,
  server: true,
)
test_driver_protocol_code = generated[0]
test_driver_client_header = generated[1]
test_driver_server_header = generated[2]

generated = wl_mod.scan_xml(
  'dummy.xml',
  client: true,
  server: true,
)
dummy_protocol_code = generated[0]
dummy_client_header = generated[1]
dummy_server_header = generated[2]
