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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-kms-impl-device.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <xf86drm.h>

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-thread-private.h"

#include "meta-default-modes.h"
#include "meta-private-enum-types.h"

enum
{
  PROP_0,

  PROP_DEVICE,
  PROP_IMPL,
  PROP_PATH,
  PROP_FLAGS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _CrtcDeadline
{
  MetaKmsImplDevice *impl_device;
  MetaKmsCrtc *crtc;
  MetaKmsUpdate *pending_update;
  gboolean await_flush;
  gboolean pending_page_flip;

  struct {
    int timer_fd;
    GSource *source;
    gboolean armed;
    gboolean is_deadline_page_flip;
    int64_t expected_presentation_time_us;
    gboolean has_expected_presentation_time;
  } deadline;
} CrtcFrame;

typedef enum _MetaDeadlineTimerState
{
  META_DEADLINE_TIMER_STATE_ENABLED,
  META_DEADLINE_TIMER_STATE_DISABLED,
  META_DEADLINE_TIMER_STATE_INHIBITED,
} MetaDeadlineTimerState;

typedef struct _MetaKmsImplDevicePrivate
{
  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd_hold_count;
  MetaDeviceFile *device_file;
  GSource *fd_source;
  char *path;
  MetaKmsDeviceFlag flags;
  gboolean has_latched_fd_hold;

  char *driver_name;
  char *driver_description;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;

  GList *fallback_modes;

  GHashTable *crtc_frames;

  MetaDeadlineTimerState deadline_timer_state;

  gboolean sync_file_retrieved;
  int sync_file;
} MetaKmsImplDevicePrivate;

static void
initable_iface_init (GInitableIface *iface);

static CrtcFrame * get_crtc_frame (MetaKmsImplDevice *impl_device,
                                   MetaKmsCrtc       *latch_crtc);

G_DEFINE_TYPE_WITH_CODE (MetaKmsImplDevice, meta_kms_impl_device,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (MetaKmsImplDevice)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

MetaKmsImpl *
meta_kms_impl_device_get_impl (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return priv->impl;
}

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
  int fd;

  drmEventContext drm_event_context;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  drm_event_context = (drmEventContext) { 0 };
  klass->setup_drm_event_context (impl_device, &drm_event_context);

  fd = meta_device_file_get_fd (priv->device_file);

  while (TRUE)
    {
      if (drmHandleEvent (fd, &drm_event_context) != 0)
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

          pfd.fd = fd;
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
kms_event_dispatch_in_impl (MetaThreadImpl  *impl,
                            gpointer         user_data,
                            GError         **error)
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
  int fd;
  unsigned int i;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  fd = meta_device_file_get_fd (priv->device_file);

  for (i = 0; i < props->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (fd, props->props[i]);
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
  int fd;
  uint64_t cursor_width, cursor_height;
  uint64_t prefer_shadow;
  uint64_t uses_monotonic_clock;
  uint64_t addfb2_modifiers;

  fd = meta_device_file_get_fd (priv->device_file);
  if (drmGetCap (fd, DRM_CAP_CURSOR_WIDTH, &cursor_width) == 0 &&
      drmGetCap (fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height) == 0)
    {
      priv->caps.has_cursor_size = TRUE;
      priv->caps.cursor_width = cursor_width;
      priv->caps.cursor_height = cursor_height;
    }

  if (drmGetCap (fd, DRM_CAP_DUMB_PREFER_SHADOW, &prefer_shadow) == 0)
    {
      if (prefer_shadow)
        g_message ("Device '%s' prefers shadow buffer", priv->path);

      priv->caps.prefers_shadow_buffer = prefer_shadow;
    }

  if (drmGetCap (fd, DRM_CAP_TIMESTAMP_MONOTONIC, &uses_monotonic_clock) == 0)
    {
      priv->caps.uses_monotonic_clock = uses_monotonic_clock;
    }

  if (drmGetCap (fd, DRM_CAP_ADDFB2_MODIFIERS, &addfb2_modifiers) == 0)
    {
      priv->caps.addfb2_modifiers = (addfb2_modifiers != 0);
    }
}

static void
init_crtcs (MetaKmsImplDevice *impl_device,
            drmModeRes        *drm_resources)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int idx;
  int fd;

  fd = meta_device_file_get_fd (priv->device_file);

  for (idx = 0; idx < drm_resources->count_crtcs; idx++)
    {
      uint32_t crtc_id;
      drmModeCrtc *drm_crtc;
      MetaKmsCrtc *crtc;
      g_autoptr (GError) error = NULL;

      crtc_id = drm_resources->crtcs[idx];
      drm_crtc = drmModeGetCrtc (fd, crtc_id);
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

static MetaKmsResourceChanges
update_connectors (MetaKmsImplDevice *impl_device,
                   drmModeRes        *drm_resources,
                   uint32_t           updated_connector_id)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  g_autolist (MetaKmsConnector) connectors = NULL;
  gboolean added_connector = FALSE;
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  unsigned int i;
  int fd;

  fd = meta_device_file_get_fd (priv->device_file);

  for (i = 0; i < drm_resources->count_connectors; i++)
    {
      drmModeConnector *drm_connector;
      MetaKmsConnector *connector;

      drm_connector = drmModeGetConnector (fd, drm_resources->connectors[i]);
      if (!drm_connector)
        continue;

      connector = find_existing_connector (impl_device, drm_connector);
      if (connector)
        {
          connector = g_object_ref (connector);
          if (updated_connector_id == 0 ||
              meta_kms_connector_get_id (connector) == updated_connector_id)
            {
              changes |= meta_kms_connector_update_state_in_impl (connector,
                                                                  drm_resources,
                                                                  drm_connector);
            }
        }
      else
        {
          connector = meta_kms_connector_new (impl_device, drm_connector,
                                              drm_resources);
          added_connector = TRUE;
        }

      drmModeFreeConnector (drm_connector);

      connectors = g_list_prepend (connectors, connector);
    }

  if (!added_connector &&
      g_list_length (connectors) == g_list_length (priv->connectors))
    return changes;

  g_list_free_full (priv->connectors, g_object_unref);
  priv->connectors = g_list_reverse (g_steal_pointer (&connectors));

  return META_KMS_RESOURCE_CHANGE_FULL;
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

uint64_t
meta_kms_prop_convert_value (MetaKmsProp *prop,
                             uint64_t     value)
{
  switch (prop->type)
    {
    case DRM_MODE_PROP_RANGE:
    case DRM_MODE_PROP_SIGNED_RANGE:
    case DRM_MODE_PROP_BLOB:
    case DRM_MODE_PROP_OBJECT:
      return value;
    case DRM_MODE_PROP_ENUM:
      g_assert (prop->enum_values[value].valid);
      return prop->enum_values[value].value;
    case DRM_MODE_PROP_BITMASK:
      {
        int i;
        uint64_t result = 0;

        for (i = 0; i < prop->num_enum_values; i++)
          {
            if (!prop->enum_values[i].valid)
              continue;

            if (value & prop->enum_values[i].bitmask)
              {
                result |= (1 << prop->enum_values[i].value);
                value &= ~(prop->enum_values[i].bitmask);
              }
          }

        g_assert (value == 0);
        return result;
      }
    default:
      g_assert_not_reached ();
    }

  return 0;
}

static void
update_prop_value (MetaKmsProp *prop,
                   uint64_t     drm_value)
{
  switch (prop->type)
    {
    case DRM_MODE_PROP_RANGE:
    case DRM_MODE_PROP_SIGNED_RANGE:
    case DRM_MODE_PROP_BLOB:
    case DRM_MODE_PROP_OBJECT:
      prop->value = drm_value;
      return;
    case DRM_MODE_PROP_ENUM:
      {
        int i;
        uint64_t result = prop->default_value;
        uint64_t supported = 0;

        for (i = 0; i < prop->num_enum_values; i++)
          {
            if (!prop->enum_values[i].valid)
              continue;

            if (prop->enum_values[i].value == drm_value)
              {
                result = i;
              }

            supported |= (1 << i);
          }

        prop->value = result;
        prop->supported_variants = supported;
        return;
      }
    case DRM_MODE_PROP_BITMASK:
      {
        int i;
        uint64_t result = 0;
        uint64_t supported = 0;

        for (i = 0; i < prop->num_enum_values; i++)
          {
            if (!prop->enum_values[i].valid)
              continue;

            if (drm_value & (1 << prop->enum_values[i].value))
              {
                result |= prop->enum_values[i].bitmask;
                drm_value &= ~(1 << prop->enum_values[i].value);
              }

            supported |= prop->enum_values[i].bitmask;
          }

        if (drm_value != 0)
          result |= prop->default_value;

        prop->value = result;
        prop->supported_variants = supported;
        return;
      }
    default:
      g_assert_not_reached ();
    }
}

static void
update_prop_enum_value(MetaKmsEnum        *prop_enum,
                       drmModePropertyRes *drm_prop)
{
  int i;

  for (i = 0; i < drm_prop->count_enums; i++)
    {
      if (strcmp (prop_enum->name, drm_prop->enums[i].name) == 0)
        {
          prop_enum->value = drm_prop->enums[i].value;
          prop_enum->valid = TRUE;
          return;
        }
    }

  prop_enum->valid = FALSE;
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
meta_kms_impl_device_update_prop_table (MetaKmsImplDevice *impl_device,
                                        uint32_t          *drm_props,
                                        uint64_t          *drm_prop_values,
                                        int                n_drm_props,
                                        MetaKmsProp       *props,
                                        int                n_props)
{
  int fd;
  uint32_t i, j;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < n_props; i++)
    {
      MetaKmsProp *prop = &props[i];

      prop->prop_id = 0;
      prop->value = 0;

      for (j = 0; j < prop->num_enum_values; j++)
        {
          prop->enum_values[j].valid = FALSE;
          prop->enum_values[j].value = 0;
        }
    }

  for (i = 0; i < n_drm_props; i++)
    {
      uint32_t prop_id;
      uint64_t prop_value;
      drmModePropertyRes *drm_prop;
      MetaKmsProp *prop;

      prop_id = drm_props[i];
      prop_value = drm_prop_values[i];

      drm_prop = drmModeGetProperty (fd, prop_id);
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
                     drm_prop->name, prop_id, drm_prop->flags);
          drmModeFreeProperty (drm_prop);
          continue;
        }

      prop->prop_id = prop_id;

      if (prop->type == DRM_MODE_PROP_BITMASK ||
          prop->type == DRM_MODE_PROP_ENUM)
        {
          for (j = 0; j < prop->num_enum_values; j++)
            update_prop_enum_value (&prop->enum_values[j], drm_prop);
        }

      update_prop_value (prop, prop_value);

      if (prop->type == DRM_MODE_PROP_RANGE)
        {
          if (drm_prop->count_values == 2)
            {
              prop->range_min = drm_prop->values[0];
              prop->range_max = drm_prop->values[1];
            }
          else
            {
              g_warning ("DRM property '%s' is a range with %d values, ignoring",
                         drm_prop->name, drm_prop->count_values);
            }
        }

      if (prop->type == DRM_MODE_PROP_SIGNED_RANGE)
        {
          if (drm_prop->count_values == 2)
            {
              prop->range_min_signed = (int64_t) drm_prop->values[0];
              prop->range_max_signed = (int64_t) drm_prop->values[1];
            }
          else
            {
              g_warning ("DRM property '%s' is a signed range with %d values, ignoring",
                         drm_prop->name, drm_prop->count_values);
            }
        }

      drmModeFreeProperty (drm_prop);
    }
}

static void
init_planes (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd;
  drmModePlaneRes *drm_planes;
  unsigned int i;

  fd = meta_device_file_get_fd (priv->device_file);

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

static MetaDeviceFile *
meta_kms_impl_device_open_device_file (MetaKmsImplDevice  *impl_device,
                                       const char         *path,
                                       GError            **error)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  return klass->open_device_file (impl_device, priv->path, error);
}

static gboolean
ensure_device_file (MetaKmsImplDevice  *impl_device,
                    GError            **error)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaDeviceFile *device_file;

  if (priv->device_file)
    return TRUE;

  device_file = meta_kms_impl_device_open_device_file (impl_device,
                                                       priv->path,
                                                       error);
  if (!device_file)
    return FALSE;

  priv->device_file = device_file;

  if (!(priv->flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING))
    {
      priv->fd_source =
        meta_thread_impl_register_fd (META_THREAD_IMPL (priv->impl),
                                      meta_device_file_get_fd (device_file),
                                      kms_event_dispatch_in_impl,
                                      impl_device);
      g_source_set_priority (priv->fd_source, G_PRIORITY_HIGH);
    }

  return TRUE;
}

static void
ensure_latched_fd_hold (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  if (!priv->has_latched_fd_hold)
    {
      meta_kms_impl_device_hold_fd (impl_device);
      priv->has_latched_fd_hold = TRUE;
    }
}

static void
clear_latched_fd_hold (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  if (priv->has_latched_fd_hold)
    {
      meta_kms_impl_device_unhold_fd (impl_device);
      priv->has_latched_fd_hold = FALSE;
    }
}

MetaKmsResourceChanges
meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device,
                                    uint32_t           crtc_id,
                                    uint32_t           connector_id)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  g_autoptr (GError) error = NULL;
  int fd;
  drmModeRes *drm_resources;
  MetaKmsResourceChanges changes;
  GList *l;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  meta_topic (META_DEBUG_KMS, "Updating device state for %s", priv->path);

  if (!ensure_device_file (impl_device, &error))
    {
      g_warning ("Failed to reopen '%s': %s", priv->path, error->message);
      goto err;
    }

  ensure_latched_fd_hold (impl_device);

  fd = meta_device_file_get_fd (priv->device_file);
  drm_resources = drmModeGetResources (fd);
  if (!drm_resources)
    {
      meta_topic (META_DEBUG_KMS, "Device '%s' didn't return any resources",
                  priv->path);
      goto err;
    }

  changes = update_connectors (impl_device, drm_resources, connector_id);

  for (l = priv->crtcs; l; l = l->next)
    {
      MetaKmsCrtc *crtc = META_KMS_CRTC (l->data);

      if (crtc_id > 0 &&
          meta_kms_crtc_get_id (crtc) != crtc_id)
        continue;

      changes |= meta_kms_crtc_update_state_in_impl (crtc);
    }

  drmModeFreeResources (drm_resources);

  return changes;

