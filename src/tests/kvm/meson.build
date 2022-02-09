build_linux = find_program('build-linux.sh')

kernel_image_path = get_option('kvm_kernel_image')
if kernel_image_path != ''
  if not fs.is_absolute(kernel_image_path)
    error('Kernel image path @0@ must be absolute'.format(kernel_image_path))
  endif
  if not fs.is_file(kernel_image_path)
    error('Kernel image @0@ does not exist'.format(kernel_image_path))
  endif
  kernel_image_target = []
else
  kernel_image_target = custom_target('linux-kernel',
    output: 'bzImage',
    input: 'kernel-version.txt',
    console: true,
    command: [
      build_linux,
      'https://anongit.freedesktop.org/git/drm/drm.git',
      '@INPUT@',
      '@OUTPUT@',
      '--enable', 'CONFIG_DRM_VKMS',
    ]
  )
  kernel_image_path = kernel_image_target.full_path()
endif

virtme_run = find_program('virtme-run.sh')

vm_env_entries = []
foreach name, value: test_env_variables
  vm_env_entries += ['@0@=@1@'.format(name, value)]
endforeach
vm_env = ' '.join(vm_env_entries)

foreach test_case: privileged_tests
  test('kvm-' + test_case['name'], virtme_run,
    suite: ['core', 'mutter/kvm', 'mutter/kvm/' + test_case['suite']],
    depends: [
      kernel_image_target,
    ],
    workdir: mutter_srcdir,
    env: test_env,
    args: [
      kernel_image_path,
      meta_dbus_runner.full_path(),
      '--kvm',
      test_case['executable'].full_path(),
      meson.current_build_dir(),
      vm_env,
    ]
  )
endforeach
