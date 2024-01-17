mtk_mtk_includesubdir = mtk_includesubdir / 'mtk'

mtk_headers = [
  'mtk.h',
  'mtk-macros.h',
  'mtk-rectangle.h',
  'mtk-region.h',
]

mtk_sources = [
  'mtk-rectangle.c',
  'mtk-region.c',
]

if have_x11
  mtk_sources += 'mtk-x11-errors.c'
  mtk_headers += [
    'mtk-x11-errors.h',
    'mtk-x11.h',
  ]
endif

mtk_private_headers = [
]


libmutter_mtk_name = 'mutter-mtk-' + libmutter_api_version
libmutter_mtk = shared_library(libmutter_mtk_name,
  sources: [
    mtk_sources,
    mtk_headers,
    mtk_private_headers,
  ],
  version: '0.0.0',
  soversion: 0,
  c_args: mtk_c_args,
  include_directories: mtk_includes,
  dependencies: [mtk_deps],
  gnu_symbol_visibility: 'hidden',
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)
libmutter_mtk_dep = declare_dependency(
  link_with: libmutter_mtk,
  dependencies: mtk_deps,
)

if have_introspection
  mtk_introspection_args = introspection_args + [
    '-DMTK_SYSCONFDIR="@0@"'.format(prefix / sysconfdir),
    '-DMTK_COMPILATION=1',
    '-DG_LOG_DOMAIN="Mtk"'
  ]

  libmutter_mtk_gir = gnome.generate_gir(libmutter_mtk,
    sources: [
      mtk_sources,
      mtk_headers,
    ],
    nsversion: libmutter_api_version,
    namespace: 'Mtk',
    export_packages: [libmutter_mtk_name],
    includes: [
      'GObject-2.0',
      'Graphene-1.0',
    ],
    extra_args: mtk_introspection_args + ['--c-include=mtk/mtk.h'],
    kwargs: introspection_common,
  )

endif

install_headers(mtk_headers,
  subdir: mtk_mtk_includesubdir)


pkg.generate(libmutter_mtk,
  name: 'Mutter Toolkit',
  filebase: libmutter_mtk_name,
  description: 'Mutter Toolkit Private Library',
  libraries: [m_dep],
  subdirs: pkgname / 'mtk',
  requires: [mtk_pkg_deps],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
  install_dir: pcdir,
)
