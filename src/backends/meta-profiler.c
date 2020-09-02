/*
 * Copyright (C) 2019 Endless, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "src/backends/meta-profiler.h"
#include "src/compositor/compositor-private.h"
#include "src/core/display-private.h"

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#include "cogl/cogl.h"

#include <sysprof-capture.h>

#define META_SYSPROF_PROFILER_DBUS_PATH "/org/gnome/Sysprof3/Profiler"

struct _MetaProfiler
{
  MetaDBusSysprof3ProfilerSkeleton parent_instance;

  SysprofCaptureWriter *plugin_capture;
  char *plugin_capture_filename;

  GDBusConnection *connection;
  GCancellable *cancellable;

  gboolean running;
};

static void
meta_sysprof_capturer_init_iface (MetaDBusSysprof3ProfilerIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaProfiler,
                         meta_profiler,
                         META_DBUS_TYPE_SYSPROF3_PROFILER_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SYSPROF3_PROFILER,
                                                meta_sysprof_capturer_init_iface))

static MetaPluginManager *
get_plugin_manager (void)
{
  MetaCompositor *compositor;

  compositor = meta_display_get_compositor (meta_get_display ());
  return meta_compositor_get_plugin_manager (compositor);
}

static void
setup_plugin_capture_writer (MetaProfiler *profiler)
{
  g_autofree char *tmpname = NULL;
  int fd;

  fd = g_file_open_tmp (".mutter-sysprof-plugin-XXXXXX", &tmpname, NULL);

  if (fd == -1)
    return;

  profiler->plugin_capture = sysprof_capture_writer_new_from_fd (fd, 4096 * 4);
  profiler->plugin_capture_filename = g_steal_pointer (&tmpname);

  meta_plugin_manager_start_profiler (get_plugin_manager (),
                                      profiler->plugin_capture);
}

static void
teardown_plugin_capture_writer (MetaProfiler *profiler)
{
  SysprofCaptureReader *plugin_capture_reader = NULL;
  SysprofCaptureWriter *cogl_capture;

  if (!profiler->plugin_capture)
    return;

  meta_plugin_manager_stop_profiler (get_plugin_manager ());

  cogl_capture = cogl_acquire_capture_writer ();

  if (!cogl_capture)
    goto out;

  sysprof_capture_writer_flush (profiler->plugin_capture);

  plugin_capture_reader =
    sysprof_capture_writer_create_reader (profiler->plugin_capture);
  sysprof_capture_writer_cat (cogl_capture, plugin_capture_reader);

out:
  g_unlink (profiler->plugin_capture_filename);

  g_clear_pointer (&plugin_capture_reader, sysprof_capture_reader_unref);
  g_clear_pointer (&profiler->plugin_capture_filename, g_free);

  cogl_release_capture_writer ();
}

static gboolean
handle_start (MetaDBusSysprof3Profiler *dbus_profiler,
              GDBusMethodInvocation    *invocation,
              GUnixFDList              *fd_list,
              GVariant                 *options,
              GVariant                 *fd_variant)
{
  MetaProfiler *profiler = META_PROFILER (dbus_profiler);
  GMainContext *main_context = g_main_context_default ();
  const char *group_name;
  int position;
  int fd = -1;

  if (profiler->running)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Profiler already running");
      return TRUE;
    }

  g_variant_get (fd_variant, "h", &position);

  if (fd_list)
    fd = g_unix_fd_list_get (fd_list, position, NULL);

  /* Translators: this string will appear in Sysprof */
  group_name = _("Compositor");

  if (fd != -1)
    {
      cogl_set_tracing_enabled_on_thread_with_fd (main_context,
                                                  group_name,
                                                  fd);
    }
  else
    {
      cogl_set_tracing_enabled_on_thread (main_context,
                                          group_name,
                                          "mutter-profile.syscap");
    }

  profiler->running = TRUE;

  g_debug ("Profiler running");

  setup_plugin_capture_writer (profiler);

  meta_dbus_sysprof3_profiler_complete_start (dbus_profiler, invocation, NULL);
  return TRUE;
}

static gboolean
handle_stop (MetaDBusSysprof3Profiler *dbus_profiler,
             GDBusMethodInvocation    *invocation)
{
  MetaProfiler *profiler = META_PROFILER (dbus_profiler);

  if (!profiler->running)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Profiler not running");
      return TRUE;
    }

  teardown_plugin_capture_writer (profiler);

  cogl_set_tracing_disabled_on_thread (g_main_context_default ());
  profiler->running = FALSE;

  g_debug ("Stopping profiler");

  meta_dbus_sysprof3_profiler_complete_stop (dbus_profiler, invocation);
  return TRUE;
}

static void
meta_sysprof_capturer_init_iface (MetaDBusSysprof3ProfilerIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
}

static void
on_bus_acquired_cb (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GDBusConnection) connection = NULL;
  GDBusInterfaceSkeleton *interface_skeleton;
  g_autoptr (GError) error = NULL;
  MetaProfiler *profiler;

  connection = g_bus_get_finish (result, &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get session bus: %s", error->message);
      return;
    }

  profiler = META_PROFILER (user_data);
  interface_skeleton = G_DBUS_INTERFACE_SKELETON (profiler);

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_SYSPROF_PROFILER_DBUS_PATH,
                                         &error))
    {
      g_warning ("Failed to export profiler object: %s", error->message);
      return;
    }

  profiler->connection = g_steal_pointer (&connection);
}

static void
meta_profiler_finalize (GObject *object)
{
  MetaProfiler *self = (MetaProfiler *)object;

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->connection);

  G_OBJECT_CLASS (meta_profiler_parent_class)->finalize (object);
}

static void
meta_profiler_class_init (MetaProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_profiler_finalize;
}

static void
meta_profiler_init (MetaProfiler *self)
{
  self->cancellable = g_cancellable_new ();

  g_bus_get (G_BUS_TYPE_SESSION,
             self->cancellable,
             on_bus_acquired_cb,
             self);
}

MetaProfiler *
meta_profiler_new (void)
{
  return g_object_new (META_TYPE_PROFILER, NULL);
}
