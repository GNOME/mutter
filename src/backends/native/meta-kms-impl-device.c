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

#include <xf86drm.h>

#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsImplDevice
{
  GObject parent;

  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd;

  GList *crtcs;
  GList *connectors;
  GList *planes;
};

G_DEFINE_TYPE (MetaKmsImplDevice, meta_kms_impl_device, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device)
{
  return impl_device->device;
}

GList *
meta_kms_impl_device_get_connectors (MetaKmsImplDevice *impl_device)
{
  return impl_device->connectors;
}

GList *
meta_kms_impl_device_get_crtcs (MetaKmsImplDevice *impl_device)
{
  return impl_device->crtcs;
}

GList *
meta_kms_impl_device_get_planes (MetaKmsImplDevice *impl_device)
{
  return impl_device->planes;
}

drmModePropertyPtr
meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                    drmModeObjectProperties *props,
                                    const char              *prop_name,
                                    int                     *out_idx)
{
  unsigned int i;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  for (i = 0; i < props->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (impl_device->fd, props->props[i]);
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
init_crtcs (MetaKmsImplDevice *impl_device,
            drmModeRes        *drm_resources)
{
  int idx;

  for (idx = 0; idx < drm_resources->count_crtcs; idx++)
    {
      drmModeCrtc *drm_crtc;
      MetaKmsCrtc *crtc;

      drm_crtc = drmModeGetCrtc (impl_device->fd, drm_resources->crtcs[idx]);
      crtc = meta_kms_crtc_new (impl_device, drm_crtc, idx);
      drmModeFreeCrtc (drm_crtc);

      impl_device->crtcs = g_list_prepend (impl_device->crtcs, crtc);
    }
  impl_device->crtcs = g_list_reverse (impl_device->crtcs);
}

static void
init_connectors (MetaKmsImplDevice *impl_device,
                 drmModeRes        *drm_resources)
{
  unsigned int i;

  for (i = 0; i < drm_resources->count_connectors; i++)
    {
      drmModeConnector *drm_connector;
      MetaKmsConnector *connector;

      drm_connector = drmModeGetConnector (impl_device->fd,
                                           drm_resources->connectors[i]);
      connector = meta_kms_connector_new (impl_device, drm_connector,
                                          drm_resources);
      drmModeFreeConnector (drm_connector);

      impl_device->connectors = g_list_prepend (impl_device->connectors,
                                                connector);
    }
  impl_device->connectors = g_list_reverse (impl_device->connectors);
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
      g_warning ("Unhandled plane type %lu", props->prop_values[idx]);
      return -1;
    }
}

static void
init_planes (MetaKmsImplDevice *impl_device)
{
  int fd = impl_device->fd;
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

              impl_device->planes = g_list_prepend (impl_device->planes, plane);
            }
        }

      g_clear_pointer (&props, drmModeFreeObjectProperties);
      drmModeFreePlane (drm_plane);
    }
  impl_device->planes = g_list_reverse (impl_device->planes);
}

void
meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device)
{
  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  g_list_foreach (impl_device->crtcs, (GFunc) meta_kms_crtc_update_state,
                  NULL);
}

MetaKmsImplDevice *
meta_kms_impl_device_new (MetaKmsDevice *device,
                          MetaKmsImpl   *impl,
                          int            fd)
{
  MetaKmsImplDevice *impl_device;
  drmModeRes *drm_resources;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  impl_device = g_object_new (META_TYPE_KMS_IMPL_DEVICE, NULL);
  impl_device->device = device;
  impl_device->impl = impl;
  impl_device->fd = fd;

  drmSetClientCap (fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

  drm_resources = drmModeGetResources (fd);

  init_crtcs (impl_device, drm_resources);
  init_connectors (impl_device, drm_resources);
  init_planes (impl_device);

  drmModeFreeResources (drm_resources);

  return impl_device;
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  return impl_device->fd;
}

int
meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device)
{
  return impl_device->fd;
}

int
meta_kms_impl_device_close (MetaKmsImplDevice *impl_device)
{
  int fd;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  fd = impl_device->fd;
  impl_device->fd = -1;

  return fd;
}

static void
meta_kms_impl_device_finalize (GObject *object)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);

  g_list_free_full (impl_device->planes, g_object_unref);
  g_list_free_full (impl_device->crtcs, g_object_unref);
  g_list_free_full (impl_device->connectors, g_object_unref);

  G_OBJECT_CLASS (meta_kms_impl_device_parent_class)->finalize (object);
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *device)
{
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_impl_device_finalize;
}

