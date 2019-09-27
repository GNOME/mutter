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
#include <gmodule.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#ifdef HAVE_WAYLAND
#include <wayland-server.h>

#include "../compositor/meta-window-actor-private.h"
#include "../wayland/meta-wayland-private.h"
#include "../wayland/meta-wayland-types.h"
#include "../wayland/meta-window-wayland.h"
#endif

#include "meta/meta-subprocess-launcher.h"

struct _MetaSubprocessLauncher {
    GObject parent_instance;

    GSubprocessLauncher *launcher;
    GSubprocess *subprocess;
    GCancellable *died_cancellable;
    GSubprocessFlags flags;
    gboolean process_running;
    struct wl_client *wayland_client;
    int fd_counter;
    GSList *fd_list;
};

enum
{
    FD_ELEMENT_TYPE_ENVIRONMENT,
    FD_ELEMENT_TYPE_SINGLE_PARAMETER,
    FD_ELEMENT_TYPE_DOUBLE_PARAMETER
};

struct fd_element {
    gchar *name;
    int fd;
    int type;
};

G_DEFINE_TYPE (MetaSubprocessLauncher, meta_subprocess_launcher, G_TYPE_OBJECT)

enum
{
    PROP_SUBPROCESS = 1,
    PROP_LAUNCHER_FLAGS,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static GQuark
meta_subprocess_launcher_error_quark (void)
{
    return g_quark_from_static_string ("meta-subprocess-launcher-error-quark");
}

static void free_fd_element(void *data) {
    struct fd_element *element = (struct fd_element*) data;
    g_free(element->name);
    if (element->fd >= 0)
        close(element->fd);
    g_free(element);
}

static void
meta_subprocess_launcher_dispose (GObject *gobject)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER(gobject);
    g_cancellable_cancel (self->died_cancellable);
    g_clear_object (&self->died_cancellable);
    g_clear_object(&self->launcher);
    g_clear_object(&self->subprocess);
    G_OBJECT_CLASS (meta_subprocess_launcher_parent_class)->dispose (gobject);
    g_slist_free_full(self->fd_list, free_fd_element);
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
    self->died_cancellable = NULL;
    self->process_running = FALSE;
    self->fd_counter = 3;
    self->wayland_client = NULL;
    self->fd_list = NULL;
}

static void
process_died (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    MetaSubprocessLauncher *self = META_SUBPROCESS_LAUNCHER(user_data);
    //GSubprocess *proc = G_SUBPROCESS (source);
    //g_autoptr (GError) error = NULL;

    self->process_running = FALSE;
}

static int
add_fd_to_list (MetaSubprocessLauncher  *self,
                const gchar             *variable,
                int                      type)
{
    int client_fd[2];
    struct fd_element *element = g_malloc(sizeof(struct fd_element));

    if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
        return -1;
    element->name = g_strdup(variable);
    element->fd = client_fd[1];
    element->type = type;
    self->fd_list = g_slist_prepend(self->fd_list, element);
    return client_fd[0];
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
GSubprocess *
meta_subprocess_launcher_spawnv (MetaSubprocessLauncher *self,
                                 const gchar * const    *argv,
                                 GError                **error)
{
    GPtrArray *args;
    GSList *list_element;

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
        int client_fd;
        MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

        client_fd = add_fd_to_list(self, "WAYLAND_SOCKET", FD_ELEMENT_TYPE_ENVIRONMENT);
        if (client_fd < 0)
        {
            g_set_error (error,
                         META_SUBPROCESS_LAUNCHER_ERROR,
                         META_SUBPROCESS_LAUNCHER_ERROR_NO_SOCKET_PAIR,
                         "Failed to create a socket pair for the wayland client.");
            return NULL;
        }
        self->wayland_client = wl_client_create (compositor->wayland_display,
                                                 client_fd);
    }
#endif
    args = g_ptr_array_new ();
    for(;*argv;argv++)
        g_ptr_array_add (args, g_strdup(*argv));
    for(list_element = self->fd_list; list_element != NULL; list_element = list_element->next) {
        struct fd_element *element = list_element->data;
        gchar *tmp_string;
        switch(element->type) {
        case FD_ELEMENT_TYPE_ENVIRONMENT:
            tmp_string = g_strdup_printf ("%d", self->fd_counter);
            g_subprocess_launcher_setenv (self->launcher, element->name, tmp_string, true);
            g_free(tmp_string);
            break;
        case FD_ELEMENT_TYPE_SINGLE_PARAMETER:
            tmp_string = g_strdup_printf ("%s%d", element->name, self->fd_counter);
            g_ptr_array_add (args, tmp_string);
            break;
        case FD_ELEMENT_TYPE_DOUBLE_PARAMETER:
            tmp_string = g_strdup_printf ("%d", self->fd_counter);
            g_ptr_array_add (args, g_strdup(element->name));
            g_ptr_array_add (args, tmp_string);
            break;
        }
        g_subprocess_launcher_take_fd (self->launcher, element->fd, self->fd_counter);
        self->fd_counter++;
        element->fd = -1;
    }
    g_ptr_array_add (args, NULL);

    self->subprocess = g_subprocess_launcher_spawnv(self->launcher, (const gchar * const *) args->pdata, error);
    g_ptr_array_free (args, TRUE);
    if (self->subprocess) {
        self->process_running = TRUE;
        g_object_ref(self->subprocess);
        self->died_cancellable = g_cancellable_new ();
        g_subprocess_wait_async (self->subprocess, self->died_cancellable,
                                 process_died, self);
    }
#ifdef HAVE_WAYLAND
    else
    {
        self->wayland_client = NULL;
    }
#endif
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
    if (!self->process_running) {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_SUBPROCESS_DIED,
                     "The process id dead.");
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
    return (wl_resource_get_client (surface->resource) == self->wayland_client);
#else
    // If Wayland support isn't compiled, trigger an exception
    g_set_error (error,
                 META_SUBPROCESS_LAUNCHER_ERROR,
                 META_SUBPROCESS_LAUNCHER_ERROR_NOT_WAYLAND,
                 "This isn't a Wayland window.");
    return FALSE;
#endif
}

