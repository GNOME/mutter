# Mutter

Mutter is a Wayland display server and X11 window manager and compositor library.

When used as a Wayland display server, it runs on top of KMS and libinput. It
implements the compositor side of the Wayland core protocol as well as various
protocol extensions. It also has functionality related to running X11
applications using Xwayland.

When used on top of Xorg it acts as a X11 window manager and compositing manager.

It contains functionality related to, among other things, window management,
window compositing, focus tracking, workspace management, keybindings and
monitor configuration.

It also contains a fork of Cogl, a hardware acceleration abstraction library
used to simplify usage of OpenGL pipelines, as well as a fork af Clutter, a
scene graph and user interface toolkit.

It is used by GNOME Shell, the GNOME core user interface. It can also be run
standalone, using the command "mutter", but just running plain mutter is only
intended for debugging purposes.

## License

Mutter is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for detalis.

[bug-tracker]: https://gitlab.gnome.org/GNOME/mutter/issues
[license]: COPYING
