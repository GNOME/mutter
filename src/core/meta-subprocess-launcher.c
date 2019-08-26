/*
 * MetaSubprocessLauncher
 *
 * Copyright (C) 2019 Sergio Costas (rastersoft@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION: metasubprocess
 * @title MetaSubprocess
 * @include: gio/gsubprocess.h
 * A class that encapsulates a Gio.SubprocessLauncher, and that allows
 * to detect whether a Wayland window belongs to the process launched
 * by it.
 */

#include <config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <glib-object.h>
#include <libintl.h>
#include <stdio.h>
#include <gio/gio.h>
#include <meta/util.h>
#include <meta/meta-window-actor.h>
#include <core/window-private.h>

#ifdef HAVE_WAYLAND
#include <wayland-server.h>

#include "../compositor/meta-window-actor-private.h"
#include "../wayland/meta-wayland-private.h"
#include "../wayland/meta-wayland-types.h"
#include "../wayland/meta-window-wayland.h"
#endif

#include "meta/meta-subprocess-launcher.h"

//#define _ gettext

struct _MetaSubprocessLauncher {
    GObject parent_instance;
    GSubprocessLauncher *launcher;
    GSubprocess *subprocess;
    GSubprocessFlags flags;

    struct wl_client *wayland_client;
};

G_DEFINE_TYPE (MetaSubprocessLauncher, meta_subprocess_launcher, G_TYPE_OBJECT)

enum
{
    PROP_SUBPROCESS_LAUNCHER = 1,
    PROP_SUBPROCESS,
    PROP_LAUNCHER_FLAGS,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static GQuark
meta_subprocess_launcher_error_quark (void)
{
    return g_quark_from_static_string ("meta-subprocess-launcher-error-quark");
}

static void
meta_subprocess_launcher_dispose (GObject *gobject)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER(gobject);
    g_clear_object(&self->launcher);
    g_clear_object(&self->subprocess);
    G_OBJECT_CLASS (meta_subprocess_launcher_parent_class)->dispose (gobject);
}

static void
meta_subprocess_launcher_finalize (GObject *gobject)
{
    G_OBJECT_CLASS (meta_subprocess_launcher_parent_class)->finalize (gobject);
}

static void
meta_subprocess_launcher_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER (object);

    switch (property_id)
    {
    case PROP_SUBPROCESS_LAUNCHER:
        g_value_set_object(value, self->launcher);
        break;
    case PROP_SUBPROCESS:
        g_value_set_object(value, self->subprocess);
        break;
    default:
        /* We don't have any other property... */
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
meta_subprocess_launcher_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER (object);

    switch (property_id)
    {
    case PROP_LAUNCHER_FLAGS:
        self->flags = g_value_get_uint(value);
        break;
    default:
        /* We don't have any other property... */
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
meta_subprocess_launcher_constructed (GObject *obj)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER(obj);
    self->launcher = g_subprocess_launcher_new(self->flags);
    G_OBJECT_CLASS (meta_subprocess_launcher_parent_class)->constructed (obj);
}

static void
meta_subprocess_launcher_class_init (MetaSubprocessLauncherClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = meta_subprocess_launcher_set_property;
    object_class->get_property = meta_subprocess_launcher_get_property;

    obj_properties[PROP_SUBPROCESS_LAUNCHER] =
    g_param_spec_object ("subprocess-launcher",
                       _("SubprocessLauncher"),
                       _("The Gio.SubprocessLauncher that will be used to launch the subprocess."),
                         G_TYPE_SUBPROCESS_LAUNCHER,
                         G_PARAM_READABLE);

    obj_properties[PROP_SUBPROCESS] =
    g_param_spec_object ("subprocess",
                       _("Subprocess"),
                       _("The Gio.Subprocess launched."),
                         G_TYPE_SUBPROCESS,
                         G_PARAM_READABLE);

    obj_properties[PROP_LAUNCHER_FLAGS] =
    g_param_spec_uint ("flags",
                     _("Flags"),
                     _("The flags for Gio.SubprocessLauncher."),
                       0,
                       0xFFFFFFFF,
                       0,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (object_class,
                                       N_PROPERTIES,
                                       obj_properties);

    object_class->dispose = meta_subprocess_launcher_dispose;
    object_class->finalize = meta_subprocess_launcher_finalize;
    object_class->constructed = meta_subprocess_launcher_constructed;
}

static void
meta_subprocess_launcher_init (MetaSubprocessLauncher *self)
{
    self->launcher = NULL;
    self->subprocess = NULL;
    self->flags = 0;

    self->wayland_client = NULL;
}

/**
 * meta_subprocess_launcher_new:
 * @flags: #GSubprocessFlags
 *
 * Creates a new #MetaSubprocessLauncher.
 *
 * The launcher is created with the default options. A copy of the
 * environment of the calling process is made at the time of this call
 * and will be used as the environment that the process is launched in.
 *
 *
 */
META_EXPORT
MetaSubprocessLauncher *
meta_subprocess_launcher_new (GSubprocessFlags flags)
{
    MetaSubprocessLauncher * self = g_object_new (META_TYPE_SUBPROCESS_LAUNCHER,
                                                  "flags",
                                                  flags,
                                                  NULL);
    return self;
}

/**
 * meta_subprocess_launcher_spawnv:
 * @self: a #MetaSubprocessLauncher
 * @argv: (array zero-terminated=1) (element-type filename): Command line arguments
 * @error: Error
 *
 * Creates a #GSubprocess given a provided array of arguments.
 *
 * Returns: (transfer full): A new #GSubprocess, or %NULL on error (and @error will be set)
 **/
META_EXPORT
GSubprocess *
meta_subprocess_launcher_spawnv (MetaSubprocessLauncher *self,
                                 const gchar * const    *argv,
                                 GError                **error) {

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);
    g_return_val_if_fail (argv != NULL && argv[0] != NULL && argv[0][0] != '\0', NULL);

    if (self->subprocess != NULL) {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_ALREADY_LAUNCHED,
                     "This object already has a process running.");
        return NULL;
    }
#ifdef HAVE_WAYLAND
    if (meta_is_wayland_compositor()) {
        int client_fd[2];
        MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

        if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
        {
            g_set_error (error,
                         META_SUBPROCESS_LAUNCHER_ERROR,
                         META_SUBPROCESS_LAUNCHER_ERROR_NO_SOCKET_PAIR,
                         "Failed to create a socket pair for the wayland client.");
            return NULL;
        }
        g_subprocess_launcher_take_fd (self->launcher, client_fd[1], 3);
        g_subprocess_launcher_setenv (self->launcher, "WAYLAND_SOCKET", "3", TRUE);
        self->wayland_client = wl_client_create (compositor->wayland_display,
                                                 client_fd[0]);
    }
#endif
    self->subprocess = g_subprocess_launcher_spawnv(self->launcher, argv, error);
    g_object_ref(self->subprocess);
    return (self->subprocess);
}