err:
  g_clear_list (&priv->planes, g_object_unref);
  g_clear_list (&priv->crtcs, g_object_unref);
  g_clear_list (&priv->connectors, g_object_unref);
  g_clear_pointer (&priv->crtc_frames, g_hash_table_unref);

  return META_KMS_RESOURCE_CHANGE_FULL;
}

static MetaKmsResourceChanges
meta_kms_impl_device_predict_states (MetaKmsImplDevice *impl_device,
                                     MetaKmsUpdate     *update)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  GList *l;

  g_list_foreach (priv->crtcs,
                  (GFunc) meta_kms_crtc_predict_state_in_impl,
                  update);

  for (l = priv->connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      changes |= meta_kms_connector_predict_state_in_impl (connector, update);
    }

  return changes;
}

void
meta_kms_impl_device_notify_modes_set (MetaKmsImplDevice *impl_device)
{
  clear_latched_fd_hold (impl_device);
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  return meta_device_file_get_fd (priv->device_file);
}

/**
 * meta_kms_impl_device_get_signaled_sync_file:
 * @impl_device: a #MetaKmsImplDevice object
 *
 * Returns a file descriptor which references a sync_file. The file descriptor
 * must not be closed by the caller.
 *
 * Always returns the same file descriptor for the same impl_device. The
 * referenced sync_file will always be considered signaled.
 *
 * Returns a negative value if a sync_file fd couldn't be retrieved.
 */
