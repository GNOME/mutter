mtk_includesubdir = pkgname / 'mtk'
mtk_includedir = includedir / mtk_includesubdir

mtk_includepath = include_directories('.', 'mtk')
mtk_includes = [mtk_includepath]

mtk_c_args = [
  '-DMTK_SYSCONFDIR="@0@"'.format(prefix / sysconfdir),
  '-DMTK_COMPILATION=1',
  '-DG_LOG_DOMAIN="Mtk"',
]

mtk_debug_c_args = []
if get_option('debug')
  mtk_debug_c_args += [
    '-fno-omit-frame-pointer'
  ]
elif buildtype != 'plain'
  mtk_debug_c_args += [
    '-DG_DISABLE_ASSERT',
    '-DG_DISABLE_CAST_CHECKS',
  ]
endif
supported_mtk_debug_c_args = cc.get_supported_arguments(mtk_debug_c_args)
mtk_c_args += mtk_debug_c_args

mtk_pkg_deps = [
  cairo_gobject_dep,
  glib_dep,
  gobject_dep,
  gio_dep,
  graphene_dep,
]

mtk_deps = [
  mtk_pkg_deps,
  m_dep
]

subdir('mtk')