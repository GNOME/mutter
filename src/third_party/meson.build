xcursor_sources = [
  'xcursor/xcursor.c',
  'xcursor/xcursor.h',
]
xcursor_includes = include_directories('xcursor')
xcursor_static_lib = static_library('xcursor',
  xcursor_sources,
  c_args: mutter_c_args,
  dependencies: [glib_dep],
  include_directories: xcursor_includes,
  install: false
)
xcursor_static_dep = declare_dependency(
  include_directories: xcursor_includes,
  link_with: xcursor_static_lib,
  sources: xcursor_sources,
) 
mutter_deps += [xcursor_static_dep]