/**
 * g_subprocess_launcher_set_environ:
 * @self: a #MetaSubprocessLauncher
 * @env: (array zero-terminated=1) (element-type filename) (transfer none):
 *     the replacement environment
 *
 * Replace the entire environment of the process that will be launched from
 * this launcher with the given 'environ' variable.
 *
 * Typically you will build this variable by using g_listenv() to copy
 * the process 'environ' and using the functions g_environ_setenv(),
 * g_environ_unsetenv(), etc.
 *
 * As an alternative, you can use meta_subprocess_launcher_setenv(),
 * meta_subprocess_launcher_unsetenv(), etc.
 *
 * Pass an empty array to set an empty environment. Pass %NULL to inherit the
 * parent process’ environment. As of GLib 2.54, the parent process’ environment
 * will be copied when meta_subprocess_launcher_set_environ() is called.
 * Previously, it was copied when the subprocess was executed. This means the
 * copied environment may now be modified (using g_subprocess_launcher_setenv(),
 * etc.) before launching the subprocess.
 *
 * On UNIX, all strings in this array can be arbitrary byte strings.
 * On Windows, they should be in UTF-8.
 *
 **/
void meta_subprocess_launcher_set_environ                    (MetaSubprocessLauncher   *self,
                                                              gchar                   **env)
{
    g_subprocess_launcher_set_environ (self->launcher, env);
}

/**
 * meta_subprocess_launcher_setenv:
 * @self: a #MetaSubprocessLauncher
 * @variable: (type filename): the environment variable to set,
 *     must not contain '='
 * @value: (type filename): the new value for the variable
 * @overwrite: whether to change the variable if it already exists
 *
 * Sets the environment variable @variable in the environment of
 * the process that will be launched from this launcher.
 *
 * On UNIX, both the variable's name and value can be arbitrary byte
 * strings, except that the variable's name cannot contain '='.
 * On Windows, they should be in UTF-8.
 *
 **/
void meta_subprocess_launcher_setenv                         (MetaSubprocessLauncher   *self,
                                                              const gchar              *variable,
                                                              const gchar              *value,
                                                              gboolean                  overwrite)
{
    g_subprocess_launcher_setenv (self->launcher, variable, value, overwrite);
}


/**
 * meta_subprocess_launcher_unsetenv:
 * @self: a #MetaSubprocessLauncher
 * @variable: (type filename): the environment variable to unset,
 *     must not contain '='
 *
 * Removes the environment variable @variable from the environment of
 * the process that will be launched from this launcher.
 *
 * On UNIX, the variable's name can be an arbitrary byte string not
 * containing '='. On Windows, it should be in UTF-8.
 *
 **/
void meta_subprocess_launcher_unsetenv                       (MetaSubprocessLauncher *self,
                                                              const gchar            *variable)
{
    g_subprocess_launcher_unsetenv (self->launcher, variable);
}

/**
 * meta_subprocess_launcher_getenv:
 * @self: a #MetaSubprocessLauncher
 * @variable: (type filename): the environment variable to get
 *
 * Returns the value of the environment variable @variable in the
 * environment of the process launched from this launcher.
 *
 * On UNIX, the returned string can be an arbitrary byte string.
 * On Windows, it will be UTF-8.
 *
 * Returns: (type filename): the value of the environment variable,
 *     %NULL if unset
 *
  **/
