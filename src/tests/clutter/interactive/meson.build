clutter_tests_interactive_srcdir = meson.current_source_dir()
clutter_tests_interactive_includepath = include_directories('.')

clutter_tests_interactive_c_args = [
  '-DTESTS_DATADIR="@0@"'.format(clutter_tests_interactive_srcdir),
  '-DG_DISABLE_SINGLE_INCLUDES',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]
clutter_tests_interactive_c_args += clutter_debug_c_args

clutter_tests_interactive_link_args = [
  '-Wl,--export-dynamic',
]

clutter_tests_interactive_test_sources = [
  'test-events.c',
  'test-grab.c',
  'test-animation.c',
  'test-binding-pool.c',
  'test-cogl-point-sprites.c',
  'test-keyframe-transition.c',
]

if have_fonts
  clutter_tests_interactive_test_sources += [
    'test-content.c',
    'test-stage-sizing.c',
    'test-text.c',
    'test-text-field.c',
  ]
endif

gen_test_unit_names = find_program('meson/gen-test-unit-names.sh')
clutter_interactive_test_unit_names_h = custom_target('gen-test-unit-names',
  output: 'test-unit-names.h',
  input: clutter_tests_interactive_test_sources,
  command: [gen_test_unit_names, '@OUTPUT@', '@INPUT@'],
  install: false,
)

clutter_tests_interactive_sources = [
  'test-main.c',
  clutter_interactive_test_unit_names_h,
  clutter_tests_interactive_test_sources,
  clutter_test_utils,
]

executable('test-interactive',
  sources: clutter_tests_interactive_sources,
  include_directories: [
    clutter_includes,
    clutter_tests_includes,
    clutter_tests_interactive_includepath,
  ],
  c_args: clutter_tests_interactive_c_args,
  link_args: clutter_tests_interactive_link_args,
  dependencies: [
    libmutter_test_dep,
  ],
  install: false,
)
