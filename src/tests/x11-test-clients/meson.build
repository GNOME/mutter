x11_test_client_installed_tests_libexecdir = \
  mutter_installed_tests_libexecdir / 'x11-test-clients'

x11_test_client_deps = [
  x11_dep,
  glib_dep,
]

x11_test_client_sources = [
  'x11-test-client-utils.c',
  'x11-test-client-utils.h',
]

x11_test_clients = [
  {
    'name': 'xwayland-allow-commits-test',
  },
  {
    'name': 'user-position',
  },
]

x11_test_client_executables = {}
foreach test : x11_test_clients
  test_name = test['name']

  test_client_sources = [
    '@0@.c'.format(test_name),
    x11_test_client_sources,
  ]

  test_executable = executable(test_name,
    sources: test_client_sources,
    include_directories: tests_includes,
    c_args: tests_c_args,
    dependencies: x11_test_client_deps,
    install: have_installed_tests,
    install_dir: x11_test_client_installed_tests_libexecdir,
    install_rpath: pkglibdir,
  )

  x11_test_client_executables += { test['name']: test_executable }
endforeach
