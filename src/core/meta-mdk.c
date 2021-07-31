/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-remote-desktop.h"
#include "backends/meta-screen-cast.h"
#include "core/meta-mdk.h"

static const char * const devkit_path =
  MUTTER_LIBEXECDIR "/mutter-devkit";

struct _MetaMdk
{
  GObject parent;

  MetaContext *context;
  char *external_wayland_display;
  char *external_x11_display;

  GSubprocess *devkit_process;
  GCancellable *devkit_process_cancellable;
};

G_DEFINE_TYPE (MetaMdk, meta_mdk, G_TYPE_OBJECT)

static void
meta_mdk_finalize (GObject *object)
{
  MetaMdk *mdk = META_MDK (object);

  g_clear_pointer (&mdk->external_wayland_display, g_free);
  g_clear_pointer (&mdk->external_x11_display, g_free);
  g_cancellable_cancel (mdk->devkit_process_cancellable);
  g_clear_object (&mdk->devkit_process_cancellable);
  g_clear_object (&mdk->devkit_process);

  G_OBJECT_CLASS (meta_mdk_parent_class)->finalize (object);
}

static void
meta_mdk_class_init (MetaMdkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_mdk_finalize;
}

static void
meta_mdk_init (MetaMdk *mdk)
{
}

static void
on_devkit_died (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  MetaMdk *mdk = META_MDK (user_data);
  MetaContext *context = meta_mdk_get_context (mdk);
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (mdk->devkit_process, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;
    }

  meta_context_terminate (context);
}

static void
maybe_launch_devkit (gpointer  dependency,
                     MetaMdk  *mdk)
{
  MetaContext *context = meta_mdk_get_context (mdk);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaScreenCast *screen_cast = meta_backend_get_screen_cast (backend);
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GError) error = NULL;

  if (!meta_remote_desktop_is_enabled (remote_desktop) ||
      !meta_screen_cast_is_enabled (screen_cast))
    return;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  if (mdk->external_wayland_display)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "WAYLAND_DISPLAY",
                                    mdk->external_wayland_display,
                                    TRUE);
    }
  else
    {
      g_subprocess_launcher_unsetenv (launcher, "WAYLAND_DISPLAY");
    }

  if (mdk->external_x11_display)
    {
      g_subprocess_launcher_setenv (launcher,
                                    "DISPLAY",
                                    mdk->external_x11_display,
                                    TRUE);
    }
  else
    {
      g_subprocess_launcher_unsetenv (launcher, "DISPLAY");
    }

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            devkit_path,
                                            NULL);
  if (!subprocess)
    {
      g_warning ("Failed to launch devkit: %s", error->message);
      return;
    }

  mdk->devkit_process = g_steal_pointer (&subprocess);

  mdk->devkit_process_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (mdk->devkit_process,
                           mdk->devkit_process_cancellable,
                           on_devkit_died,
                           mdk);
}

MetaMdk *
meta_mdk_new (MetaContext  *context,
              GError      **error)
{
  g_autoptr (MetaMdk) mdk = NULL;
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaScreenCast *screen_cast = meta_backend_get_screen_cast (backend);

  mdk = g_object_new (META_TYPE_MDK, NULL);
  mdk->context = context;
  mdk->external_wayland_display = g_strdup (getenv ("WAYLAND_DISPLAY"));
  mdk->external_x11_display = g_strdup (getenv ("DISPLAY"));

  g_signal_connect_object (remote_desktop, "enabled",
                           G_CALLBACK (maybe_launch_devkit),
                           mdk, 0);
  g_signal_connect_object (screen_cast, "enabled",
                           G_CALLBACK (maybe_launch_devkit),
                           mdk, 0);

  return g_steal_pointer (&mdk);
}

MetaContext *
meta_mdk_get_context (MetaMdk *mdk)
{
  return mdk->context;
}
