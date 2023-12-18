# Debugging

## Reproducing CI test failures locally

1. Create a podman that can run gdb locally using the same image used in CI. The example below uses the tag `x86_64-2022-01-20` but this will depend on the image used by the failed CI job. The Fedora version may also differ.

    ```sh
    podman pull registry.gitlab.gnome.org/gnome/mutter/fedora/35:x86_64-2022-01-20
    podman run -t -i --cap-add=SYS_PTRACE registry.gitlab.gnome.org/gnome/mutter/fedora/35:x86_64-2022-01-20 bash -l
    ```

2. Clone, build and install mutter inside the container

    ```sh
    git clone https://gitlab.gnome.org/[your-user]/mutter.git -b [merge-request-branch]
    cd mutter
    meson build
    ninja -C build install
    ```

3. Install debug utilities

    ```sh
    dnf install -y gdb
    ```

4. Replicate a environment and run the test inside gdb. What you need here depends on the test that needs investigation. In the simplest case, the following is enough:

    ```sh
    export XDG_RUNTIME_DIR=$PWD/runtime-dir
    mkdir -p $XDG_RUNTIME_DIR
    ./src/tests/meta-dbus-runner.py xvfb-run meson test -C build --setup plain --gdb failing-test-case
    ```

    The need for `xvfb-run` depends on whether the test case uses the nested backend or the headless backend.

    If it involves screen casting, it becomes a bit more complicated:

    ```sh
    export XDG_RUNTIME_DIR=$PWD/runtime-dir
    mkdir -p $XDG_RUNTIME_DIR
    ./src/tests/meta-dbus-runner.py bash -l
    pipewire&
    wireplumber&
    meson test -C build --setup plain --gdb failing-test-case
    ```
