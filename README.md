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
used to simplify usage of OpenGL pipelines, as well as a fork af Clutter, a
scene graph and user interface toolkit.

Mutter is used by, for example, GNOME Shell, the GNOME core user interface, and
by  Gala, elementary OS's window manager. It can also be run standalone, using
the  command "mutter", but just running plain mutter is only intended for
debugging purposes.

## Contributing

To contribute, open merge requests at https://gitlab.gnome.org/GNOME/mutter.

It can be useful to look at the documentation available at the
[Wiki](https://gitlab.gnome.org/GNOME/mutter/-/wikis/home).

The API documentation is available at:
- Meta: <https://gnome.pages.gitlab.gnome.org/mutter/meta/>
- Clutter: <https://gnome.pages.gitlab.gnome.org/mutter/clutter/>
- Cally: <https://gnome.pages.gitlab.gnome.org/mutter/cally/>
- Cogl: <https://gnome.pages.gitlab.gnome.org/mutter/cogl/>
- CoglPango: <https://gnome.pages.gitlab.gnome.org/mutter/cogl-pango/>

## Coding style and conventions

See [HACKING.md](./HACKING.md).

## Git messages

Commit messages should follow the [GNOME commit message
guidelines](https://wiki.gnome.org/Git/CommitMessages). We require an URL
to either an issue or a merge request in each commit. Try to always prefix
commit subjects with a relevant topic, such as `compositor:` or
`clutter/actor:`, and it's always better to write too much in the commit
message body than too little.

## Default branch

The default development branch is `main`. If you still have a local
checkout under the old name, use:
```sh
git checkout master
git branch -m master main
git fetch
git branch --unset-upstream
git branch -u origin/main
git symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main
```

## License

Mutter is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for detalis.

[bug-tracker]: https://gitlab.gnome.org/GNOME/mutter/issues
[license]: COPYING