/**
 * meta_subprocess_launcher_spawn:
 * @self: a #MetaSubprocessLauncher
 * @error: Error
 * @argv0: Command line arguments
 * @...: Continued arguments, %NULL terminated
 *
 * Creates a #GSubprocess given a provided varargs list of arguments.
 *
 * Returns: (transfer full): A new #GSubprocess, or %NULL on error (and @error will be set)
 **/
META_EXPORT
GSubprocess *
meta_subprocess_launcher_spawn (MetaSubprocessLauncher  *self,
                                GError                 **error,
                                const gchar             *argv0,
                                ...)
{
    GSubprocess *result;
    GPtrArray *args;
    const gchar *arg;
    va_list ap;

    g_return_val_if_fail (argv0 != NULL && argv0[0] != '\0', NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    args = g_ptr_array_new ();

    va_start (ap, argv0);
    g_ptr_array_add (args, (gchar *) argv0);
    while ((arg = va_arg (ap, const gchar *)))
        g_ptr_array_add (args, (gchar *) arg);

    g_ptr_array_add (args, NULL);
    va_end (ap);

    result = meta_subprocess_launcher_spawnv (self, (const gchar * const *) args->pdata, error);

    g_ptr_array_free (args, TRUE);

    return result;

}

/**
 * meta_subprocess_launcher_query_window_belongs_to
 * @self: a #MetaSubprocessLauncher
 * @window: a MetaWindow
 * @error: Error
 *
 * Checks whether @window belongs to the process launched from @self or not.
 * This only works under Wayland. If the window is an X11 window (no matter if
 * Wayland support is or not compiled), an exception will be triggered.
 *
 * Returns: TRUE if the window was created by this process; FALSE if not.
 */
META_EXPORT
gboolean
meta_subprocess_launcher_query_window_belongs_to(MetaSubprocessLauncher  *self,
                                                 MetaWindow              *window,
                                                 GError                 **error) {
#ifdef HAVE_WAYLAND
    if (!meta_is_wayland_compositor()) {
        // If running under X11, trigger an exception
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_NOT_WAYLAND,
                     "This isn't a Wayland window.");
        return FALSE;
    }
    if (self->subprocess == NULL) {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_SUBPROCESS_NOT_LAUNCHED,
                     "No process was launched.");
        return FALSE;
    }
    MetaWaylandSurface *surface = window->surface;
    if (surface == NULL) {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_NOT_WAYLAND,
                     "This isn't a Wayland window.");
        return FALSE;
    }
    return wl_resource_get_client (surface->resource) == self->wayland_client;
#else
    // If Wayland support isn't compiled, trigger an exception
    g_set_error (error,
                 META_SUBPROCESS_LAUNCHER_ERROR,
                 META_SUBPROCESS_LAUNCHER_ERROR_NOT_WAYLAND,
                 "This isn't a Wayland window.");
    return FALSE;
#endif
}