int
meta_kms_impl_device_get_signaled_sync_file (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  if (!priv->sync_file_retrieved)
    {
      uint32_t syncobj_handle;
      int drm_fd, ret;

      priv->sync_file = -1;
      priv->sync_file_retrieved = TRUE;

      drm_fd = meta_kms_impl_device_get_fd (impl_device);
      ret = drmSyncobjCreate (drm_fd,
                              DRM_SYNCOBJ_CREATE_SIGNALED,
                              &syncobj_handle);
      if (ret < 0)
        {
          meta_topic (META_DEBUG_KMS,
                      "drmSyncobjCreate failed: %s",
                      g_strerror (errno));
          return -1;
        }

      ret = drmSyncobjExportSyncFile (drm_fd, syncobj_handle, &priv->sync_file);
      if (ret < 0)
        {
          meta_topic (META_DEBUG_KMS,
                      "drmSyncobjExportSyncFile failed: %s",
                      g_strerror (errno));
        }

      drmSyncobjDestroy (drm_fd, syncobj_handle);
    }

  return priv->sync_file;
}

static void
disarm_crtc_frame_deadline_timer (CrtcFrame *crtc_frame)
{
  struct itimerspec its = {};

  if (!crtc_frame->deadline.source)
    return;

  meta_topic (META_DEBUG_KMS, "Disarming deadline timer for crtc %u (%s)",
              meta_kms_crtc_get_id (crtc_frame->crtc),
              meta_kms_device_get_path (meta_kms_crtc_get_device (crtc_frame->crtc)));

  timerfd_settime (crtc_frame->deadline.timer_fd,
                   TFD_TIMER_ABSTIME, &its, NULL);

  crtc_frame->deadline.armed = FALSE;
}

