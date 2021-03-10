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

## Coding style and conventions

The coding style used is primarily the GNU flavor of the [GNOME coding
style](https://developer.gnome.org/programming-guidelines/stable/c-coding-style.html.en)
with some additions:

 - Use regular C types and `stdint.h` types instead of GLib fundamental
   types, except for `gboolean`, and `guint`/`gulong` for GSource ids and
   signal handler ids. That means e.g. `uint64_t` instead of `guint64`, `int`
   instead of `gint`, `unsigned int` instead of `guint` if unsignedness
   is of importance, `uint8_t` instead of `guchar`, and so on.

 - Try to to limit line length to 80 characters, although it's not a
   strict limit.

 - Usage of g_autofree and g_autoptr are encouraged. The style used is

    ```c
      g_autofree char *text = NULL;
      g_autoptr (MetaSomeThing) thing = NULL;

      text = g_strdup_printf ("The text: %d", a_number);
      thing = g_object_new (META_TYPE_SOME_THING,
                            "text", text,
                            NULL);
      thinger_use_thing (rocket, thing);
    ```

 - Declare variables at the top of the block they are used, but avoid
   non-trivial logic among variable declarations. Non-trivial logic can be
   getting a pointer that may be `NULL`, any kind of math, or anything
   that may have side effects.

 - Instead of boolean arguments in functions, prefer enums or flags when
   they're more expressive. The naming convention for flags is

    ```c
    typedef _MetaSomeThingFlags
    {
      META_SOME_THING_FLAG_NONE = 0,
      META_SOME_THING_FLAG_ALTER_REALITY = 1 << 0,
      META_SOME_THING_FLAG_MANIPULATE_PERCEPTION = 1 << 1,
    } MetaSomeThingFlags;
    ```

 - Use `g_new0()` etc instead of `g_slice_new0()`.

 - Initialize and assign floating point variables (i.e. `float` or
   `double`) using the form `floating_point = 3.14159` or `ratio = 2.0`.

## Git messages

Commit messages should follow the [GNOME commit message
guidelines](https://wiki.gnome.org/Git/CommitMessages). We require an URL
to either an issue or a merge request in each commit. Try to always prefix
commit subjects with a relevant topic, such as `compositor:` or
`clutter/actor:`, and it's always better to write too much in the commit
message body than too little.

## License

Mutter is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for detalis.

[bug-tracker]: https://gitlab.gnome.org/GNOME/mutter/issues
[license]: COPYING