const gchar * meta_subprocess_launcher_getenv                (MetaSubprocessLauncher   *self,
                                                              const gchar              *variable)
{
    return g_subprocess_launcher_getenv (self->launcher, variable);
}

/**
 * meta_subprocess_launcher_set_cwd:
 * @self: a #MetaSubprocessLauncher
 * @cwd: (type filename): the cwd for the launched process
 *
 * Sets the current working directory for the process that will be
 * launched
 *
 * By default processes are launched with the current working directory
 * of the launching process at the time of launch.
 *
 **/
void meta_subprocess_launcher_set_cwd                        (MetaSubprocessLauncher   *self,
                                                              const gchar              *cwd)
{
    g_subprocess_launcher_set_cwd (self->launcher, cwd);
}

/**
 * meta_subprocess_launcher_set_flags:
 * @self: a #MetaSubprocessLauncher
 * @flags: #GSubprocessFlags
 *
 * Sets the flags on the launcher.
 *
 * The default flags are %G_SUBPROCESS_FLAGS_NONE.
 *
 * You may not set flags that specify conflicting options for how to
 * handle a particular stdio stream (eg: specifying both
 * %G_SUBPROCESS_FLAGS_STDIN_PIPE and
 * %G_SUBPROCESS_FLAGS_STDIN_INHERIT).
 *
 * You may also not set a flag that conflicts with a previous call to a
 * function like g_subprocess_launcher_set_stdin_file_path() or
 * g_subprocess_launcher_take_stdout_fd().
 *
 **/
void meta_subprocess_launcher_set_flags                      (MetaSubprocessLauncher   *self,
                                                              GSubprocessFlags          flags)
{
    g_subprocess_launcher_set_flags (self->launcher, flags);
}


/**
 * meta_subprocess_launcher_create_env_socket:
 * @self: a #MetaSubprocessLauncher
 * @variable: a name for an environment variable, used to pass the socket ID
 * @error: Error
 *
 * Creates a socket pair, returns the first one, and pass the other to the child process
 * (when launched). Also creates an environment variable with the specified name at @variable,
 * and assigns as its value the ID of the second socket.
 *
 * Returns: (transfer full): an IOStream for communicating with this socket
 **/
GIOStream *meta_subprocess_launcher_create_env_socket   (MetaSubprocessLauncher   *self,
                                                         const gchar              *variable,
                                                         GError                  **error)
{
    int client_fd;

    client_fd = add_fd_to_list(self, variable, FD_ELEMENT_TYPE_ENVIRONMENT);
    if (client_fd < 0)
    {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_NO_SOCKET_PAIR,
                     "Failed to create a socket pair for the client.");
        return 0;
    }
    return g_simple_io_stream_new(g_unix_input_stream_new(client_fd, true),
                                  g_unix_output_stream_new(client_fd, true));
}

/**
 * meta_subprocess_launcher_create_param_socket:
 * @self: a #MetaSubprocessLauncher
 * @parameter: a string for a parameter, used to pass the socket ID
 * @single: specifies wether the socket ID should be append to the parameter string, as a single parameter,
 *          or added as a new parameter after the one passed in @parameter
 * @error: Error
 *
 * Creates a socket pair, returns the first one, and pass the other to the child process
 * (when launched). Also appends a parameter with the specified name at @variable,
 * and assigns as its value the ID of the second socket.
 *
 * If @single is true, the socket ID will be append to @parameter, so it is responsibility of
 * the programmer to add any needed characters (so @parameter should be something like '-param=');
 * but if @single is false, two parameters will be added to the command line: one for @parameter,
 * and another for the socket ID (like for "--param X").
 *
 * Returns: (transfer full): an IOStream for communicating with this socket
 **/
GIOStream *meta_subprocess_launcher_create_param_socket   (MetaSubprocessLauncher   *self,
                                                           const gchar              *parameter,
                                                           const gboolean            single,
                                                           GError                  **error)
{
    int client_fd;

    client_fd = add_fd_to_list(self, parameter, single ? FD_ELEMENT_TYPE_SINGLE_PARAMETER : FD_ELEMENT_TYPE_DOUBLE_PARAMETER);
    if (client_fd < 0)
    {
        g_set_error (error,
                     META_SUBPROCESS_LAUNCHER_ERROR,
                     META_SUBPROCESS_LAUNCHER_ERROR_NO_SOCKET_PAIR,
                     "Failed to create a socket pair for the client.");
        return 0;
    }
    return g_simple_io_stream_new(g_unix_input_stream_new(client_fd, true),
                                  g_unix_output_stream_new(client_fd, true));
}