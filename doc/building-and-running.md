# Building and Running

## Building mutter

Mutter is a meson project and can be build the usual way:
```sh
meson setup builddir && meson compile -C builddir
```

There are quite a few dependencies which have to be satisfied. The easiest and risk-free way to obtain the dependencies is through the distribution repository in a [Toolbx](https://containertoolbx.org/) pet-container.
```sh
$ # on the host system:
$ toolbox create --distro fedora mutter
$ toolbox enter mutter
⬢ # inside the mutter Toolbx container:
⬢ sudo dnf builddep -y gnome-shell mutter
```

Most dependencies from the `main` branch will be satisfied like this but sometimes the `main` branch requires new or more up-to-date dependencies which must be installed manually. Those dependencies are usually also `meson` projects and can be installed into the `/usr` prefix of the `mutter` Toolbx container:
```sh
⬢ # for example, if we the need the latest gnome-desktop:
⬢ git clone https://gitlab.gnome.org/GNOME/gnome-desktop.git
⬢ cd gnome-desktop
⬢ meson setup builddir --prefix=/usr
⬢ meson compile -C builddir
⬢ sudo meson install -C
```

Note: the above should not be run outside the Toolbx container, it may make your system unusable.

## Configuring the build

When build in a Toolbx container, we can safely install Mutter into the `/usr` prefix as well:
```sh
⬢ meson configure builddir --prefix=/usr
⬢ meson compile -C builddir && sudo meson install -C
```

Like any meson project, the available build options are in the `meson_options.txt` file. The defaults are usually fine but when developing for the `native backend`, it's a good idea to turn on additional tests:
```sh
⬢ meson configure builddir -Dtty_tests=true
```

## KVM tests

The KVM tests are usually not necessary to run on your own machine and are meant mainly for CI where it's not possible to run the `tty` tests due to VKMS not being available.

To run them, a specific version of [virtme-ng](https://github.com/arighi/virtme-ng) is required
```sh
⬢ sudo dnf install python3-pip qemu
⬢ sudo pip3 install --prefix=/usr --verbose -r requirements.txt .
```

## Running the tests

Most of the test suit can be run via the usual `meson test` command:
```sh
⬢ meson test -C builddir --print-errorlogs
```

To run the `tty` tests, the `VKMS` kernel module must be loaded, and the session from which the test are invoked must be a session master. This usually means switching to another tty using `ctrl+alt+f3`, logging in, possibly entering the Toolbx container, and then invoking meson test with the `mutter/tty` suite to only run the relevant tests:
```sh
$ sudo modprobe vkms
$ toolbox enter mutter
⬢ meson test -C builddir --print-errorlogs --suite mutter/tty
```

## Updating Ref-Tests

Ref-tests compare image captures of Mutter against a reference image. Sometimes a change of the rendering result is expected with some code changes. In those cases it's required to update the reference images. This can be done by running the tests with:
```sh
MESA_LOADER_DRIVER_OVERRIDE=swrast MUTTER_REF_TEST_UPDATE='/path/to/test/case'
```

This makes sure a software renderer is being used and the reference image of the test case `/path/to/test/case` is updated. More information is available in `src/tests/meta-ref-test.c`.

## Running a nested instance

While the test suite helps to catch mistakes, there are a lot of cases where we actually need to run and interact with Mutter. The least invasive method is running a "nested" instance.
```sh
⬢ dbus-run-session mutter --wayland --nested
```

This starts a nested Mutter instance in a new dbus session with the default plugin. Often we want to run Mutter with a real plugin, such as `gnome-shell`:
```sh
⬢ dbus-run-session gnome-shell --wayland --nested
```

But sometimes running Mutter with the default plugin is preferred but there is nothing to interact with by default. We can either start something, like a terminal directly when invoking Mutter
```sh
⬢ dbus-run-session mutter --wayland --nested vte-2.91
```

or open apps on the nested compositor by setting `WAYLAND_DISPLAY` to the display of the nested session. This is usually just `wayland-1` but Mutter should print this to the terminal:
```
libmutter-Message: 21:01:37.323: Using Wayland display name 'wayland-1'
```

```sh
$ WAYLAND_DISPLAY=wayland-1 vte-2.91
```

Getting some apps to open on the desired nested compositor can sometimes be an issue. A lot of GNOME apps for example use d-bus to avoid starting multiple instances of the same app.

Changing the size of the nested session can be done with the `MUTTER_DEBUG_DUMMY_MODE_SPECS` environment variable.
```sh
⬢ MUTTER_DEBUG_DUMMY_MODE_SPECS=1920x1080 dbus-run-session mutter --wayland --nested
```

## D-Bus session

In the examples above we use `dbus-run-session` to create a nested D-Bus user session to avoid messing up the system's running D-Bus user session.

It's sometimes required to run two applications in the same nested D-Bus session. In that case, the `dbus-session.sh` script helps:
```sh
#!/bin/bash

set -euo pipefail

LIGHT_GRAY="\[\033[1m\]"
NO_COLOR="\[\033[0m\]"
export PS1="[$LIGHT_GRAY D-Bus \$(echo \$DBUS_SESSION_BUS_ADDRESS | sed -e 's/.*guid=\([a-z0-9]\{4\}\).*$/\1/') $NO_COLOR][\u@\h \W]$ "

ENV_FILE="$XDG_RUNTIME_DIR/nested-dbus-session.txt"

ACTION=${1:-}
if [[ "$ACTION" = "attach" ]]; then
  export DBUS_SESSION_BUS_ADDRESS=$(cat $ENV_FILE)
  bash -i
elif [[ "$ACTION" = "new" ]]; then
  cat > /tmp/dbussessionbashrc << __EOF__
. ~/.bashrc
echo \$DBUS_SESSION_BUS_ADDRESS > $ENV_FILE
__EOF__
  dbus-run-session -- bash --init-file /tmp/dbussessionbashrc -i
else
  echo "Usage: $0 [attach|new]"
  echo "  new .. start a new dbus session"
  echo "  attach .. to attach to a previously started dbus session"
  exit 1
fi
```

We can create a nested D-Bus user session by running
```sh
⬢ dbus-session.sh new
```

This will create a D-Bus session, and attach to it. To attach to the same session from another terminal, run
```sh
⬢ dbus-session.sh attach
```

## Remote desktop

There are limitations to the nested instance, such as keyboard shortcuts usually not getting to the nested compositor. The remote-desktop feature can help working around this.

First create a [nested D-Bus session](#d-bus-session). In this, run gnome-shell in headless mode with a virtual monitor. E.g.
```sh
⬢ dbus-session.sh new
⬢ gnome-shell --headless --virtual-monitor 1280x720
```

```sh
⬢ dbus-session.sh attach
⬢ ./build/src/gnome-remote-desktop-daemon
```

## Native

Sometimes it's necessary to run the "native backend", on real display hardware. The easiest way is to switch to a tty and run (in your Toolbx container if this is where it was installed):
```sh
⬢ dbus-run-session mutter --wayland
```

One can also run `gnome-shell` this way, and use the `dbus-session.sh` script.

## Exit

When running gnome-shell on the native backend, it's possible to exit gnome-shell by opening the "Run a Command" prompt and executing `debugexit`.

## Full session

Unfortunately sometimes none of that is enough and we need to run an entire session with our own Mutter. Some developers found success with some of the following techniques:
- Using an immutable operating system such as Fedora Silverblue and installing to `/usr`. It is possible to [temporarily make the system mutable](https://blog.sebastianwick.net/posts/silverblue-development-utils/) and then rollback when something goes wrong.
- Installing to `/usr/local`
- Adding a GNOME session in GDM that uses the built project through [environment variables](https://gitlab.gnome.org/GNOME/jhbuild/-/blob/master/examples/jhbuild-session?ref_type=heads).
