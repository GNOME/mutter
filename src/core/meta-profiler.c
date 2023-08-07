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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "src/core/meta-profiler.h"

#include <glib-unix.h>
#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>

#include "cogl/cogl.h"

#define META_SYSPROF_PROFILER_DBUS_PATH "/org/gnome/Sysprof3/Profiler"

typedef struct
{
  GMainContext *main_context;
  char *name;
} ThreadInfo;

struct _MetaProfiler
{
  MetaDBusSysprof3ProfilerSkeleton parent_instance;

  GDBusConnection *connection;
  GCancellable *cancellable;

  gboolean persistent;
  gboolean running;

  GMutex mutex;
  GList *threads;
};

static void
meta_sysprof_capturer_init_iface (MetaDBusSysprof3ProfilerIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaProfiler,
                         meta_profiler,
                         META_DBUS_TYPE_SYSPROF3_PROFILER_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SYSPROF3_PROFILER,
                                                meta_sysprof_capturer_init_iface))

static void
thread_info_free (ThreadInfo *thread_info)
{
  g_free (thread_info->name);
  g_free (thread_info);
}

static ThreadInfo *
thread_info_new (GMainContext *main_context,
                 const char   *name)
{
  ThreadInfo *thread_info;

  thread_info = g_new0 (ThreadInfo, 1);
  thread_info->main_context = main_context;
  thread_info->name = g_strdup (name);

  return thread_info;
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
  g_autoptr (GError) error = NULL;
  const char *group_name;
  int position;
  int fd = -1;
  GList *l;

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
      if (!cogl_start_tracing_with_fd (fd, &error))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Failed to start: %s",
                                                 error->message);
          return G_DBUS_METHOD_INVOCATION_HANDLED;
        }
    }
  else
    {
      if (!cogl_start_tracing_with_path ("mutter-profile.syscap", &error))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Failed to start: %s",
                                                 error->message);
          return G_DBUS_METHOD_INVOCATION_HANDLED;
        }
    }

  cogl_set_tracing_enabled_on_thread (main_context, group_name);

  g_mutex_lock (&profiler->mutex);
  for (l = profiler->threads; l; l = l->next)
    {
      ThreadInfo *thread_info = l->data;
      g_autofree char *thread_group_name = NULL;

      thread_group_name = g_strdup_printf ("%s (%s)",
                                           group_name,
                                           thread_info->name);
      cogl_set_tracing_enabled_on_thread (thread_info->main_context,
                                          thread_group_name);
    }
  g_mutex_unlock (&profiler->mutex);

  profiler->running = TRUE;

  g_debug ("Profiler running");

  meta_dbus_sysprof3_profiler_complete_start (dbus_profiler, invocation, NULL);
  return TRUE;
}

static gboolean
handle_stop (MetaDBusSysprof3Profiler *dbus_profiler,
             GDBusMethodInvocation    *invocation)
{
  MetaProfiler *profiler = META_PROFILER (dbus_profiler);
  GList *l;

  if (profiler->persistent)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Can't stop persistent profiling");
      return TRUE;
    }

  if (!profiler->running)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Profiler not running");
      return TRUE;
    }

  cogl_set_tracing_disabled_on_thread (g_main_context_default ());

  g_mutex_lock (&profiler->mutex);
  for (l = profiler->threads; l; l = l->next)
    {
      ThreadInfo *thread_info = l->data;

      cogl_set_tracing_disabled_on_thread (thread_info->main_context);
    }
  g_mutex_unlock (&profiler->mutex);

  cogl_stop_tracing ();

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

  if (self->persistent)
    cogl_stop_tracing ();

  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->connection);
  g_mutex_clear (&self->mutex);
  g_list_free_full (self->threads, (GDestroyNotify) thread_info_free);

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
  g_mutex_init (&self->mutex);
  self->cancellable = g_cancellable_new ();

  g_bus_get (G_BUS_TYPE_SESSION,
             self->cancellable,
             on_bus_acquired_cb,
             self);
}

MetaProfiler *
meta_profiler_new (const char *trace_file)
{
  MetaProfiler *profiler;

  profiler = g_object_new (META_TYPE_PROFILER, NULL);

  if (trace_file)
    {
      GMainContext *main_context = g_main_context_default ();
      const char *group_name;
      g_autoptr (GError) error = NULL;

      /* Translators: this string will appear in Sysprof */
      group_name = _("Compositor");

      if (!cogl_start_tracing_with_path (trace_file, &error))
        {
          g_warning ("Failed to start persistent profiling: %s",
                     error->message);
        }
      else
        {
          cogl_set_tracing_enabled_on_thread (main_context, group_name);
          profiler->persistent = TRUE;
          profiler->running = TRUE;
        }
    }

  return profiler;
}

void
meta_profiler_register_thread (MetaProfiler *profiler,
                               GMainContext *main_context,
                               const char   *name)
{
  g_mutex_lock (&profiler->mutex);
  g_warn_if_fail (!g_list_find (profiler->threads, main_context));
  profiler->threads = g_list_prepend (profiler->threads,
                                      thread_info_new (main_context, name));
  if (profiler->running)
    cogl_set_tracing_enabled_on_thread (main_context, name);
  g_mutex_unlock (&profiler->mutex);
}

void
meta_profiler_unregister_thread (MetaProfiler *profiler,
                                 GMainContext *main_context)
{
  GList *l;

  g_mutex_lock (&profiler->mutex);
  for (l = profiler->threads; l; l = l->next)
    {
      ThreadInfo *thread_info = l->data;

      if (thread_info->main_context == main_context)
        {
          thread_info_free (thread_info);
          profiler->threads = g_list_delete_link (profiler->threads, l);
          break;
        }
    }

  if (profiler->running)
    cogl_set_tracing_disabled_on_thread (main_context);

  g_mutex_unlock (&profiler->mutex);
}
