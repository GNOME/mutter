clutter_test_accessibility_common_sources = [
  'cally-examples-util.c',
  'cally-examples-util.h',
]

clutter_test_accessibility_c_args = [
  '-DPREFIXDIR="@0@"'.format(libdir),
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

clutter_test_accessibility_c_args += clutter_debug_c_args

clutter_accessibility_tests_dependencies = [
  libmutter_test_dep,
]

clutter_accessibility_tests = [
  'cally-atkcomponent-example',
]

if have_fonts
  clutter_accessibility_tests += [
    'cally-atkeditabletext-example',
    'cally-atkevents-example',
    'cally-atktext-example',
    'cally-clone-example',
  ]
endif

foreach test : clutter_accessibility_tests
  executable(test,
    sources: [
      clutter_test_accessibility_common_sources,
      test + '.c',
      clutter_test_utils,
    ],
    include_directories: clutter_includes,
    c_args: clutter_test_accessibility_c_args,
    dependencies: [clutter_accessibility_tests_dependencies],
    install: false,
  )
endforeach
