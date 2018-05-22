cogl_test_journal_sources = [
  'test-journal.c',
]

cogl_test_journal = executable('test-journal',
  sources: cogl_test_journal_sources,
  c_args: cogl_debug_c_args + [
    '-DCOGL_DISABLE_DEPRECATED',
    '-DTESTS_DATADIR="@0@"'.format(join_paths(cogl_srcdir, 'tests/data')),
  ],
  include_directories: cogl_includepath,
  dependencies: cogl_deps,
  link_with: [libmutter_cogl],
  install: false,
)
