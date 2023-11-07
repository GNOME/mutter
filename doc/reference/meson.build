if not have_introspection
  error('API reference requires introspection.')
endif


toml_conf = configuration_data()
toml_conf.set('version', libmutter_api_version)
toml_conf.set('vcs_tag', 'main')

gidocgen = find_program('gi-docgen')

gidocgen_common_args = [
  '--quiet',
  '--no-namespace-dir',
]

if get_option('werror')
  gidocgen_common_args += ['--fatal-warnings']
endif

docs_dir = pkgdatadir / 'doc'

subdir('clutter')
subdir('cogl')
subdir('meta')
subdir('mtk')
