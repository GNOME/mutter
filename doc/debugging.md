# Debugging

## Nested D-Bus session

### Installing

Download and place [dbus-session.sh](uploads/a209c8f1fe6b51df669b58bab1300199/dbus-session.sh) in `~/.local/bin/` and make it executable.

### Using

To create a nested D-Bus user session, run

```sh
dbus-session.sh -n
```

This will create a D-Bus session, and attach to it.

To attach to the same session from another terminal, run

```sh
dbus-session.sh -x
```

## Nested mutter or gnome-shell

To run a nested mutter or gnome-shell instance, i.e. when you are presented with a floating window running mutter or gnome-shell, first enter a [nested D-Bus session](#nested-d-bus-session), then pass `--nested` to either gnome-shell or mutter. E.g.

```sh
mutter --nested
```

## Headless GNOME Shell and GNOME Remote Desktop

First create [nested D-Bus session](#nested-d-bus-session). In this, run gnome-shell in headless mode with a virtual monitor. E.g.

```sh
[jonas@localhost gnome-shell]$ dbus-session.sh -n
[ D-Bus 60ff ][jonas@localhost gnome-shell]$ gnome-shell --headless --virtual-monitor 1280x720
```

```sh
[jonas@localhost gnome-remote-desktop]$ dbus-session.sh -x
[ D-Bus 60ff ][jonas@localhost gnome-remote-desktop]$ ./build/src/gnome-remote-desktop-daemon
```



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