static void
arm_crtc_frame_deadline_timer (CrtcFrame *crtc_frame,
                               int64_t    next_deadline_us,
                               int64_t    next_presentation_us)
{
  struct itimerspec its = {};
  int64_t tv_sec;
  int64_t tv_nsec;

  g_warn_if_fail (!crtc_frame->await_flush);

  if (!crtc_frame->deadline.source)
    return;

  meta_topic (META_DEBUG_KMS, "Arming deadline timer for crtc %u (%s): %ld",
              meta_kms_crtc_get_id (crtc_frame->crtc),
              meta_kms_device_get_path (meta_kms_crtc_get_device (crtc_frame->crtc)),
              next_deadline_us);

  tv_sec = us2s (next_deadline_us);
  tv_nsec = us2ns (next_deadline_us - s2us (tv_sec));

  its.it_value.tv_sec = tv_sec;
  its.it_value.tv_nsec = tv_nsec;
  timerfd_settime (crtc_frame->deadline.timer_fd,
                   TFD_TIMER_ABSTIME, &its, NULL);

  crtc_frame->deadline.expected_presentation_time_us = next_presentation_us;
  crtc_frame->deadline.has_expected_presentation_time = next_presentation_us != 0;
  crtc_frame->deadline.armed = TRUE;
}

static void
notify_crtc_frame_ready (CrtcFrame *crtc_frame)
{
  MetaKmsCrtc *crtc = crtc_frame->crtc;

  crtc_frame->pending_page_flip = FALSE;
  crtc_frame->deadline.is_deadline_page_flip = FALSE;

  if (!crtc_frame->pending_update)
    return;

  if (crtc_frame->await_flush)
    return;

  meta_kms_impl_device_schedule_process (crtc_frame->impl_device, crtc);
}

static void
crtc_page_flip_feedback_flipped (MetaKmsCrtc  *crtc,
                                 unsigned int  sequence,
                                 unsigned int  tv_sec,
                                 unsigned int  tv_usec,
                                 gpointer      user_data)
{
  CrtcFrame *crtc_frame = user_data;

  if (crtc_frame->deadline.is_deadline_page_flip &&
      meta_is_topic_enabled (META_DEBUG_KMS))
    {
      struct timeval page_flip_timeval;
      int64_t presentation_time_us;

      page_flip_timeval = (struct timeval) {
        .tv_sec = tv_sec,
        .tv_usec = tv_usec,
      };
      presentation_time_us = meta_timeval_to_microseconds (&page_flip_timeval);

      if (crtc_frame->deadline.has_expected_presentation_time)
        {
          meta_topic (META_DEBUG_KMS,
                      "Deadline page flip presentation time: %" G_GINT64_FORMAT " us, "
                      "expected %" G_GINT64_FORMAT " us "
                      "(diff: %" G_GINT64_FORMAT ")",
                      presentation_time_us,
                      crtc_frame->deadline.expected_presentation_time_us,
                      crtc_frame->deadline.expected_presentation_time_us -
                      presentation_time_us);
        }
      else
        {
          meta_topic (META_DEBUG_KMS,
                      "Deadline page flip presentation time: %" G_GINT64_FORMAT " us",
                      presentation_time_us);
        }
    }

  notify_crtc_frame_ready (crtc_frame);
}

static void
crtc_page_flip_feedback_ready (MetaKmsCrtc *crtc,
                               gpointer     user_data)
{
  CrtcFrame *crtc_frame = user_data;

  notify_crtc_frame_ready (crtc_frame);
}

