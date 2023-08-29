/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-screen-cast.h"

#include <pipewire/pipewire.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-remote-desktop-session.h"
#include "backends/meta-screen-cast-session.h"

#define META_SCREEN_CAST_DBUS_SERVICE "org.gnome.Mutter.ScreenCast"
#define META_SCREEN_CAST_DBUS_PATH "/org/gnome/Mutter/ScreenCast"
#define META_SCREEN_CAST_API_VERSION 4

struct _MetaScreenCast
{
  MetaDbusSessionManager parent;

  gboolean disable_dma_bufs;
};

G_DEFINE_TYPE (MetaScreenCast, meta_screen_cast,
               META_TYPE_DBUS_SESSION_MANAGER)

MetaBackend *
meta_screen_cast_get_backend (MetaScreenCast *screen_cast)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (screen_cast);

  return meta_dbus_session_manager_get_backend (session_manager);
}

void
meta_screen_cast_disable_dma_bufs (MetaScreenCast *screen_cast)
{
  screen_cast->disable_dma_bufs = TRUE;
}

CoglDmaBufHandle *
meta_screen_cast_create_dma_buf_handle (MetaScreenCast  *screen_cast,
                                        CoglPixelFormat  format,
                                        int              width,
                                        int              height)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (screen_cast);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session_manager);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer = cogl_context_get_renderer (cogl_context);
  g_autoptr (GError) error = NULL;
  CoglDmaBufHandle *dmabuf_handle;

  if (screen_cast->disable_dma_bufs)
    return NULL;

  dmabuf_handle = cogl_renderer_create_dma_buf (cogl_renderer,
                                                format,
                                                width, height,
                                                &error);
  if (!dmabuf_handle)
    {
      g_warning ("Failed to allocate DMA buffer, "
                 "disabling DMA buffer based screen casting: %s",
                 error->message);
      screen_cast->disable_dma_bufs = TRUE;
      return NULL;
    }

  return dmabuf_handle;
}

static MetaRemoteDesktopSession *
find_remote_desktop_session (MetaDbusSessionManager  *session_manager,
                             const char              *remote_desktop_session_id,
                             GError                 **error)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session_manager);
  MetaRemoteDesktop *remote_desktop = meta_backend_get_remote_desktop (backend);
  MetaDbusSessionManager *remote_desktop_session_manager =
    META_DBUS_SESSION_MANAGER (remote_desktop);
  MetaDbusSession *remote_desktop_dbus_session;

  remote_desktop_dbus_session =
    meta_dbus_session_manager_get_session (remote_desktop_session_manager,
                                           remote_desktop_session_id);
  if (!remote_desktop_dbus_session)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No remote desktop session found");
      return NULL;
    }

  return META_REMOTE_DESKTOP_SESSION (remote_desktop_dbus_session);
}

static gboolean
handle_create_session (MetaDBusScreenCast    *skeleton,
                       GDBusMethodInvocation *invocation,
                       GVariant              *properties,
                       MetaScreenCast        *screen_cast)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (screen_cast);
  char *remote_desktop_session_id = NULL;
  MetaRemoteDesktopSession *remote_desktop_session = NULL;
  MetaDbusSession *dbus_session;
  MetaScreenCastSession *session;
  g_autoptr (GError) error = NULL;
  gboolean disable_animations;
  const char *session_path;

  g_variant_lookup (properties, "remote-desktop-session-id", "s",
                    &remote_desktop_session_id);

  if (remote_desktop_session_id)
    {
      remote_desktop_session = find_remote_desktop_session (session_manager,
                                                            remote_desktop_session_id,
                                                            &error);
      if (!remote_desktop_session)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "%s", error->message);
          return G_DBUS_METHOD_INVOCATION_HANDLED;
        }
    }

  dbus_session =
    meta_dbus_session_manager_create_session (session_manager,
                                              invocation,
                                              &error,
                                              "remote-desktop-session", remote_desktop_session,
                                              NULL);
  if (!dbus_session)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_FAILED,
                                                     error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
  session = META_SCREEN_CAST_SESSION (dbus_session);

  if (g_variant_lookup (properties, "disable-animations", "b",
                        &disable_animations))
    {
      meta_screen_cast_session_set_disable_animations (session,
                                                       disable_animations);
    }
  session_path = meta_screen_cast_session_get_object_path (session);
  meta_dbus_screen_cast_complete_create_session (skeleton,
                                                 invocation,
                                                 session_path);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_screen_cast_constructed (GObject *object)
{
  MetaScreenCast *screen_cast = META_SCREEN_CAST (object);
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (screen_cast);
  GDBusInterfaceSkeleton *interface_skeleton =
    meta_dbus_session_manager_get_interface_skeleton (session_manager);
  MetaDBusScreenCast *skeleton = META_DBUS_SCREEN_CAST (interface_skeleton);

  g_signal_connect (interface_skeleton, "handle-create-session",
                    G_CALLBACK (handle_create_session), screen_cast);

  meta_dbus_screen_cast_set_version (skeleton, META_SCREEN_CAST_API_VERSION);

  G_OBJECT_CLASS (meta_screen_cast_parent_class)->constructed (object);
}

MetaScreenCast *
meta_screen_cast_new (MetaBackend *backend)
{
  MetaScreenCast *screen_cast;
  g_autoptr (MetaDBusScreenCast) skeleton = NULL;

  skeleton = meta_dbus_screen_cast_skeleton_new ();
  screen_cast =
    g_object_new (META_TYPE_SCREEN_CAST,
                  "backend", backend,
                  "service-name", META_SCREEN_CAST_DBUS_SERVICE,
                  "service-path", META_SCREEN_CAST_DBUS_PATH,
                  "session-gtype", META_TYPE_SCREEN_CAST_SESSION,
                  "interface-skeleton", skeleton,
                  NULL);

  return screen_cast;
}

static void
meta_screen_cast_init (MetaScreenCast *screen_cast)
{
  static gboolean is_pipewire_initialized = FALSE;

  if (!is_pipewire_initialized)
    {
      pw_init (NULL, NULL);
      is_pipewire_initialized = TRUE;
    }
}

static void
meta_screen_cast_class_init (MetaScreenCastClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_screen_cast_constructed;
}
