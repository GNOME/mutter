mtk_includesubdir = pkgname / 'mtk'
mtk_includedir = includedir / mtk_includesubdir

mtk_includepath = include_directories('.', 'mtk')
mtk_includes = [
  mtk_includepath,
  top_includepath,
]

mtk_c_args = [
  '-DMTK_SYSCONFDIR="@0@"'.format(prefix / sysconfdir),
  '-DMTK_COMPILATION=1',
  '-DG_LOG_DOMAIN="Mtk"',
]

mtk_debug_c_args = []
if buildtype != 'plain'
  if not get_option('debug')
    mtk_debug_c_args += [
      '-DG_DISABLE_ASSERT',
      '-DG_DISABLE_CAST_CHECKS',
    ]
  endif
endif
mtk_debug_c_args = cc.get_supported_arguments(mtk_debug_c_args)
mtk_c_args += mtk_debug_c_args

mtk_pkg_deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
  graphene_dep,
  pixman_dep,
]

if have_x11
  mtk_pkg_deps += x11_dep
endif

mtk_deps = [
  mtk_pkg_deps,
  m_dep
]

subdir('mtk')