static void
crtc_page_flip_feedback_mode_set_fallback (MetaKmsCrtc *crtc,
                                           gpointer     user_data)
{
  CrtcFrame *crtc_frame = user_data;

  crtc_frame->pending_page_flip = FALSE;
}

static void
crtc_page_flip_feedback_discarded (MetaKmsCrtc  *crtc,
                                   gpointer      user_data,
                                   const GError *error)
{
  CrtcFrame *crtc_frame = user_data;

  crtc_frame->pending_page_flip = FALSE;
}

static const MetaKmsPageFlipListenerVtable crtc_page_flip_listener_vtable = {
  .flipped = crtc_page_flip_feedback_flipped,
  .ready = crtc_page_flip_feedback_ready,
  .mode_set_fallback = crtc_page_flip_feedback_mode_set_fallback,
  .discarded = crtc_page_flip_feedback_discarded,
};

static void
emit_resources_changed_callback (MetaThread *thread,
                                 gpointer    user_data)
{
  MetaKmsResourceChanges changes = GPOINTER_TO_UINT (user_data);

  meta_kms_emit_resources_changed (META_KMS (thread), changes);
}

static void
queue_result_feedback (MetaKmsImplDevice *impl_device,
                       MetaKmsUpdate     *update,
                       MetaKmsFeedback   *feedback)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (priv->device);
  g_autoptr (GList) result_listeners = NULL;
  GList *l;

  result_listeners = meta_kms_update_take_result_listeners (update);
  for (l = result_listeners; l; l = l->next)
    {
      MetaKmsResultListener *listener = l->data;

      meta_kms_result_listener_set_feedback (listener, feedback);
      meta_kms_queue_result_callback (kms, listener);
    }
}

static MetaKmsFeedback *
do_process (MetaKmsImplDevice *impl_device,
            MetaKmsCrtc       *latch_crtc,
            MetaKmsUpdate     *update,
            MetaKmsUpdateFlag  flags)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (priv->device);
  MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);
  CrtcFrame *crtc_frame = NULL;
  MetaKmsFeedback *feedback;
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;

  COGL_TRACE_BEGIN_SCOPED (MetaKmsImplDeviceProcess,
                           "Meta::KmsImplDevice::do_process()");

  update = meta_kms_impl_filter_update (impl, latch_crtc, update, flags);

  if (!update || meta_kms_update_is_empty (update))
    {
      GError *error;

      error = g_error_new (META_KMS_ERROR,
                           META_KMS_ERROR_EMPTY_UPDATE,
                           "Empty update");
      feedback = meta_kms_feedback_new_failed (NULL, error);

      if (update)
        {
          queue_result_feedback (impl_device, update, feedback);
          meta_kms_update_free (update);
        }

      return feedback;
    }

  if (!(flags & META_KMS_UPDATE_FLAG_TEST_ONLY))
    {
      if (latch_crtc)
        {
          crtc_frame = get_crtc_frame (impl_device, latch_crtc);
          if (crtc_frame && crtc_frame->pending_update)
            {
              meta_kms_update_merge_from (crtc_frame->pending_update, update);
              meta_kms_update_free (update);
              update = g_steal_pointer (&crtc_frame->pending_update);
            }
        }

      if (crtc_frame)
        {
          GMainContext *thread_context =
            meta_thread_impl_get_main_context (thread_impl);

          meta_kms_update_add_page_flip_listener (update,
                                                  crtc_frame->crtc,
                                                  &crtc_page_flip_listener_vtable,
                                                  thread_context,
                                                  crtc_frame, NULL);
          crtc_frame->pending_page_flip = TRUE;
        }
    }

  feedback = klass->process_update (impl_device, update, flags);

  if (meta_kms_feedback_get_result (feedback) != META_KMS_FEEDBACK_PASSED &&
      crtc_frame)
    crtc_frame->pending_page_flip = FALSE;

  if (!(flags & META_KMS_UPDATE_FLAG_TEST_ONLY))
    changes = meta_kms_impl_device_predict_states (impl_device, update);

  queue_result_feedback (impl_device, update, feedback);

  meta_kms_update_free (update);

  if (changes != META_KMS_RESOURCE_CHANGE_NONE)
    {
      meta_kms_queue_callback (kms,
                               NULL,
                               emit_resources_changed_callback,
                               GUINT_TO_POINTER (changes), NULL);
    }
  return feedback;
}

static gpointer
crtc_frame_deadline_dispatch (MetaThreadImpl  *thread_impl,
                              gpointer         user_data,
                              GError         **error)
{
  CrtcFrame *crtc_frame = user_data;
  MetaKmsDevice *device = meta_kms_crtc_get_device (crtc_frame->crtc);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  g_autoptr (MetaKmsFeedback) feedback = NULL;
  uint64_t timer_value;
  ssize_t ret;

  ret = read (crtc_frame->deadline.timer_fd,
              &timer_value,
              sizeof (timer_value));
  if (ret == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to read from timerfd: %s", g_strerror (errno));
      return GINT_TO_POINTER (FALSE);
    }
  else if (ret != sizeof (timer_value))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to read from timerfd: unexpected size %zd", ret);
      return GINT_TO_POINTER (FALSE);
    }

  feedback = do_process (impl_device,
                         crtc_frame->crtc,
                         g_steal_pointer (&crtc_frame->pending_update),
                         META_KMS_UPDATE_FLAG_NONE);
  if (meta_kms_feedback_did_pass (feedback))
    crtc_frame->deadline.is_deadline_page_flip = TRUE;
  disarm_crtc_frame_deadline_timer (crtc_frame);

  return GINT_TO_POINTER (TRUE);
}

