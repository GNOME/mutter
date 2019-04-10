cogl_gles2_public_headers = [
  'GLES2/gl2.h',
  'GLES2/gl2ext.h',
  'GLES2/gl2platform.h',
]

cogl_gles2_sources = [
  'cogl-gles2-api.c',
]

libmutter_cogl_gles2_map = 'libmutter-cogl-gles2.map'
libmutter_cogl_gles2_link_args = [
  '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(),
      libmutter_cogl_gles2_map),
]
libmutter_cogl_gles2 = shared_library('mutter-cogl-gles2-' + libmutter_api_version,
  sources: [cogl_gles2_sources, cogl_gles2_public_headers],
  version: '0.0.0',
  soversion: 0,
  c_args: cogl_c_args,
  include_directories: [cogl_includepath, cogl_path_includepath],
  link_depends: libmutter_cogl_gles2_map,
  link_args: libmutter_cogl_gles2_link_args,
  dependencies: libmutter_cogl_dep,
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)

cogl_gles2_includesubdir = join_paths(cogl_includesubdir, 'cogl-gles2/GLES2')
install_headers(cogl_gles2_public_headers,
  subdir: cogl_gles2_includesubdir)

pkg.generate(libmutter_cogl_gles2,
  name: 'CoglGles2',
  filebase: 'mutter-cogl-gles2-' + libmutter_api_version,
  description: 'A cogl GLES2 helper library for mutter',
  subdirs: join_paths(pkgname, 'cogl'),
  requires: [cogl_pkg_deps, libmutter_cogl_name],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
  install_dir: pcdir,
)
