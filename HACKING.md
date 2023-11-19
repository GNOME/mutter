# Style

The coding style used is primarily the GNU flavor of the [GNOME coding
style][gnome-coding-style], with some additions described below.

## General

 * Use this code style on new code. When changing old code with a different
   code style, feel free to also adjust it to use this code style.

 * Use regular C types and `stdint.h` types instead of GLib fundamental
   types, except for `gboolean`, and `guint`/`gulong` for GSource IDs and
   signal handler IDs. That means e.g. `uint64_t` instead of `guint64`, `int`
   instead of `gint`, `unsigned int` instead of `guint` if unsignedness
   is of importance, `uint8_t` instead of `guchar`, and so on.

 * Try to to limit line length to 80 characters, although it's not a
   strict limit.

 * Usage of `g_autofree` and `g_autoptr` is encouraged. The style to use is

    ```c
    g_autofree char *text = NULL;
    g_autoptr (MetaSomeThing) thing = NULL;

    text = g_strdup_printf ("The text: %d", a_number);
    thing = g_object_new (META_TYPE_SOME_THING,
                          "text", text,
                          NULL);
    thinger_use_thing (rocket, thing);
    ```

 * Declare variables at the top of the block they are used, but avoid
   non-trivial logic among variable declarations. Non-trivial logic can be
   getting a pointer that may be `NULL`, any kind of math, or anything
   that may have side effects.

 * Instead of boolean arguments in functions, prefer enums or flags when
   they're more expressive. The naming convention for flags is

    ```c
    typedef _MetaSomeThingFlags
    {
      META_SOME_THING_FLAG_NONE = 0,
      META_SOME_THING_FLAG_ALTER_REALITY = 1 << 0,
      META_SOME_THING_FLAG_MANIPULATE_PERCEPTION = 1 << 1,
    } MetaSomeThingFlags;
    ```

 * Use `g_new0 ()` etc. instead of `g_slice_new0 ()`.

 * Initialize and assign floating point variables (i.e. `float` or
   `double`) using the form `floating_point = 3.14159` or `ratio = 2.0`.

## Naming conventions

 * For object instance pointers, use a descriptive name instead of `self`, e.g.

```c
G_DEFINE_TYPE (MetaPlaceholder, meta_placeholder, G_TYPE_OBJECT)

...

void
meta_placeholder_hold_place (MetaPlaceholder *placeholder)
{
  ...
}
```

 * When object instance pointers are pointers to non-generic implementations of
   a generalized type, the convention is to suffix the variable name with the
   sub-type name. E.g.

```c
G_DEFINE_TYPE (MetaPlaceholderWayland, meta_placeholder_wayland,
               META_TYPE_PLACEHOLDER)

...

void
meta_placeholder_wayland_get_waylandy (MetaPlaceholderWayland *placeholder_wayland)
{
  ...
}
```

## Header (.h) files

 * The return type and `*` are separated by a space.
 * Function name starts one space after the last `*`.
 * Parenthesis comes one space after the function name.

As an example, this is how functions in a header file should look like:

```c
gboolean meta_udev_is_drm_device (MetaUdev    *udev,
                                  GUdevDevice *device);

GList * meta_udev_list_drm_devices (MetaUdev  *udev,
                                    GError   **error);

MetaUdev * meta_udev_new (MetaBackendNative *backend_native);
```

## Source code

Keep functions in the following order in source files:

  1. GPL header
  2. Include header files
  3. Enums
  4. Structures
  5. Function prototypes
  6. `G_DEFINE_TYPE()`
  7. Static variables
  8. Auxiliary functions
  9. Callbacks
  10. Interface implementations
  11. Parent vfunc overrides
  12. class_init and init
  13. Public API

### Include header files

Source files should use the header include order of the following example:

* `meta-example.c`:
```c
#include "config.h"

#include "meta-example-private.h"

#include <glib-object.h>
#include <stdint.h>

#ifdef HAVE_WAYLAND
#include <wayland-server-core.h>
#endif

#include "clutter/clutter.h"
#include "backends/meta-backend-private.h"
#include "mtk/mtk.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-surface-private.h"
#endif

#include "meta-dbus-file-generated-by-gdbus-codegen.h"
```

Include paths for non-system includes should be relative to the corresponding
modules source root; i.e. `"backends/meta-backend-private.h"` for
`src/backends/` and `"clutter/clutter.h"` for `clutter/clutter/clutter.h`.

### Structures

Each structure field has a space after their type name. Structure fields aren't
aligned. For example:

```c
struct _MetaFooBar
{
  MetaFoo parent;

  MetaBar *bar;
  MetaSomething *something;
};
```

### Function Prototypes

Function prototypes must be formatted just like in header files.

### Overrides