static void
crtc_frame_free (CrtcFrame *crtc_frame)
{
  g_clear_fd (&crtc_frame->deadline.timer_fd, NULL);
  g_clear_pointer (&crtc_frame->deadline.source, g_source_destroy);
  g_clear_pointer (&crtc_frame->pending_update, meta_kms_update_free);
  g_free (crtc_frame);
}

static CrtcFrame *
get_crtc_frame (MetaKmsImplDevice *impl_device,
                MetaKmsCrtc       *latch_crtc)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  return g_hash_table_lookup (priv->crtc_frames, latch_crtc);
}

static gboolean
is_using_deadline_timer (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  if (priv->deadline_timer_state != META_DEADLINE_TIMER_STATE_ENABLED)
    {
      return FALSE;
    }
  else
    {
      MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
      MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);

      return meta_thread_impl_is_realtime (thread_impl);
    }
}

static CrtcFrame *
ensure_crtc_frame (MetaKmsImplDevice *impl_device,
                   MetaKmsCrtc       *latch_crtc)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
  CrtcFrame *crtc_frame;

  crtc_frame = get_crtc_frame (impl_device, latch_crtc);
  if (crtc_frame)
    return crtc_frame;

  crtc_frame = g_new0 (CrtcFrame, 1);
  crtc_frame->impl_device = impl_device;
  crtc_frame->crtc = latch_crtc;
  crtc_frame->deadline.timer_fd = -1;
  crtc_frame->await_flush = TRUE;

  if (is_using_deadline_timer (impl_device))
    {
      int timer_fd;
      GSource *source;
      g_autofree char *name = NULL;

      timer_fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
      source = meta_thread_impl_register_fd (thread_impl,
                                             timer_fd,
                                             crtc_frame_deadline_dispatch,
                                             crtc_frame);

      name = g_strdup_printf ("[mutter] KMS deadline clock (crtc: %u, %s)",
                              meta_kms_crtc_get_id (latch_crtc),
                              priv->path);
      g_source_set_name (source, name);
      g_source_set_priority (source, G_PRIORITY_HIGH + 1);
      g_source_set_can_recurse (source, FALSE);
      g_source_set_ready_time (source, -1);

      crtc_frame->deadline.timer_fd = timer_fd;
      crtc_frame->deadline.source = source;

      g_source_unref (source);
    }

  g_hash_table_insert (priv->crtc_frames, latch_crtc, crtc_frame);

  return crtc_frame;
}

static void
queue_update (MetaKmsImplDevice *impl_device,
              CrtcFrame         *crtc_frame,
              MetaKmsUpdate     *update)
{
  g_assert (update);

  if (crtc_frame->pending_update)
    {
      meta_kms_update_merge_from (crtc_frame->pending_update, update);
      meta_kms_update_free (update);
    }
  else
    {
      crtc_frame->pending_update = update;
    }
}

void
meta_kms_impl_device_handle_update (MetaKmsImplDevice *impl_device,
                                    MetaKmsUpdate     *update,
                                    MetaKmsUpdateFlag  flags)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  g_autoptr (GError) error = NULL;
  MetaKmsCrtc *latch_crtc;
  CrtcFrame *crtc_frame;
  MetaKmsFeedback *feedback;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (priv->impl));

  latch_crtc = meta_kms_update_get_latch_crtc (update);
  if (!latch_crtc)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Only single-CRTC updates supported");
      goto err;
    }

  if (!priv->crtc_frames)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_CLOSED, "Shutting down");
      goto err;
    }

  if (!ensure_device_file (impl_device, &error))
    goto err;

  meta_kms_update_realize (update, impl_device);

  crtc_frame = ensure_crtc_frame (impl_device, latch_crtc);

  crtc_frame->await_flush = FALSE;

  if (crtc_frame->pending_page_flip &&
      !meta_kms_update_get_mode_sets (update))
    {
      g_assert (latch_crtc);

      meta_topic (META_DEBUG_KMS,
                  "Queuing update on CRTC %u (%s): pending page flip",
                  meta_kms_crtc_get_id (latch_crtc),
                  priv->path);

      queue_update (impl_device, crtc_frame, update);
      return;
    }

  if (crtc_frame->pending_update)
    {
      meta_kms_update_merge_from (crtc_frame->pending_update, update);
      meta_kms_update_free (update);
      update = g_steal_pointer (&crtc_frame->pending_update);
      disarm_crtc_frame_deadline_timer (crtc_frame);
    }

  meta_kms_device_handle_flush (priv->device, latch_crtc);

  feedback = do_process (impl_device, latch_crtc, update, flags);
  meta_kms_feedback_unref (feedback);
  return;

err:
  feedback = meta_kms_feedback_new_failed (NULL, g_steal_pointer (&error));
  queue_result_feedback (impl_device, update, feedback);
  meta_kms_feedback_unref (feedback);
  meta_kms_update_free (update);
}

