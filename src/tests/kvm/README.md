## High level description of the files in this directory.

### build-linux.sh

Builds a Linux kernel image meant to be launched using qemu. Doesn't make any
assumptions about configuration other than that. It uses the drm kernel tree.
It's used from meson.build.

### kernel-version.txt

Describes the version of the Linux kernel to use; usually a tag. It's a
separate file so that changing the version will make meson build a new kernel
image.

### virtme-run.sh

A helper script that uses 'virtme' to launch a qemu virtual machine with the
host filesystem exposed inside the virtual machine.

### run-kvm-test.sh

Runs the passed test executable in a mocked environment using
'meta-dbus-runner.py' (which uses python-dbusmock to create a mocked system
environment.

### meson.build

Contains one rule for building the Linux kernel image, and meson test cases
that launches tests inside virtual machines.