When overriding parent class vfuncs, or implementing an interface, vfunc
overrides should be named as a composition of the current class prefix,
followed by the vfunc name. For example:


```c
static void
meta_bar_spawn_unicorn (MetaParent *parent)
{
  /* ... */
}

static void
meta_bar_dispose (GObject *object)
{
  /* ... */
}

static void
meta_bar_finalize (GObject *object)
{
  /* ... */
}

static void
meta_bar_class_init (MetaBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaParentClass *parent_class = META_PARENT_CLASS (klass);

  object_class->dispose = meta_bar_dispose;
  object_class->finalize = meta_bar_finalize;

  parent_class->spawn_unicorn = meta_bar_spawn_unicorn;
}
```

### Interface Implementations

When implementing interfaces, two groups of functions are involved: the init
function, and the overrides.

The interface init function is named after the interface type in snake case,
followed by the `_iface_init` suffix. For example:


```c
static void meta_foo_iface_init (MetaFooInterface *foo_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBar, meta_bar, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (META_TYPE_FOO,
                                                meta_foo_iface_init));
```

Then, when implementing each vfunc of the interface, follow the same pattern
of the [Overrides](###Overrides) section. Here's an example:

```c
static void
meta_bar_do_something (MetaFoo *foo)
{
  /* ... */
}

static void
meta_foo_iface_init (MetaFooInterface *foo_iface)
{
  foo_iface->do_something = meta_bar_do_something;
}
```

### Auxiliary Functions

Auxiliary functions are above every other functions to minimize the number of
function prototypes in the file. These functions often grow when factoring out
the same code between two or more functions:

```c
static void
do_something_on_data (Foo *data,
                      Bar *bar)
{
  /* ... */
}

static void
random_function (Foo *foo)
{
  do_something_on_data (foo, bar);
}

static void
another_random_function (Foo *foo)
{
  do_something_on_data (foo, bar);
}
```

Sometimes, however, auxiliary functions are created to break down otherwise
large functions - in this case, it is appropriate to keep these auxiliary
functions close to the function they are tightly related to.

Auxiliary function names must have a verb in the imperative form, and should
always perform an action over something. They usually don't have the class
prefix (`meta_`, `clutter_`, or `cogl_`). For example:

```c
static void
do_something_on_data (Foo *data,
                      Bar *bar)
{
  /* ... */
}
```

Exceptionally, when converting between types, auxiliary function names may
have the class prefix to this rule. For example:

```c
static MetaFoo *
meta_foo_from_bar (Bar *bar)
{
  /* ... */
}
```

### Callback Functions

Callback function names should have the name of the action in imperative
form. They don't have any prefix, but have a `_func` suffix. For example:

```c
static void
filter_something_func (Foo      *foo,
                       Bar      *bar,
                       gpointer  user_data)
{
  /* ... */
}
```

### Signal Callbacks

Signal callbacks generally have the signal name. They should be prefixed with
`on_`, or suffixed with `_cb`, but not both. For example:

```c
static void
on_realize (ClutterActor *actor,
            gpointer      user_data)
{
  /* ... */
}

static void
destroy_cb (ClutterActor *actor,
            gpointer      user_data)
{
  /* ... */
}
```

When the callback is named after the object that generated it, and the signal,
then passive voice is used. For example:

```c
static void
click_action_clicked_cb (ClutterClickAction *click_action,
                         ClutterActor       *actor,
                         gpointer            user_data)
{
  /* ... */
}
```

## Trace Span Naming

Trace spans should be named in C++ style, i.e.
`Namespace::Class::method(args)`. For example:

```c
static void
meta_wayland_surface_commit (MetaWaylandSurface *surface)
{
  /* ... */

  COGL_TRACE_BEGIN_SCOPED (MetaWaylandSurfaceCommit,
                           "Meta::WaylandSurface::commit()");

  /* ... */
}
```

This is to facilitate automatic name shrinking in profilers that will cut out
the least important parts of the name (args, then namespaces in order, then
class) to fit the name on screen when zoomed-out.

If you need to annotate multiple spans within a function, you can append their
name to the function name, delimited by a `#` sign. For example:

```c
void
meta_thing_do_stuff (MetaThing *thing)
{
  COGL_TRACE_BEGIN_SCOPED (OneThing, "Meta::Thing::do_stuff#one_thing()");
  /* Code that does one thing that takes time */
  COGL_TRACE_END (OneThing);

  COGL_TRACE_BEGIN_SCOPED (OtherThing, "Meta::Thing::do_stuff#other_thing()");
  /* Code that does other thing that takes time */
  COGL_TRACE_END (OtherThing);
}
```

Keeping in the entire method name helps in profiler views that don't show
parent-child relationships, i.e. a global span statistics view.

[gnome-coding-style]: https://developer.gnome.org/documentation/guidelines/programming/coding-style.html