void
meta_kms_impl_device_await_flush (MetaKmsImplDevice *impl_device,
                                  MetaKmsCrtc       *crtc)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  CrtcFrame *crtc_frame;

  meta_topic (META_DEBUG_KMS, "Awaiting flush on CRTC %u (%s)",
              meta_kms_crtc_get_id (crtc),
              priv->path);

  crtc_frame = ensure_crtc_frame (impl_device, crtc);
  crtc_frame->await_flush = TRUE;

  if (crtc_frame->deadline.armed)
    disarm_crtc_frame_deadline_timer (crtc_frame);
}

static gboolean
ensure_deadline_timer_armed (MetaKmsImplDevice  *impl_device,
                             CrtcFrame          *crtc_frame,
                             GError            **error)
{
  int64_t next_deadline_us;
  int64_t next_presentation_us;

  if (crtc_frame->deadline.armed)
    return TRUE;

  if (!meta_kms_crtc_determine_deadline (crtc_frame->crtc,
                                         &next_deadline_us,
                                         &next_presentation_us,
                                         error))
    return FALSE;

  arm_crtc_frame_deadline_timer (crtc_frame,
                                 next_deadline_us,
                                 next_presentation_us);

  return TRUE;
}

void
meta_kms_impl_device_schedule_process (MetaKmsImplDevice *impl_device,
                                       MetaKmsCrtc       *crtc)
{
  CrtcFrame *crtc_frame;
  g_autoptr (GError) error = NULL;
  MetaKmsImplDevicePrivate *priv;

  crtc_frame = ensure_crtc_frame (impl_device, crtc);

  if (crtc_frame->await_flush)
    return;

  if (!is_using_deadline_timer (impl_device))
    goto needs_flush;

  if (crtc_frame->pending_page_flip)
    return;

  if (ensure_deadline_timer_armed (impl_device, crtc_frame, &error))
    return;

  priv = meta_kms_impl_device_get_instance_private (impl_device);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
    {
      meta_topic (META_DEBUG_KMS, "Could not determine deadline: %s",
                  error->message);

      priv->deadline_timer_state = META_DEADLINE_TIMER_STATE_INHIBITED;
    }
  else
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to determine deadline: %s", error->message);

      priv->deadline_timer_state = META_DEADLINE_TIMER_STATE_DISABLED;
    }

needs_flush:
  meta_kms_device_set_needs_flush (meta_kms_crtc_get_device (crtc), crtc);
}

static MetaKmsFeedback *
process_mode_set_update (MetaKmsImplDevice *impl_device,
                         MetaKmsUpdate     *update,
                         MetaKmsUpdateFlag  flags)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImpl *kms_impl = meta_kms_impl_device_get_impl (impl_device);
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (kms_impl);
  MetaThread *thread = meta_thread_impl_get_thread (thread_impl);
  MetaKmsFeedback *feedback;
  CrtcFrame *crtc_frame;
  GList *l;
  GHashTableIter iter;

  for (l = meta_kms_update_get_mode_sets (update); l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;
      MetaKmsCrtc *crtc = mode_set->crtc;

      crtc_frame = get_crtc_frame (impl_device, crtc);
      if (!crtc_frame)
        continue;

      if (!crtc_frame->pending_update)
        continue;

      meta_kms_update_merge_from (crtc_frame->pending_update, update);
      meta_kms_update_free (update);
      update = g_steal_pointer (&crtc_frame->pending_update);
    }

  g_hash_table_iter_init (&iter, priv->crtc_frames);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &crtc_frame))
    {
      crtc_frame->deadline.is_deadline_page_flip = FALSE;
      crtc_frame->await_flush = FALSE;
      crtc_frame->pending_page_flip = FALSE;
      g_clear_pointer (&crtc_frame->pending_update, meta_kms_update_free);
      disarm_crtc_frame_deadline_timer (crtc_frame);
    }

  meta_thread_inhibit_realtime_in_impl (thread);
  feedback = do_process (impl_device, NULL, update, flags);
  meta_thread_uninhibit_realtime_in_impl (thread);

  return feedback;
}

MetaKmsFeedback *
meta_kms_impl_device_process_update (MetaKmsImplDevice *impl_device,
                                     MetaKmsUpdate     *update,
                                     MetaKmsUpdateFlag  flags)
{
  g_autoptr (GError) error = NULL;

  if (!ensure_device_file (impl_device, &error))
    {
      MetaKmsFeedback *feedback = NULL;

      feedback = meta_kms_feedback_new_failed (NULL, g_steal_pointer (&error));
      queue_result_feedback (impl_device, update, feedback);

      meta_kms_update_free (update);
      return feedback;
    }

  meta_kms_update_realize (update, impl_device);

  if (flags & META_KMS_UPDATE_FLAG_TEST_ONLY)
    {
      return do_process (impl_device,
                         meta_kms_update_get_latch_crtc (update),
                         update, flags);
    }
  else if (flags & META_KMS_UPDATE_FLAG_MODE_SET)
    {
      return process_mode_set_update (impl_device, update, flags);
    }
  else
    {
      g_assert_not_reached ();
    }
}

