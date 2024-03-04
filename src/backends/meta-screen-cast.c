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

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-render-device.h"
#include "backends/native/meta-renderer-native-private.h"

#include "cogl/cogl-egl.h"

#include "common/meta-cogl-drm-formats.h"
#endif

#define META_SCREEN_CAST_DBUS_SERVICE "org.gnome.Mutter.ScreenCast"
#define META_SCREEN_CAST_DBUS_PATH "/org/gnome/Mutter/ScreenCast"
#define META_SCREEN_CAST_API_VERSION 4

struct _MetaScreenCast
{
  MetaDbusSessionManager parent;
};

G_DEFINE_TYPE (MetaScreenCast, meta_screen_cast,
               META_TYPE_DBUS_SESSION_MANAGER)

MetaBackend *
meta_screen_cast_get_backend (MetaScreenCast *screen_cast)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (screen_cast);

  return meta_dbus_session_manager_get_backend (session_manager);
}

gboolean
meta_screen_cast_get_preferred_modifier (MetaScreenCast  *screen_cast,
                                         CoglPixelFormat  format,
                                         GArray          *modifiers,
                                         int              width,
                                         int              height,
                                         uint64_t        *preferred_modifier)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend =
    meta_screen_cast_get_backend (screen_cast);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer =
    cogl_context_get_renderer (cogl_context);
  CoglRendererEGL *cogl_renderer_egl =
    cogl_renderer->winsys;
  MetaRendererNativeGpuData *renderer_gpu_data =
    cogl_renderer_egl->platform;
  MetaRenderDevice *render_device =
    renderer_gpu_data->render_device;
  MetaRendererNative *renderer_native =
    renderer_gpu_data->renderer_native;
  int dmabuf_fd;
  uint32_t stride;
  uint32_t offset;
  g_autoptr (GError) error = NULL;
  const MetaFormatInfo *format_info;
  gboolean use_implicit_modifier;

  g_assert (cogl_renderer_is_dma_buf_supported (cogl_renderer));

  format_info = meta_format_info_from_cogl_format (format);
  g_assert (format_info);

  while (modifiers->len > 0)
    {
      g_autoptr (MetaDrmBuffer) dmabuf = NULL;
      g_autoptr (CoglFramebuffer) fb = NULL;

      if ((modifiers->len > 1) ||
          (g_array_index (modifiers, uint64_t, 0) != DRM_FORMAT_MOD_INVALID))
        use_implicit_modifier = FALSE;
      else
        use_implicit_modifier = TRUE;

      dmabuf = meta_render_device_allocate_dma_buf (render_device,
                                                    width, height,
                                                    format_info->drm_format,
                                                    (uint64_t *) modifiers->data,
                                                    use_implicit_modifier ? 0 : modifiers->len,
                                                    META_DRM_BUFFER_FLAG_NONE,
                                                    &error);
      if (!dmabuf)
        break;

      stride = meta_drm_buffer_get_stride (dmabuf);
      offset = meta_drm_buffer_get_offset (dmabuf, 0);

      dmabuf_fd = meta_drm_buffer_export_fd (dmabuf, &error);
      if (dmabuf_fd == -1)
        break;

      if (use_implicit_modifier)
        {
          *preferred_modifier = DRM_FORMAT_MOD_INVALID;
          fb = meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                                dmabuf_fd,
                                                                width, height,
                                                                stride,
                                                                offset,
                                                                NULL,
                                                                format_info->drm_format,
                                                                &error);
        }
      else
        {
          *preferred_modifier = meta_drm_buffer_get_modifier (dmabuf);
          fb = meta_renderer_native_create_dma_buf_framebuffer (renderer_native,
                                                                dmabuf_fd,
                                                                width, height,
                                                                stride,
                                                                offset,
                                                                preferred_modifier,
                                                                format_info->drm_format,
                                                                &error);
        }
      close (dmabuf_fd);

      if (!fb)
        {
          int i;

          g_clear_error (&error);

          for (i = 0; i < modifiers->len; i++)
            {
              if (g_array_index (modifiers, uint64_t, i) == *preferred_modifier)
                {
                  g_array_remove_index (modifiers, i);
                  break;
                }
            }
        }
      else
        {
          return TRUE;
        }
    }
