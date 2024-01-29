# Debugging

## Native mutter

When running Mutter on the native backend, it takes over the input and output of the system. This poses a problem when we have to debug it: when a debugger stops Mutter, the input and output will not be released and the debugger cannot be controlled.

A second system is required in that case to continue controlling the debugger. Usually we login to a tty on the target system, create a "screen" session, and then detach from it. On the other system, we log in to the target system with ssh, resume the screen session (`screen -rd`), and then start Mutter the usual way. This even works in toolbox.

## gdb

gdb usually works like with any other application. There are situations, especially when debugging the native backend, where breaking the execution can screw up the timing of the thing that's being debugged. The gdb scripting feature and conditional breakpoints can be a great help.

## glib

When our code creates a warning or a critical message, we can make glib send a signal when this code is hit, to make gdb stop there. This can be done with setting `G_DEBUG` to either `fatal-warnings`, `fatal-criticals`, or both `fatal-warnings,fatal-criticals`.

## Mutter debug topics

It's possible to make Mutter much more verbose by turning on some debugging topics with the `MUTTER_DEBUG` environment variable.

The different topics are defined in `src/core/util.c` as `meta_debug_keys`. It's possible to enable multiple topics:
```sh
MUTTER_DEBUG="focus,stack" dbus-run-session mutter --wayland --nested
```

## Looking Glass

Gnome Shell has a build-in debugger called [Looking Glass](https://gitlab.gnome.org/GNOME/gnome-shell/-/blob/main/docs/looking-glass.md). It's possible to enable Mutter debug topics at runtime, show damage, run arbitrary JS code in the shell, and much more. It can be opened with the "Run a Command" prompt (Alt+F2) by executing `lg`.

## Tests

When a test fails when running `meson test`, it makes sense to re-run this specific test with `--print-errorlogs`. Meson's `--wrapper` argument is not useful here because the test cases run in their own special environment (`meta-dbus-runner.py`). Use the `META_DBUS_RUNNER_WRAPPER` environment variable instead to specify a custom wrapper binary. Meson takes over input and output which means it might be required to run the test outside of meson. A failing test prints out the command line that was invoked:
```sh
 11/243 mutter:cogl+cogl/conform / cogl-test-backface-culling-gl3                                               OK              1.49s
 12/243 mutter:cogl+cogl/conform / cogl-test-backface-culling-gles2                                             OK              1.46s
^CWARNING: CTRL-C detected, interrupting mutter:cogl+cogl/conform / cogl-test-just-vertex-shader-gl3
 13/243 mutter:cogl+cogl/conform / cogl-test-just-vertex-shader-gl3                                             INTERRUPT       0.75s   killed by signal 15 SIGTERM
>>> COGL_DRIVER=gl3 G_TEST_BUILDDIR=/var/home/swick/Projects/mutter/build/src/tests/cogl/conform MALLOC_PERTURB_=83 G_TEST_SRCDIR=/var/home/swick/Projects/mutter/src/tests/cogl/conform G_ENABLE_DIAGNOSTIC=0 LD_LIBRARY_PATH=/var/home/swick/Projects/mutter/build/cogl/cogl:/var/home/swick/Projects/mutter/build/src/tests:/var/home/swick/Projects/mutter/build/mtk/mtk:/var/home/swick/Projects/mutter/build/clutter/clutter:/var/home/swick/Projects/mutter/build/cogl/cogl-pango:/var/home/swick/Projects/mutter/build/src /var/home/swick/Projects/mutter/src/tests/meta-dbus-runner.py -- /var/home/swick/Projects/mutter/build/src/tests/cogl/conform/cogl-test-just-vertex-shader

```

Adding gdb:
```sh
[...] /var/home/swick/Projects/mutter/src/tests/meta-dbus-runner.py -- gdb --args /var/home/swick/Projects/mutter/build/src/tests/cogl/conform/cogl-test-just-vertex-shader
```

Some tests spawn a test client to exercise some functionality of the compositor. If the test client has a bug we have to help gdb out a bit. Usually we just break on the function spawning a test client (for example `subsurface_corner_cases`) and then set `follow-fork-mode child`. This can be quite annoying to do every time we re-run the test, so making use of gdb's scripting abilities can help out a lot:
```
set breakpoint pending on
break buffer_ycbcr_basic
commands 1
  set follow-fork-mode child
  break wayland_buffer_shm_allocate
  continue
end
run
```

## Graphics Debugging

Debugging GL based applications can be hard without insights into the GL state that's actually in effect. Cogl has its own set of debug options in `cogl/cogl/cogl-debug.c`, including one to show the shader sources (`COGL_DEBUG=show-source`).

For more detailed state dumps, it's possible to use graphics debuggers such as apitrace and renderdoc.

Renderdoc is usually the better tool to debug something with, but it's also harder to capture a trace. In those cases apitrace can help by capturing trace by wrapping the Mutter invocation with `apitrace trace --api=egl` and then capturing with renderdoc the replay of the apitrace (`eglretrace mutter.trace`).

## Reproducing CI test failures locally

1. Create a podman that can run gdb locally using the same image used in CI. The example below uses the tag `x86_64-2022-01-20` but this will depend on the image used by the failed CI job. The Fedora version may also differ.

    ```sh
    podman run -t -i --cap-add=SYS_PTRACE registry.gitlab.gnome.org/gnome/mutter/fedora/35:x86_64-2022-01-20 bash -l
    ```

2. Clone, build and install Mutter inside the container

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
