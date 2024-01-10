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

Internally it uses a fork of Cogl, a hardware acceleration abstraction library
used to simplify usage of OpenGL pipelines, as well as a fork of Clutter, a
scene graph and user interface toolkit.

Mutter is used by, for example, GNOME Shell, the GNOME core user interface, and
by  Gala, elementary OS's window manager. It can also be run standalone, using
the  command "mutter", but just running plain mutter is only intended for
debugging purposes.

## Contributing

To contribute, open merge requests at https://gitlab.gnome.org/GNOME/mutter.

It can be useful to first look at the
[GNOME Handbook](https://handbook.gnome.org/development.html) and the
documentation and API references below first.

## Documentation

- [Coding style and conventions](doc/coding-style.md)
- [Git conventions](doc/git-conventions.md)
- [Code overview](doc/code-overview.md)
- [Building and Running](doc/building-and-running.md)
- [Debugging](doc/debugging.md)
- [Monitor configuration](doc/monitor-configuration.md)

## API Reference

- Meta: <https://mutter.gnome.org/meta/>
- Clutter: <https://mutter.gnome.org/clutter/>
- Cally: <https://mutter.gnome.org/cally/>
- Cogl: <https://mutter.gnome.org/cogl/>
- CoglPango: <https://mutter.gnome.org/cogl-pango/>
- Mtk: <https://mutter.gnome.org/mtk/>

## License

Mutter is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for detalis.

[bug-tracker]: https://gitlab.gnome.org/GNOME/mutter/issues
[license]: COPYING