void
meta_kms_impl_device_disable (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImpl *kms_impl = meta_kms_impl_device_get_impl (impl_device);
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (kms_impl);
  MetaThread *thread = meta_thread_impl_get_thread (thread_impl);
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  if (!priv->device_file)
    return;

  meta_kms_impl_device_hold_fd (impl_device);
  meta_thread_inhibit_realtime_in_impl (thread);
  klass->disable (impl_device);
  meta_thread_uninhibit_realtime_in_impl (thread);
  g_list_foreach (priv->crtcs,
                  (GFunc) meta_kms_crtc_disable_in_impl, NULL);
  g_list_foreach (priv->connectors,
                  (GFunc) meta_kms_connector_disable_in_impl, NULL);
  meta_kms_impl_device_unhold_fd (impl_device);
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

void
meta_kms_impl_device_hold_fd (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (priv->device);

  meta_assert_in_kms_impl (kms);

  g_assert (priv->device_file);

  priv->fd_hold_count++;
}

static void
clear_fd_source (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  if (!priv->fd_source)
    return;

  g_source_destroy (priv->fd_source);
  g_clear_pointer (&priv->fd_source, g_source_unref);
}

void
meta_kms_impl_device_unhold_fd (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (priv->device);

  meta_assert_in_kms_impl (kms);

  g_return_if_fail (priv->fd_hold_count > 0);

  priv->fd_hold_count--;
  if (priv->fd_hold_count == 0)
    {
      g_clear_pointer (&priv->device_file, meta_device_file_release);
      clear_fd_source (impl_device);
    }
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
    case PROP_FLAGS:
      g_value_set_flags (value, priv->flags);
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
    case PROP_PATH:
      priv->path = g_value_dup_string (value);
      break;
    case PROP_FLAGS:
      priv->flags = g_value_get_flags (value);
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

  clear_latched_fd_hold (impl_device);
  g_warn_if_fail (!priv->device_file);

  g_free (priv->driver_name);
  g_free (priv->driver_description);
  g_free (priv->path);

  g_clear_fd (&priv->sync_file, NULL);

  G_OBJECT_CLASS (meta_kms_impl_device_parent_class)->finalize (object);
}

gboolean
meta_kms_impl_device_init_mode_setting (MetaKmsImplDevice  *impl_device,
                                        GError            **error)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd;
  drmModeRes *drm_resources;

  fd = meta_device_file_get_fd (priv->device_file);

  drm_resources = drmModeGetResources (fd);
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

  update_connectors (impl_device, drm_resources, 0);

  drmModeFreeResources (drm_resources);

  return TRUE;
}

void
meta_kms_impl_device_resume (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);

  if (priv->deadline_timer_state == META_DEADLINE_TIMER_STATE_INHIBITED)
    priv->deadline_timer_state = META_DEADLINE_TIMER_STATE_ENABLED;
}

void
meta_kms_impl_device_prepare_shutdown (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  MetaKmsImplDeviceClass *klass = META_KMS_IMPL_DEVICE_GET_CLASS (impl_device);

  if (klass->prepare_shutdown)
    klass->prepare_shutdown (impl_device);

  clear_fd_source (impl_device);
  g_clear_pointer (&priv->crtc_frames, g_hash_table_unref);
}

static gboolean
get_driver_info (int    fd,
                 char **name,
                 char **description)
{
  drmVersion *drm_version;

  drm_version = drmGetVersion (fd);
  if (!drm_version)
    return FALSE;

  *name = g_strndup (drm_version->name,
                     drm_version->name_len);
  *description = g_strndup (drm_version->desc,
                            drm_version->desc_len);
  drmFreeVersion (drm_version);

  return TRUE;
}

static void
maybe_disable_deadline_timer (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  static const char *deadline_timer_deny_list[] = {
    "vc4",
  };
  int i;

  for (i = 0; i < G_N_ELEMENTS (deadline_timer_deny_list); i++)
    {
      if (g_strcmp0 (deadline_timer_deny_list[i], priv->driver_name) == 0)
        {
          priv->deadline_timer_state = META_DEADLINE_TIMER_STATE_DISABLED;
          break;
        }
    }
}

static gboolean
meta_kms_impl_device_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (initable);
  MetaKmsImplDevicePrivate *priv =
    meta_kms_impl_device_get_instance_private (impl_device);
  int fd;

  if (!ensure_device_file (impl_device, error))
    return FALSE;

  ensure_latched_fd_hold (impl_device);

  g_clear_pointer (&priv->path, g_free);
  priv->path = g_strdup (meta_device_file_get_path (priv->device_file));

  fd = meta_device_file_get_fd (priv->device_file);
  if (!get_driver_info (fd, &priv->driver_name, &priv->driver_description))
    {
      priv->driver_name = g_strdup ("unknown");
      priv->driver_description = g_strdup ("Unknown");
    }

  maybe_disable_deadline_timer (impl_device);

  priv->crtc_frames =
    g_hash_table_new_full (NULL, NULL,
                           NULL, (GDestroyNotify) crtc_frame_free);

  priv->sync_file = -1;

  return TRUE;
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *impl_device)
{
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = meta_kms_impl_device_initable_init;
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_kms_impl_device_get_property;
  object_class->set_property = meta_kms_impl_device_set_property;
  object_class->finalize = meta_kms_impl_device_finalize;

  obj_props[PROP_DEVICE] =
    g_param_spec_object ("device", NULL, NULL,
                         META_TYPE_KMS_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IMPL] =
    g_param_spec_object ("impl", NULL, NULL,
                         META_TYPE_KMS_IMPL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        META_TYPE_KMS_DEVICE_FLAG,
                        META_KMS_DEVICE_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
