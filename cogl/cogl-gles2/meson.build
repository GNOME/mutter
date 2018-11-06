cogl_gles2_public_headers = [
  'GLES2/gl2.h',
  'GLES2/gl2ext.h',
  'GLES2/gl2platform.h',
]

cogl_gles2_sources = [
  'cogl-gles2-api.c',
]

libmutter_cogl_gles2 = shared_library('mutter-cogl-gles2-' + libmutter_api_version,
  sources: [cogl_gles2_sources, cogl_gles2_public_headers],
  c_args: cogl_c_args,
  include_directories: [cogl_includepath, cogl_path_includepath],
  link_depends: 'libmutter-cogl-gles2.map',
  dependencies: [
    cogl_deps,
    libmutter_cogl_dep
  ],
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)

cogl_gles2_includedir = join_paths(cogl_includedir, 'cogl-gles2/GLES2')
install_headers(cogl_gles2_public_headers,
  subdir: cogl_gles2_includedir)

pkg.generate(
  name: 'CoglGles2',
  filebase: 'mutter-cogl-gles2-' + libmutter_api_version,
  description: 'A cogl GLES2 helper library for mutter',
  libraries: [libmutter_cogl_gles2],
  subdirs: join_paths(pkgname, 'cogl'),
  requires: [cogl_pkg_deps, libmutter_cogl_name],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
)