#endif

  g_array_set_size (modifiers, 0);
  return FALSE;
}

GArray *
meta_screen_cast_query_modifiers (MetaScreenCast  *screen_cast,
                                  CoglPixelFormat  format)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend =
    meta_screen_cast_get_backend (screen_cast);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer =
    cogl_context_get_renderer (cogl_context);
  EGLDisplay egl_display =
    cogl_egl_context_get_egl_display (cogl_context);
  MetaEgl *egl =
    meta_backend_get_egl (backend);
  EGLint num_modifiers;
  g_autofree EGLuint64KHR *all_modifiers = NULL;
  g_autofree EGLBoolean *external_only = NULL;
  g_autoptr (GError) error = NULL;
  GArray *modifiers = NULL;
  gboolean ret;
  const MetaFormatInfo *format_info;
  uint64_t modifier;
  int i;

  if (!cogl_renderer_is_dma_buf_supported (cogl_renderer))
    return g_array_new (FALSE, FALSE, sizeof (uint64_t));

  format_info = meta_format_info_from_cogl_format (format);
  g_assert (format_info);

  // TODO: query cogl_renderer for modifiers
  ret = meta_egl_query_dma_buf_modifiers (egl,
                                          egl_display,
                                          format_info->drm_format,
                                          0,
                                          NULL,
                                          NULL,
                                          &num_modifiers,
                                          &error);
  if (!ret || num_modifiers == 0)
    {
      if (error)
        g_warning ("Failed to query DMA-BUF modifiers: %s", error->message);
      goto add_implicit_modifier;
    }

  all_modifiers = g_new (uint64_t, num_modifiers);
  external_only = g_new (EGLBoolean, num_modifiers);

  ret = meta_egl_query_dma_buf_modifiers (egl,
                                          egl_display,
                                          format_info->drm_format,
                                          num_modifiers,
                                          all_modifiers,
                                          external_only,
                                          &num_modifiers,
                                          &error);
  if (!ret)
    {
      g_warning ("Failed to query DMA-BUF modifiers: %s", error->message);
      goto add_implicit_modifier;
    }

  modifiers = g_array_sized_new (FALSE, FALSE, sizeof (uint64_t),
                                 num_modifiers + 1);

  for (i = 0; i < num_modifiers; i++)
    {
      if (!external_only[i])
        {
          modifier = all_modifiers[i];
          g_array_append_vals (modifiers, &modifier, 1);
        }
    }

add_implicit_modifier:
  if (!modifiers)
    {
      g_warning ("Couldn't retrieve the supported modifiers");
      modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));
    }

  modifier = DRM_FORMAT_MOD_INVALID;
  g_array_append_vals (modifiers, &modifier, 1);

  return modifiers;
#else
  return g_array_new (FALSE, FALSE, sizeof (uint64_t));
#endif
}

CoglDmaBufHandle *
meta_screen_cast_create_dma_buf_handle (MetaScreenCast  *screen_cast,
                                        CoglPixelFormat  format,
                                        uint64_t         modifier,
                                        int              width,
                                        int              height)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend =
    meta_screen_cast_get_backend (screen_cast);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglRenderer *cogl_renderer = cogl_context_get_renderer (cogl_context);
  g_autoptr (GError) error = NULL;
  CoglDmaBufHandle *dmabuf_handle;
  int n_modifiers;

  n_modifiers = (modifier == DRM_FORMAT_MOD_INVALID) ? 0
                                                     : 1;

  dmabuf_handle = cogl_renderer_create_dma_buf (cogl_renderer,
                                                format,
                                                &modifier, n_modifiers,
                                                width, height,
                                                &error);
  return dmabuf_handle;
#else
  return NULL;
#endif
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
