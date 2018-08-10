/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2018 Endless, Inc
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

#include <glib-unix.h>
#include <gio/gunixfdlist.h>

#include "meta-profiler.h"

#include "cogl/cogl-trace.h"

#define META_SYSPROF_PROFILER_DBUS_PATH "/org/gnome/Sysprof/Profiler"

struct _MetaProfiler
{
  MetaDBusSysprofCapturerSkeleton parent_instance;

  GCancellable *cancellable;
  GDBusConnection *connection;

  guint capture_timeout_id;
};

static void
meta_sysprof_capturer_init_iface (MetaDBusSysprofCapturerIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaProfiler,
                         meta_profiler,
                         META_DBUS_TYPE_SYSPROF_CAPTURER_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SYSPROF_CAPTURER,
                                                meta_sysprof_capturer_init_iface))

static gboolean
on_capture_timeout_cb (gpointer user_data)
{
  MetaProfiler *self = META_PROFILER (user_data);

  g_debug ("Stopping profiler");

  cogl_set_tracing_disabled_on_thread (g_main_context_default ());

  self->capture_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
handle_get_capabilities (MetaDBusSysprofCapturer *capturer,
                         GDBusMethodInvocation   *invocation)
{
  GVariantBuilder builder;
  GVariant *capabilities;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "Name",
                         g_variant_new_string ("Mutter"));
  g_variant_builder_add (&builder, "{sv}", "Interface-Version",
                         g_variant_new_uint32 (1));

  capabilities = g_variant_builder_end (&builder);

  meta_dbus_sysprof_capturer_complete_get_capabilities (capturer,
                                                        invocation,
                                                        capabilities);
  return TRUE;
}

static gboolean
handle_capture (MetaDBusSysprofCapturer *capturer,
                GDBusMethodInvocation   *invocation,
                GVariant                *parameters)
{
  MetaProfiler *self = META_PROFILER (capturer);
  guint timeout = 0;

  g_debug ("Starting profiler");

  g_variant_lookup (parameters, "timeout", "u", &timeout);

  if (timeout == 0)
    {
      g_autoptr (GUnixFDList) fd_list = NULL;
      g_autoptr (GVariant) fd_variant = NULL;
      g_autoptr (GError) error = NULL;
      int capture_pipe[2];
      int fd_index;

      if (!g_unix_open_pipe (capture_pipe, FD_CLOEXEC, &error))
        {
          g_critical ("Error opening pipe: %s", error->message);

          g_dbus_method_invocation_return_error (invocation,
                                                 G_IO_ERROR,
                                                 G_IO_ERROR_FAILED,
                                                 "Error opening pipe");
          return TRUE;
        }

      cogl_set_tracing_enabled_on_thread (g_main_context_default (),
                                          capture_pipe[1]);

      fd_list = g_unix_fd_list_new ();
      fd_index = g_unix_fd_list_append (fd_list, capture_pipe[0], &error);
      fd_variant = g_variant_new_handle (fd_index);

      close (capture_pipe[0]);

      g_dbus_method_invocation_return_value_with_unix_fd_list (invocation,
                                                               fd_variant,
                                                               fd_list);
      return TRUE;
    }
  else if (self->capture_timeout_id == 0)
    {
      cogl_set_tracing_enabled_on_thread (g_main_context_default (), -1);

      self->capture_timeout_id =
        g_timeout_add_seconds (timeout, on_capture_timeout_cb, self);

      g_debug ("Capturing profiling data for %u seconds", timeout);
    }

  meta_dbus_sysprof_capturer_complete_capture (capturer, invocation,
                                               g_variant_new_handle (-1));
  return TRUE;
}

static void
meta_sysprof_capturer_init_iface (MetaDBusSysprofCapturerIface *iface)
{
  iface->handle_get_capabilities = handle_get_capabilities;
  iface->handle_capture = handle_capture;
}

static void
on_bus_acquired_cb (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr (GDBusConnection) connection = NULL;
  GDBusInterfaceSkeleton *interface_skeleton;
  g_autoptr (GError) error = NULL;
  MetaProfiler *self;

  connection = g_bus_get_finish (result, &error);

  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get session bus: %s\n", error->message);
      return;
    }

  self = META_PROFILER (user_data);
  interface_skeleton = G_DBUS_INTERFACE_SKELETON (self);

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_SYSPROF_PROFILER_DBUS_PATH,
                                         &error))
    {
      g_warning ("Failed to export profiler object: %s\n", error->message);
      return;
    }

  self->connection = g_steal_pointer (&connection);
}

static void
meta_profiler_finalize (GObject *object)
{
  MetaProfiler *self = (MetaProfiler *)object;

  g_cancellable_cancel (self->cancellable);

  g_clear_handle_id (&self->capture_timeout_id, g_source_remove);
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

