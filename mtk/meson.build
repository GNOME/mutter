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

mtk_pkg_deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
  graphene_dep,
  pixman_dep,
]

if have_x11_client
  mtk_pkg_deps += x11_dep
endif

mtk_deps = [
  mtk_pkg_deps,
  m_dep
]

subdir('mtk')
