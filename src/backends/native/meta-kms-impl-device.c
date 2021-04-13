/*
 * Copyright (C) 2019 Red Hat
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

#include "backends/native/meta-kms-impl-device.h"

#include <errno.h>
#include <xf86drm.h>

#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"

#include "meta-default-modes.h"
#include "meta-private-enum-types.h"

enum
{
  PROP_0,

  PROP_DEVICE,
  PROP_IMPL,
  PROP_FD,
  PROP_PATH,
  PROP_FLAGS,
  PROP_DRIVER_NAME,
  PROP_DRIVER_DESCRIPTION,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaKmsImplDevicePrivate
{
  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd;
  GSource *fd_source;
  char *path;
  MetaKmsDeviceFlag flags;

  char *driver_name;
  char *driver_description;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;

  GList *fallback_modes;
} MetaKmsImplDevicePrivate;

G_DEFINE_TYPE_WITH_CODE (MetaKmsImplDevice, meta_kms_impl_device,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaKmsImplDevice))

MetaKmsDevice *
meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->device;
}

GList *
meta_kms_impl_device_copy_connectors (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return g_list_copy (priv->connectors);
}

GList *
meta_kms_impl_device_copy_crtcs (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return g_list_copy (priv->crtcs);
}

GList *
meta_kms_impl_device_copy_planes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return g_list_copy (priv->planes);
}

GList *
meta_kms_impl_device_peek_connectors (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->connectors;
}

GList *
meta_kms_impl_device_peek_crtcs (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->crtcs;
}

GList *
meta_kms_impl_device_peek_planes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->planes;
}

const MetaKmsDeviceCaps *
meta_kms_impl_device_get_caps (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return &priv->caps;
}

GList *
meta_kms_impl_device_copy_fallback_modes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return g_list_copy (priv->fallback_modes);
}

const char *
meta_kms_impl_device_get_driver_name (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->driver_name;
}

const char *
meta_kms_impl_device_get_driver_description (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->driver_description;
}

const char *
meta_kms_impl_device_get_path (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->path;
}

gboolean
meta_kms_impl_device_dispatch (MetaKmsImplDevice  *impl_device,
                               GError            **error)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  drmEventContext drm_event_context;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  drm_event_context = (drmEventContext) { 0 };
  klass->setup_drm_event_context (impl_device, &drm_event_context);

  while (TRUE)
    {
      if (drmHandleEvent (priv->fd, &drm_event_context) != 0)
        {
          struct pollfd pfd;
          int ret;

          if (errno != EAGAIN)
            {
              g_set_error_literal (error, G_IO_ERROR,
                                   g_io_error_from_errno (errno),
                                   strerror (errno));
              return FALSE;
            }

          pfd.fd = priv->fd;
          pfd.events = POLL_IN | POLL_ERR;
          do
            {
              ret = poll (&pfd, 1, -1);
            }
          while (ret == -1 && errno == EINTR);
        }
      else
        {
          break;
        }
    }

  return TRUE;
}

static gpointer
kms_event_dispatch_in_impl (MetaKmsImpl  *impl,
                            gpointer      user_data,
                            GError      **error)
{
  MetaKmsImplDevice *impl_device = user_data;
  gboolean ret;

  ret = meta_kms_impl_device_dispatch (impl_device, error);
  return GINT_TO_POINTER (ret);
}

drmModePropertyPtr
meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                    drmModeObjectProperties *props,
                                    const char              *prop_name,
                                    int                     *out_idx)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  unsigned int i;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  for (i = 0; i < props->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (priv->fd, props->props[i]);
      if (!prop)
        continue;

      if (strcmp (prop->name, prop_name) == 0)
        {
          *out_idx = i;
          return prop;
        }

      drmModeFreeProperty (prop);
    }

  return NULL;
}

static void
init_caps (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd = priv->fd;
  uint64_t cursor_width, cursor_height;

  if (drmGetCap (fd, DRM_CAP_CURSOR_WIDTH, &cursor_width) == 0 &&
      drmGetCap (fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height) == 0)
    {
      priv->caps.has_cursor_size = TRUE;
      priv->caps.cursor_width = cursor_width;
      priv->caps.cursor_height = cursor_height;
    }
}

static void
init_crtcs (MetaKmsImplDevice *impl_device,
            drmModeRes        *drm_resources)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int idx;

  for (idx = 0; idx < drm_resources->count_crtcs; idx++)
    {
      uint32_t crtc_id;
      drmModeCrtc *drm_crtc;
      MetaKmsCrtc *crtc;
      g_autoptr (GError) error = NULL;

      crtc_id = drm_resources->crtcs[idx];
      drm_crtc = drmModeGetCrtc (priv->fd, crtc_id);
      if (!drm_crtc)
        {
          g_warning ("Failed to get CRTC %u info on '%s': %s",
                     crtc_id, priv->path, error->message);
          continue;
        }

      crtc = meta_kms_crtc_new (impl_device, drm_crtc, idx, &error);

      drmModeFreeCrtc (drm_crtc);

      if (!crtc)
        {
          g_warning ("Failed to create CRTC for %u on '%s': %s",
                     crtc_id, priv->path, error->message);
          continue;
        }

      priv->crtcs = g_list_prepend (priv->crtcs, crtc);
    }
  priv->crtcs = g_list_reverse (priv->crtcs);
}

static MetaKmsConnector *
find_existing_connector (MetaKmsImplDevice *impl_device,
                         drmModeConnector  *drm_connector)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  GList *l;

  for (l = priv->connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      if (meta_kms_connector_is_same_as (connector, drm_connector))
        return connector;
    }

  return NULL;
}

static void
update_connectors (MetaKmsImplDevice *impl_device,
                   drmModeRes        *drm_resources)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  GList *connectors = NULL;
  unsigned int i;

  for (i = 0; i < drm_resources->count_connectors; i++)
    {
      drmModeConnector *drm_connector;
      MetaKmsConnector *connector;

      drm_connector = drmModeGetConnector (priv->fd,
                                           drm_resources->connectors[i]);
      if (!drm_connector)
        continue;

      connector = find_existing_connector (impl_device, drm_connector);
      if (connector)
        connector = g_object_ref (connector);
      else
        connector = meta_kms_connector_new (impl_device, drm_connector,
                                            drm_resources);
      drmModeFreeConnector (drm_connector);

      connectors = g_list_prepend (connectors, connector);
    }

  g_list_free_full (priv->connectors, g_object_unref);
  priv->connectors = g_list_reverse (connectors);
}

static MetaKmsPlaneType
get_plane_type (MetaKmsImplDevice       *impl_device,
                drmModeObjectProperties *props)
{
  drmModePropertyPtr prop;
  int idx;

  prop = meta_kms_impl_device_find_property (impl_device, props, "type", &idx);
  if (!prop)
    return FALSE;
  drmModeFreeProperty (prop);

  switch (props->prop_values[idx])
    {
    case DRM_PLANE_TYPE_PRIMARY:
      return META_KMS_PLANE_TYPE_PRIMARY;
    case DRM_PLANE_TYPE_CURSOR:
      return META_KMS_PLANE_TYPE_CURSOR;
    case DRM_PLANE_TYPE_OVERLAY:
      return META_KMS_PLANE_TYPE_OVERLAY;
    default:
      g_warning ("Unhandled plane type %" G_GUINT64_FORMAT,
                 props->prop_values[idx]);
      return -1;
    }
}

MetaKmsPlane *
meta_kms_impl_device_add_fake_plane (MetaKmsImplDevice *impl_device,
                                     MetaKmsPlaneType   plane_type,
                                     MetaKmsCrtc       *crtc)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsPlane *plane;

  plane = meta_kms_plane_new_fake (plane_type, crtc);
  priv->planes = g_list_append (priv->planes, plane);

  return plane;
}

static MetaKmsProp *
find_prop (MetaKmsProp *props,
           int          n_props,
           const char  *name)
{
  int i;

  for (i = 0; i < n_props; i++)
    {
      MetaKmsProp *prop = &props[i];

      g_warn_if_fail (prop->name);

      if (g_strcmp0 (prop->name, name) == 0)
        return prop;
    }

  return NULL;
}

void
meta_kms_impl_device_init_prop_table (MetaKmsImplDevice *impl_device,
                                      uint32_t          *drm_props,
                                      uint64_t          *drm_prop_values,
                                      int                n_drm_props,
                                      MetaKmsProp       *props,
                                      int                n_props,
                                      gpointer           user_data)
{
  int fd;
  uint32_t i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < n_drm_props; i++)
    {
      drmModePropertyRes *drm_prop;
      MetaKmsProp *prop;

      drm_prop = drmModeGetProperty (fd, drm_props[i]);
      if (!drm_prop)
        continue;

      prop = find_prop (props, n_props, drm_prop->name);
      if (!prop)
        {
          drmModeFreeProperty (drm_prop);
          continue;
        }

      if (!(drm_prop->flags & prop->type))
        {
          g_warning ("DRM property '%s' (%u) had unexpected flags (0x%x), "
                     "ignoring",
                     drm_prop->name, drm_props[i], drm_prop->flags);
          drmModeFreeProperty (drm_prop);
          continue;
        }

      prop->prop_id = drm_props[i];

      if (prop->parse)
        {
          prop->parse (impl_device, prop,
                       drm_prop, drm_prop_values[i],
                       user_data);
        }

      drmModeFreeProperty (drm_prop);
    }
}

static void
init_planes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd = priv->fd;
  drmModePlaneRes *drm_planes;
  unsigned int i;

  drm_planes = drmModeGetPlaneResources (fd);
  if (!drm_planes)
    return;

  for (i = 0; i < drm_planes->count_planes; i++)
    {
      drmModePlane *drm_plane;
      drmModeObjectProperties *props;

      drm_plane = drmModeGetPlane (fd, drm_planes->planes[i]);
      if (!drm_plane)
        continue;

      props = drmModeObjectGetProperties (fd,
                                          drm_plane->plane_id,
                                          DRM_MODE_OBJECT_PLANE);
      if (props)
        {
          MetaKmsPlaneType plane_type;

          plane_type = get_plane_type (impl_device, props);
          if (plane_type != -1)
            {
              MetaKmsPlane *plane;

              plane = meta_kms_plane_new (plane_type,
                                          impl_device,
                                          drm_plane, props);

              priv->planes = g_list_prepend (priv->planes, plane);
            }
        }

      g_clear_pointer (&props, drmModeFreeObjectProperties);
      drmModeFreePlane (drm_plane);
    }
  priv->planes = g_list_reverse (priv->planes);
}

static void
init_fallback_modes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  GList *modes = NULL;
  int i;

  for (i = 0; i < G_N_ELEMENTS (meta_default_landscape_drm_mode_infos); i++)
    {
      MetaKmsMode *mode;

      mode = meta_kms_mode_new (impl_device,
                                &meta_default_landscape_drm_mode_infos[i],
                                META_KMS_MODE_FLAG_FALLBACK_LANDSCAPE);
      modes = g_list_prepend (modes, mode);
    }

  for (i = 0; i < G_N_ELEMENTS (meta_default_portrait_drm_mode_infos); i++)
    {
      MetaKmsMode *mode;

      mode = meta_kms_mode_new (impl_device,
                                &meta_default_portrait_drm_mode_infos[i],
                                META_KMS_MODE_FLAG_FALLBACK_PORTRAIT);
      modes = g_list_prepend (modes, mode);
    }

  priv->fallback_modes = g_list_reverse (modes);
}

void
meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  drmModeRes *drm_resources;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  meta_topic (META_DEBUG_KMS, "Updating device state for %s", priv->path);

  drm_resources = drmModeGetResources (priv->fd);
  if (!drm_resources)
    {
      g_list_free_full (priv->planes, g_object_unref);
      g_list_free_full (priv->crtcs, g_object_unref);
      g_list_free_full (priv->connectors, g_object_unref);
      priv->planes = NULL;
      priv->crtcs = NULL;
      priv->connectors = NULL;
      return;
    }

  update_connectors (impl_device, drm_resources);

  g_list_foreach (priv->crtcs, (GFunc) meta_kms_crtc_update_state,
                  NULL);
  g_list_foreach (priv->connectors, (GFunc) meta_kms_connector_update_state,
                  drm_resources);
  drmModeFreeResources (drm_resources);
}

void
meta_kms_impl_device_predict_states (MetaKmsImplDevice *impl_device,
                                     MetaKmsUpdate     *update)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  g_list_foreach (priv->crtcs, (GFunc) meta_kms_crtc_predict_state,
                  update);
  g_list_foreach (priv->connectors, (GFunc) meta_kms_connector_predict_state,
                  update);
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  return priv->fd;
}

int
meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->fd;
}

MetaKmsFeedback *
meta_kms_impl_device_process_update (MetaKmsImplDevice *impl_device,
                                     MetaKmsUpdate     *update)
{
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  return klass->process_update (impl_device, update);
}

void
meta_kms_impl_device_handle_page_flip_callback (MetaKmsImplDevice   *impl_device,
                                                MetaKmsPageFlipData *page_flip_data)
{
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  klass->handle_page_flip_callback (impl_device, page_flip_data);
}

void
meta_kms_impl_device_discard_pending_page_flips (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  klass->discard_pending_page_flips (impl_device);
}

int
meta_kms_impl_device_close (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  g_clear_pointer (&priv->fd_source, g_source_destroy);
  fd = priv->fd;
  priv->fd = -1;

  return fd;
}

static void
meta_kms_impl_device_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, priv->device);
      break;
    case PROP_IMPL:
      g_value_set_object (value, priv->impl);
      break;
    case PROP_FD:
      g_value_set_int (value, priv->fd);
      break;
    case PROP_PATH:
      g_value_set_string (value, priv->path);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, priv->flags);
      break;
    case PROP_DRIVER_NAME:
      g_value_set_string (value, priv->driver_name);
      break;
    case PROP_DRIVER_DESCRIPTION:
      g_value_set_string (value, priv->driver_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_kms_impl_device_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  switch (prop_id)
    {
    case PROP_DEVICE:
      priv->device = g_value_get_object (value);
      break;
    case PROP_IMPL:
      priv->impl = g_value_get_object (value);
      break;
    case PROP_FD:
      priv->fd = g_value_get_int (value);
      break;
    case PROP_PATH:
      priv->path = g_value_dup_string (value);
      break;
    case PROP_FLAGS:
      priv->flags = g_value_get_flags (value);
      break;
    case PROP_DRIVER_NAME:
      priv->driver_name = g_value_dup_string (value);
      break;
    case PROP_DRIVER_DESCRIPTION:
      priv->driver_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_kms_impl_device_finalize (GObject *object)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  meta_kms_impl_remove_impl_device (priv->impl, impl_device);

  g_list_free_full (priv->planes, g_object_unref);
  g_list_free_full (priv->crtcs, g_object_unref);
  g_list_free_full (priv->connectors, g_object_unref);
  g_list_free_full (priv->fallback_modes,
                    (GDestroyNotify) meta_kms_mode_free);
  g_free (priv->driver_name);
  g_free (priv->driver_description);
  g_free (priv->path);

  G_OBJECT_CLASS (meta_kms_impl_device_parent_class)->finalize (object);
}

gboolean
meta_kms_impl_device_init_mode_setting (MetaKmsImplDevice  *impl_device,
                                        GError            **error)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int ret;
  drmModeRes *drm_resources;

  ret = drmSetClientCap (priv->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to activate universal planes: %s",
                   g_strerror (-ret));
      return FALSE;
    }

  drm_resources = drmModeGetResources (priv->fd);
  if (!drm_resources)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to activate universal planes: %s",
                   g_strerror (errno));
      return FALSE;
    }

  init_caps (impl_device);

  init_crtcs (impl_device, drm_resources);
  init_planes (impl_device);

  init_fallback_modes (impl_device);

  update_connectors (impl_device, drm_resources);

  drmModeFreeResources (drm_resources);

  priv->fd_source =
    meta_kms_register_fd_in_impl (meta_kms_impl_get_kms (priv->impl), priv->fd,
                                  kms_event_dispatch_in_impl,
                                  impl_device);

  return TRUE;
}

void
meta_kms_impl_device_prepare_shutdown (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  if (klass->prepare_shutdown)
    klass->prepare_shutdown (impl_device);
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *impl_device)
{
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_kms_impl_device_get_property;
  object_class->set_property = meta_kms_impl_device_set_property;
  object_class->finalize = meta_kms_impl_device_finalize;

  obj_props[PROP_DEVICE] =
    g_param_spec_object ("device",
                         "device",
                         "MetaKmsDevice",
                         META_TYPE_KMS_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IMPL] =
    g_param_spec_object ("impl",
                         "impl",
                         "MetaKmsImpl",
                         META_TYPE_KMS_IMPL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FD] =
    g_param_spec_int ("fd",
                      "fd",
                      "DRM device file descriptor",
                      INT_MIN, INT_MAX, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);
  obj_props[PROP_PATH] =
    g_param_spec_string ("path",
                         "path",
                         "DRM device file path",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FLAGS] =
    g_param_spec_flags ("flags",
                        "flags",
                        "KMS impl device flags",
                        META_TYPE_KMS_DEVICE_FLAG,
                        META_KMS_DEVICE_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DRIVER_NAME] =
    g_param_spec_string ("driver-name",
                         "driver-name",
                         "DRM device driver name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DRIVER_DESCRIPTION] =
    g_param_spec_string ("driver-description",
                         "driver-description",
                         "DRM device driver description",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
