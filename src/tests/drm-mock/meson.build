drm_mock = shared_library('drm-mock',
  sources: [
    'drm-mock.c',
  ],
  dependencies: [
    glib_dep,
    libdrm_dep,
  ],
  install: false,
)

drm_mock_dep = declare_dependency(
  link_with: drm_mock,
)
