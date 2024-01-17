/*
 * Copyright (C) 2019-2020 Red Hat
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

#include "backends/native/meta-kms-impl-device-atomic.h"

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update-private.h"

typedef gboolean (* MetaKmsAtomicProcessFunc) (MetaKmsImplDevice  *impl_device,
                                               MetaKmsUpdate      *update,
                                               drmModeAtomicReq   *req,
                                               GArray             *blob_ids,
                                               gpointer            entry_data,
                                               gpointer            user_data,
                                               GError            **error);

struct _MetaKmsImplDeviceAtomic
{
  MetaKmsImplDevice parent;

  GHashTable *page_flip_datas;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *iface);

/*
 * Fallback while the patch updating the uAPI header has not landed.
 * Should be removed afterward.
 * Clients which do set cursor hotspot and treat the cursor plane
 * like a mouse cursor should set this property.
 */
#ifndef DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT
#define DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT	6
#endif

G_DEFINE_TYPE_WITH_CODE (MetaKmsImplDeviceAtomic, meta_kms_impl_device_atomic,
                         META_TYPE_KMS_IMPL_DEVICE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static uint32_t
store_new_blob (MetaKmsImplDevice  *impl_device,
                GArray             *blob_ids,
                const void         *data,
                size_t              size,
                GError            **error)
{
  int fd = meta_kms_impl_device_get_fd (impl_device);
  uint32_t blob_id;
  int ret;

  ret = drmModeCreatePropertyBlob (fd, data, size, &blob_id);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeCreatePropertyBlob: %s", g_strerror (-ret));
      return 0;
    }

  g_array_append_val (blob_ids, blob_id);

  return blob_id;
}

static void
release_blob_ids (MetaKmsImplDevice *impl_device,
                  GArray            *blob_ids)
{
  int fd = meta_kms_impl_device_get_fd (impl_device);
  unsigned int i;

  for (i = 0; i < blob_ids->len; i++)
    {
      uint32_t blob_id = g_array_index (blob_ids, uint32_t, i);

      drmModeDestroyPropertyBlob (fd, blob_id);
    }
}

static gboolean
add_connector_property (MetaKmsImplDevice     *impl_device,
                        MetaKmsConnector      *connector,
                        drmModeAtomicReq      *req,
                        MetaKmsConnectorProp   prop,
                        uint64_t               value,
                        GError               **error)
{
  int ret;
  uint32_t prop_id;

  prop_id = meta_kms_connector_get_prop_id (connector, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Connector property '%s' not found",
                   meta_kms_connector_get_prop_name (connector, prop));
      return FALSE;
    }

  value = meta_kms_connector_get_prop_drm_value (connector, prop, value);

  meta_topic (META_DEBUG_KMS,
              "[atomic] Setting connector %u (%s) property '%s' (%u) to %"
              G_GUINT64_FORMAT,
              meta_kms_connector_get_id (connector),
              meta_kms_impl_device_get_path (impl_device),
              meta_kms_connector_get_prop_name (connector, prop),
              meta_kms_connector_get_prop_id (connector, prop),
              value);
  ret = drmModeAtomicAddProperty (req,
                                  meta_kms_connector_get_id (connector),
                                  prop_id,
                                  value);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeAtomicAddProperty, connector: %u, prop id: %u: %s",
                   meta_kms_connector_get_id (connector),
                   prop_id,
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_connector_update (MetaKmsImplDevice  *impl_device,
                          MetaKmsUpdate      *update,
                          drmModeAtomicReq   *req,
                          GArray             *blob_ids,
                          gpointer            update_entry,
                          gpointer            user_data,
                          GError            **error)
{
  MetaKmsConnectorUpdate *connector_update = update_entry;
  MetaKmsConnector *connector = connector_update->connector;

  if (connector_update->underscanning.has_update &&
      connector_update->underscanning.is_active)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting underscanning on connector %u (%s) to "
                  "%" G_GUINT64_FORMAT "x%" G_GUINT64_FORMAT,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device),
                  connector_update->underscanning.hborder,
                  connector_update->underscanning.vborder);

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN,
                                   META_KMS_CONNECTOR_UNDERSCAN_ON,
                                   error))
        return FALSE;

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN_HBORDER,
                                   connector_update->underscanning.hborder,
                                   error))
        return FALSE;

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN_VBORDER,
                                   connector_update->underscanning.vborder,
                                   error))
        return FALSE;
    }
  else if (connector_update->underscanning.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Unsetting underscanning on connector %u (%s)",
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN,
                                   META_KMS_CONNECTOR_UNDERSCAN_OFF,
                                   error))
        return FALSE;
    }

  if (connector_update->privacy_screen.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Toggling privacy screen to %d on connector %u (%s)",
                  connector_update->privacy_screen.is_enabled,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_SW_STATE,
                                   connector_update->privacy_screen.is_enabled ?
                                     META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED :
                                     META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED,
                                   error))
        return FALSE;
    }

  if (connector_update->max_bpc.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting max BPC to %u on connector %u (%s)",
                  (unsigned int) connector_update->max_bpc.value,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_MAX_BPC,
                                   connector_update->max_bpc.value,
                                   error))
        return FALSE;
    }

  if (connector_update->colorspace.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting colorspace to %u on connector %u (%s)",
                  connector_update->colorspace.value,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_COLORSPACE,
                                   meta_output_color_space_to_drm_color_space (
                                     connector_update->colorspace.value),
                                   error))
        return FALSE;
    }

  if (connector_update->hdr.has_update)
    {
      uint32_t hdr_blob_id;

      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting HDR metadata on connector %u (%s)",
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      hdr_blob_id = 0;
      if (connector_update->hdr.value.active)
        {
          struct hdr_output_metadata metadata;

          meta_set_drm_hdr_metadata (&connector_update->hdr.value, &metadata);

          hdr_blob_id = store_new_blob (impl_device,
                                        blob_ids,
                                        &metadata,
                                        sizeof (metadata),
                                        error);
          if (!hdr_blob_id)
            return FALSE;
        }

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_HDR_OUTPUT_METADATA,
                                   hdr_blob_id,
                                   error))
        return FALSE;
    }

  if (connector_update->broadcast_rgb.has_update)
    {
      MetaOutputRGBRange rgb_range = connector_update->broadcast_rgb.value;
      uint64_t value = meta_output_rgb_range_to_drm_broadcast_rgb (rgb_range);

      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting Broadcast RGB to %u on connector %u (%s)",
                  rgb_range,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_BROADCAST_RGB,
                                   value,
                                   error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
add_crtc_property (MetaKmsImplDevice  *impl_device,
                   MetaKmsCrtc        *crtc,
                   drmModeAtomicReq   *req,
                   MetaKmsCrtcProp     prop,
                   uint64_t            value,
                   GError            **error)
{
  int ret;
  uint32_t prop_id;

  prop_id = meta_kms_crtc_get_prop_id (crtc, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "CRTC property (%s) not found",
                   meta_kms_crtc_get_prop_name (crtc, prop));
      return FALSE;
    }

  value = meta_kms_crtc_get_prop_drm_value (crtc, prop, value);

  meta_topic (META_DEBUG_KMS,
              "[atomic] Setting CRTC %u (%s) property '%s' (%u) to %"
              G_GUINT64_FORMAT,
              meta_kms_crtc_get_id (crtc),
              meta_kms_impl_device_get_path (impl_device),
              meta_kms_crtc_get_prop_name (crtc, prop),
              meta_kms_crtc_get_prop_id (crtc, prop),
              value);
  ret = drmModeAtomicAddProperty (req,
                                  meta_kms_crtc_get_id (crtc),
                                  prop_id,
                                  value);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeAtomicAddProperty, crtc: %u, prop: %s (%u): %s",
                   meta_kms_crtc_get_id (crtc),
                   meta_kms_crtc_get_prop_name (crtc, prop),
                   prop_id,
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_mode_set (MetaKmsImplDevice  *impl_device,
                  MetaKmsUpdate      *update,
                  drmModeAtomicReq   *req,
                  GArray             *blob_ids,
                  gpointer            update_entry,
                  gpointer            user_data,
                  GError            **error)
{
  MetaKmsModeSet *mode_set = update_entry;
  MetaKmsCrtc *crtc = mode_set->crtc;
  MetaKmsMode *mode;

  mode = (MetaKmsMode *) mode_set->mode;
  if (mode)
    {
      uint32_t mode_id;
      GList *l;

      mode_id = meta_kms_mode_create_blob_id (mode, error);
      if (mode_id == 0)
        return FALSE;

      g_array_append_val (blob_ids, mode_id);

      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting mode of CRTC %u (%s) to %s",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device),
                  meta_kms_mode_get_name (mode));

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_MODE_ID,
                              mode_id,
                              error))
        return FALSE;

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_ACTIVE,
                              1,
                              error))
        return FALSE;

      for (l = mode_set->connectors; l; l = l->next)
        {
          MetaKmsConnector *connector = l->data;

          if (!add_connector_property (impl_device,
                                       connector, req,
                                       META_KMS_CONNECTOR_PROP_CRTC_ID,
                                       meta_kms_crtc_get_id (crtc),
                                       error))
            return FALSE;
        }
    }
  else
    {
      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_MODE_ID,
                              0,
                              error))
        return FALSE;

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_ACTIVE,
                              0,
                              error))
        return FALSE;

      meta_topic (META_DEBUG_KMS,
                  "[atomic] Unsetting mode of (%u, %s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));
    }

  return TRUE;
}

static gboolean
add_plane_property (MetaKmsImplDevice  *impl_device,
                    MetaKmsPlane       *plane,
                    drmModeAtomicReq   *req,
                    MetaKmsPlaneProp    prop,
                    uint64_t            value,
                    GError            **error)
{
  int ret;
  uint32_t prop_id;

  prop_id = meta_kms_plane_get_prop_id (plane, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Plane property (%s) not found on %u",
                   meta_kms_plane_get_prop_name (plane, prop),
                   meta_kms_plane_get_id (plane));
      return FALSE;
    }

  value = meta_kms_plane_get_prop_drm_value (plane, prop, value);

  switch (meta_kms_plane_get_prop_internal_type (plane, prop))
    {
    case META_KMS_PROP_TYPE_RAW:
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting plane %u (%s) property '%s' (%u) to %"
                  G_GUINT64_FORMAT,
                  meta_kms_plane_get_id (plane),
                  meta_kms_impl_device_get_path (impl_device),
                  meta_kms_plane_get_prop_name (plane, prop),
                  meta_kms_plane_get_prop_id (plane, prop),
                  value);
      break;
    case META_KMS_PROP_TYPE_FIXED_16:
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting plane %u (%s) property '%s' (%u) to %.2f",
                  meta_kms_plane_get_id (plane),
                  meta_kms_impl_device_get_path (impl_device),
                  meta_kms_plane_get_prop_name (plane, prop),
                  meta_kms_plane_get_prop_id (plane, prop),
                  meta_fixed_16_to_double (value));
      break;
    }
  ret = drmModeAtomicAddProperty (req,
                                  meta_kms_plane_get_id (plane),
                                  prop_id,
                                  value);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeAtomicAddProperty, plane: %u, prop: %s (%u): %s",
                   meta_kms_plane_get_id (plane),
                   meta_kms_plane_get_prop_name (plane, prop),
                   prop_id,
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static const char *
get_plane_type_string (MetaKmsPlane *plane)
{
  switch (meta_kms_plane_get_plane_type (plane))
    {
    case META_KMS_PLANE_TYPE_PRIMARY:
      return "primary";
    case META_KMS_PLANE_TYPE_CURSOR:
      return "cursor";
    case META_KMS_PLANE_TYPE_OVERLAY:
      return "overlay";
    }

  g_assert_not_reached ();
}

static gboolean
process_plane_assignment (MetaKmsImplDevice  *impl_device,
                          MetaKmsUpdate      *update,
                          drmModeAtomicReq   *req,
                          GArray             *blob_ids,
                          gpointer            update_entry,
                          gpointer            user_data,
                          GError            **error)
{
  MetaKmsPlaneAssignment *plane_assignment = update_entry;
  MetaKmsPlane *plane = plane_assignment->plane;
  MetaDrmBuffer *buffer;
  MetaKmsFbDamage *fb_damage;
  uint32_t prop_id;

  buffer = plane_assignment->buffer;

  if (buffer && !meta_drm_buffer_ensure_fb_id (buffer, error))
    return FALSE;

  meta_topic (META_DEBUG_KMS,
              "[atomic] Assigning %s plane (%u, %s) to %u, "
              "%hdx%hd+%hd+%hd -> %dx%d+%d+%d",
              get_plane_type_string (plane),
              meta_kms_plane_get_id (plane),
              meta_kms_impl_device_get_path (impl_device),
              buffer ? meta_drm_buffer_get_fb_id (buffer) : 0,
              meta_fixed_16_to_int (plane_assignment->src_rect.width),
              meta_fixed_16_to_int (plane_assignment->src_rect.height),
              meta_fixed_16_to_int (plane_assignment->src_rect.x),
              meta_fixed_16_to_int (plane_assignment->src_rect.y),
              plane_assignment->dst_rect.width,
              plane_assignment->dst_rect.height,
              plane_assignment->dst_rect.x,
              plane_assignment->dst_rect.y);

  if (buffer)
    {
      int i;
      struct {
        MetaKmsPlaneProp prop;
        uint64_t value;
      } props[] = {
        {
          .prop = META_KMS_PLANE_PROP_FB_ID,
          .value = meta_drm_buffer_get_fb_id (buffer),
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_ID,
          .value = meta_kms_crtc_get_id (plane_assignment->crtc),
        },
        {
          .prop = META_KMS_PLANE_PROP_SRC_X,
          .value = plane_assignment->src_rect.x,
        },
        {
          .prop = META_KMS_PLANE_PROP_SRC_Y,
          .value = plane_assignment->src_rect.y,
        },
        {
          .prop = META_KMS_PLANE_PROP_SRC_W,
          .value = plane_assignment->src_rect.width,
        },
        {
          .prop = META_KMS_PLANE_PROP_SRC_H,
          .value = plane_assignment->src_rect.height,
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_X,
          .value = plane_assignment->dst_rect.x,
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_Y,
          .value = plane_assignment->dst_rect.y,
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_W,
          .value = plane_assignment->dst_rect.width,
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_H,
          .value = plane_assignment->dst_rect.height,
        },
      };

      for (i = 0; i < G_N_ELEMENTS (props); i++)
        {
          if (!add_plane_property (impl_device,
                                   plane, req,
                                   props[i].prop,
                                   props[i].value,
                                   error))
            return FALSE;
        }

      if (plane_assignment->flags & META_KMS_ASSIGN_PLANE_FLAG_DIRECT_SCANOUT)
        {
          int signaled_sync_file;

          signaled_sync_file =
            meta_kms_impl_device_get_signaled_sync_file (impl_device);

          if (signaled_sync_file >= 0)
            {
              g_autoptr (GError) local_error = NULL;

              if (!add_plane_property (impl_device,
                                       plane, req,
                                       META_KMS_PLANE_PROP_IN_FENCE_FD,
                                       signaled_sync_file,
                                       &local_error))
                {
                  meta_topic (META_DEBUG_KMS,
                              "add_plane_property failed for IN_FENCE_FD: %s",
                              local_error->message);
                }
            }
        }

      if (plane_assignment->cursor_hotspot.has_update)
        {
          struct {
            MetaKmsPlaneProp prop;
            uint64_t value;
          } props[] = {
            {
              .prop = META_KMS_PLANE_PROP_HOTSPOT_X,
              .value = plane_assignment->cursor_hotspot.is_valid ?
                       plane_assignment->cursor_hotspot.x :
                       0,
            },
            {
              .prop = META_KMS_PLANE_PROP_HOTSPOT_Y,
              .value = plane_assignment->cursor_hotspot.is_valid ?
                       plane_assignment->cursor_hotspot.y :
                       0,
            },
          };

          for (i = 0; i < G_N_ELEMENTS (props); i++)
            {
              if (!add_plane_property (impl_device,
                                       plane, req,
                                       props[i].prop,
                                       props[i].value,
                                       error))
                return FALSE;
            }
        }
    }
  else
    {
      int i;
      struct {
        MetaKmsPlaneProp prop;
        uint64_t value;
      } props[] = {
        {
          .prop = META_KMS_PLANE_PROP_FB_ID,
          .value = 0,
        },
        {
          .prop = META_KMS_PLANE_PROP_CRTC_ID,
          .value = 0,
        },
      };

      for (i = 0; i < G_N_ELEMENTS (props); i++)
        {
          if (!add_plane_property (impl_device,
                                   plane, req,
                                   props[i].prop,
                                   props[i].value,
                                   error))
            return FALSE;
        }
    }

  if (plane_assignment->rotation)
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting plane (%u, %s) rotation to %u",
                  meta_kms_plane_get_id (plane),
                  meta_kms_impl_device_get_path (impl_device),
                  plane_assignment->rotation);

      if (!add_plane_property (impl_device, plane, req,
                               META_KMS_PLANE_PROP_ROTATION,
                               plane_assignment->rotation, error))
        return FALSE;
    }

  fb_damage = plane_assignment->fb_damage;
  if (fb_damage &&
      meta_kms_plane_get_prop_id (plane,
                                  META_KMS_PLANE_PROP_FB_DAMAGE_CLIPS_ID))
    {
      meta_topic (META_DEBUG_KMS,
                  "[atomic] Setting %d damage clips on %u",
                  fb_damage->n_rects,
                  meta_kms_plane_get_id (plane));

      prop_id = store_new_blob (impl_device,
                                blob_ids,
                                fb_damage->rects,
                                fb_damage->n_rects *
                                sizeof (struct drm_mode_rect),
                                error);
      if (!prop_id)
        return FALSE;

      if (!add_plane_property (impl_device,
                               plane, req,
                               META_KMS_PLANE_PROP_FB_DAMAGE_CLIPS_ID,
                               prop_id,
                               error))
        return FALSE;
    }
  return TRUE;
}

static gboolean
process_crtc_color_updates (MetaKmsImplDevice  *impl_device,
                            MetaKmsUpdate      *update,
                            drmModeAtomicReq   *req,
                            GArray             *blob_ids,
                            gpointer            update_entry,
                            gpointer            user_data,
                            GError            **error)
{
  MetaKmsCrtcColorUpdate *color_update = update_entry;
  MetaKmsCrtc *crtc = color_update->crtc;

  if (color_update->gamma.has_update)
    {
      MetaGammaLut *gamma = color_update->gamma.state;
      uint32_t color_lut_blob_id = 0;

      if (gamma && gamma->size > 0)
        {
          g_autofree struct drm_color_lut *drm_color_lut = NULL;
          size_t color_lut_size;
          int i;

          color_lut_size = sizeof(struct drm_color_lut) * gamma->size;
          drm_color_lut = g_malloc (color_lut_size);

          for (i = 0; i < gamma->size; i++)
            {
              drm_color_lut[i].red = gamma->red[i];
              drm_color_lut[i].green = gamma->green[i];
              drm_color_lut[i].blue = gamma->blue[i];
            }

          color_lut_blob_id = store_new_blob (impl_device,
                                              blob_ids,
                                              drm_color_lut,
                                              color_lut_size,
                                              error);

          meta_topic (META_DEBUG_KMS,
                      "[atomic] Setting CRTC (%u, %s) gamma, size: %zu",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device),
                      gamma->size);
        }
      else
        {
          meta_topic (META_DEBUG_KMS,
                      "[atomic] Setting CRTC (%u, %s) gamma to bypass",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device));
        }

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_GAMMA_LUT,
                              color_lut_blob_id,
                              error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
process_page_flip_listener (MetaKmsImplDevice  *impl_device,
                            MetaKmsUpdate      *update,
                            drmModeAtomicReq   *req,
                            GArray             *blob_ids,
                            gpointer            update_entry,
                            gpointer            user_data,
                            GError            **error)
{
  MetaKmsImplDeviceAtomic *impl_device_atomic =
    META_KMS_IMPL_DEVICE_ATOMIC (impl_device);
  MetaKmsPageFlipListener *listener = update_entry;
  MetaKmsPageFlipData *page_flip_data;
  uint32_t crtc_id;
  gpointer listener_user_data;
  GDestroyNotify listener_destroy_notify;

  crtc_id = meta_kms_crtc_get_id (listener->crtc);
  page_flip_data = g_hash_table_lookup (impl_device_atomic->page_flip_datas,
                                        GUINT_TO_POINTER (crtc_id));
  if (!page_flip_data)
    {
      page_flip_data = meta_kms_page_flip_data_new (impl_device,
                                                    listener->crtc);
      g_hash_table_insert (impl_device_atomic->page_flip_datas,
                           GUINT_TO_POINTER (crtc_id),
                           page_flip_data);

      meta_kms_impl_device_hold_fd (impl_device);

      meta_topic (META_DEBUG_KMS,
                  "[atomic] Adding page flip data for (%u, %s): %p",
                  crtc_id,
                  meta_kms_impl_device_get_path (impl_device),
                  page_flip_data);
    }

  listener_user_data = g_steal_pointer (&listener->user_data);
  listener_destroy_notify = g_steal_pointer (&listener->destroy_notify);
  meta_kms_page_flip_data_add_listener (page_flip_data,
                                        listener->vtable,
                                        listener->main_context,
                                        listener_user_data,
                                        listener_destroy_notify);

  return TRUE;
}

static gboolean
process_entries (MetaKmsImplDevice         *impl_device,
                 MetaKmsUpdate             *update,
                 drmModeAtomicReq          *req,
                 GArray                    *blob_ids,
                 GList                     *entries,
                 gpointer                   user_data,
                 MetaKmsAtomicProcessFunc   func,
                 GError                   **error)
{
  GList *l;

  for (l = entries; l; l = l->next)
    {
      if (!func (impl_device,
                 update,
                 req,
                 blob_ids,
                 l->data,
                 user_data,
                 error))
        return FALSE;
    }

  return TRUE;
}

static void
atomic_page_flip_handler (int           fd,
                          unsigned int  sequence,
                          unsigned int  tv_sec,
                          unsigned int  tv_usec,
                          unsigned int  crtc_id,
                          void         *user_data)
{
  MetaKmsImplDeviceAtomic *impl_device_atomic = user_data;
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (impl_device_atomic);
  MetaKmsPageFlipData *page_flip_data = NULL;

  g_hash_table_steal_extended (impl_device_atomic->page_flip_datas,
                               GUINT_TO_POINTER (crtc_id),
                               NULL,
                               (gpointer *) &page_flip_data);

  COGL_TRACE_MESSAGE ("atomic_page_flip_handler()",
                      "[atomic] Page flip callback for CRTC (%u, %s)",
                      crtc_id, meta_kms_impl_device_get_path (impl_device));

  meta_topic (META_DEBUG_KMS,
              "[atomic] Page flip callback for CRTC (%u, %s), data: %p",
              crtc_id, meta_kms_impl_device_get_path (impl_device),
              page_flip_data);

  if (!page_flip_data)
    return;

  meta_kms_impl_device_unhold_fd (impl_device);

  meta_kms_page_flip_data_set_timings_in_impl (page_flip_data,
                                               sequence, tv_sec, tv_usec);
  meta_kms_impl_device_handle_page_flip_callback (impl_device, page_flip_data);
}

static void
meta_kms_impl_device_atomic_setup_drm_event_context (MetaKmsImplDevice *impl_device,
                                                     drmEventContext   *drm_event_context)
{
  drm_event_context->version = 3;
  drm_event_context->page_flip_handler2 = atomic_page_flip_handler;
}

static const char *
commit_flags_string (uint32_t commit_flags)
{
  static char static_commit_flags_string[255];
  const char *commit_flag_strings[4] = { NULL };
  int i = 0;
  g_autofree char *commit_flags_string = NULL;

  if (commit_flags & DRM_MODE_ATOMIC_NONBLOCK)
    commit_flag_strings[i++] = "ATOMIC_NONBLOCK";
  if (commit_flags & DRM_MODE_ATOMIC_ALLOW_MODESET)
    commit_flag_strings[i++] = "ATOMIC_ALLOW_MODESET";
  if (commit_flags & DRM_MODE_PAGE_FLIP_EVENT)
    commit_flag_strings[i++] = "PAGE_FLIP_EVENT";
  if (commit_flags & DRM_MODE_ATOMIC_TEST_ONLY)
    commit_flag_strings[i++] = "TEST_ONLY";

  commit_flags_string = g_strjoinv ("|", (char **) commit_flag_strings);
  strncpy (static_commit_flags_string, commit_flags_string,
           (sizeof static_commit_flags_string) - 1);

  return static_commit_flags_string;
}

static gboolean
disable_connectors (MetaKmsImplDevice  *impl_device,
                    drmModeAtomicReq   *req,
                    GError            **error)
{
  GList *l;

  for (l = meta_kms_impl_device_peek_connectors (impl_device); l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      if (!add_connector_property (impl_device,
                                   connector, req,
                                   META_KMS_CONNECTOR_PROP_CRTC_ID,
                                   0,
                                   error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
disable_planes (MetaKmsImplDevice  *impl_device,
                drmModeAtomicReq   *req,
                GError            **error)
{
  GList *l;

  for (l = meta_kms_impl_device_peek_planes (impl_device); l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (!add_plane_property (impl_device,
                               plane, req,
                               META_KMS_PLANE_PROP_CRTC_ID,
                               0,
                               error))
        return FALSE;

      if (!add_plane_property (impl_device,
                               plane, req,
                               META_KMS_PLANE_PROP_FB_ID,
                               0,
                               error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
disable_crtcs (MetaKmsImplDevice  *impl_device,
               drmModeAtomicReq   *req,
               GError            **error)
{
  GList *l;

  for (l = meta_kms_impl_device_peek_crtcs (impl_device); l; l = l->next)
    {
      MetaKmsCrtc *crtc = l->data;

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_ACTIVE,
                              0,
                              error))
        return FALSE;

      if (!add_crtc_property (impl_device,
                              crtc, req,
                              META_KMS_CRTC_PROP_MODE_ID,
                              0,
                              error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
disable_planes_and_connectors (MetaKmsImplDevice  *impl_device,
                               drmModeAtomicReq   *req,
                               GError            **error)
{
  if (!disable_connectors (impl_device, req, error))
    return FALSE;
  if (!disable_planes (impl_device, req, error))
    return FALSE;

  return TRUE;
}

static MetaKmsFeedback *
meta_kms_impl_device_atomic_process_update (MetaKmsImplDevice *impl_device,
                                            MetaKmsUpdate     *update,
                                            MetaKmsUpdateFlag  flags)
{
  GError *error = NULL;
  GList *failed_planes = NULL;
  drmModeAtomicReq *req;
  g_autoptr (GArray) blob_ids = NULL;
  int fd;
  uint32_t commit_flags = 0;
  int ret;

  blob_ids = g_array_new (FALSE, TRUE, sizeof (uint32_t));

  meta_topic (META_DEBUG_KMS, "[atomic] Processing update");

  req = drmModeAtomicAlloc ();
  if (!req)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create atomic transaction request: %s",
                   g_strerror (errno));
      goto err;
    }

  if (meta_kms_update_get_mode_sets (update))
    {
      if (!disable_planes_and_connectors (impl_device, req, &error))
        goto err;
    }

  if (!process_entries (impl_device,
                        update,
                        req,
                        blob_ids,
                        meta_kms_update_get_connector_updates (update),
                        NULL,
                        process_connector_update,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        req,
                        blob_ids,
                        meta_kms_update_get_mode_sets (update),
                        NULL,
                        process_mode_set,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        req,
                        blob_ids,
                        meta_kms_update_get_plane_assignments (update),
                        NULL,
                        process_plane_assignment,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        req,
                        blob_ids,
                        meta_kms_update_get_crtc_color_updates (update),
                        NULL,
                        process_crtc_color_updates,
                        &error))
    goto err;

  if (meta_kms_update_get_needs_modeset (update))
    commit_flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
  else
    commit_flags |= DRM_MODE_ATOMIC_NONBLOCK;

  if (meta_kms_update_get_page_flip_listeners (update))
    commit_flags |= DRM_MODE_PAGE_FLIP_EVENT;

  if (flags & META_KMS_UPDATE_FLAG_TEST_ONLY)
    commit_flags |= DRM_MODE_ATOMIC_TEST_ONLY;

  meta_topic (META_DEBUG_KMS,
              "[atomic] Committing update flags: %s",
              commit_flags_string (commit_flags));

  fd = meta_kms_impl_device_get_fd (impl_device);
  ret = drmModeAtomicCommit (fd, req, commit_flags, impl_device);
  if (ret < 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeAtomicCommit: %s", g_strerror (-ret));
      goto err;
    }

  drmModeAtomicFree (req);

  process_entries (impl_device,
                   update,
                   req,
                   blob_ids,
                   meta_kms_update_get_page_flip_listeners (update),
                   NULL,
                   process_page_flip_listener,
                   NULL);

  release_blob_ids (impl_device, blob_ids);

  return meta_kms_feedback_new_passed (NULL);

err:
  meta_topic (META_DEBUG_KMS, "[atomic] KMS update failed: %s", error->message);

  if (req)
    drmModeAtomicFree (req);

  release_blob_ids (impl_device, blob_ids);

  return meta_kms_feedback_new_failed (failed_planes, error);
}

static void
meta_kms_impl_device_atomic_disable (MetaKmsImplDevice *impl_device)
{
  g_autoptr (GError) error = NULL;
  drmModeAtomicReq *req;
  int fd;
  int ret;

  meta_topic (META_DEBUG_KMS, "[atomic] Disabling '%s'",
              meta_kms_impl_device_get_path (impl_device));

  req = drmModeAtomicAlloc ();
  if (!req)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create atomic transaction request: %s",
                   g_strerror (errno));
      goto err;
    }

  if (!disable_connectors (impl_device, req, &error))
    goto err;
  if (!disable_planes (impl_device, req, &error))
    goto err;
  if (!disable_crtcs (impl_device, req, &error))
    goto err;

  meta_topic (META_DEBUG_KMS, "[atomic] Committing disable-device transaction");

  fd = meta_kms_impl_device_get_fd (impl_device);
  ret = drmModeAtomicCommit (fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, impl_device);
  drmModeAtomicFree (req);
  if (ret < 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeAtomicCommit: %s", g_strerror (-ret));
      goto err;
    }

  return;

err:
  g_warning ("[atomic] Failed to disable device '%s': %s",
             meta_kms_impl_device_get_path (impl_device),
             error->message);
}

static void
meta_kms_impl_device_atomic_handle_page_flip_callback (MetaKmsImplDevice   *impl_device,
                                                       MetaKmsPageFlipData *page_flip_data)
{
  meta_kms_page_flip_data_flipped_in_impl (page_flip_data);
}

static void
meta_kms_impl_device_atomic_discard_pending_page_flips (MetaKmsImplDevice *impl_device)
{
}

static gboolean
dispose_page_flip_data (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  MetaKmsPageFlipData *page_flip_data = value;
  MetaKmsImplDevice *impl_device = user_data;

  meta_kms_page_flip_data_discard_in_impl (page_flip_data, NULL);
  meta_kms_impl_device_unhold_fd (impl_device);

  return TRUE;
}

static void
meta_kms_impl_device_atomic_prepare_shutdown (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDeviceAtomic *impl_device_atomic =
    META_KMS_IMPL_DEVICE_ATOMIC (impl_device);

  g_hash_table_foreach_remove (impl_device_atomic->page_flip_datas,
                               dispose_page_flip_data,
                               impl_device);
}

static void
meta_kms_impl_device_atomic_finalize (GObject *object)
{
  MetaKmsImplDeviceAtomic *impl_device_atomic =
    META_KMS_IMPL_DEVICE_ATOMIC (object);

  g_assert (g_hash_table_size (impl_device_atomic->page_flip_datas) == 0);

  g_hash_table_unref (impl_device_atomic->page_flip_datas);

  G_OBJECT_CLASS (meta_kms_impl_device_atomic_parent_class)->finalize (object);
}

static gboolean
requires_hotspots (const char *driver_name)
{
  const char *atomic_driver_hotspots[] = {
    "qxl",
    "vboxvideo",
    "virtio_gpu",
    "vmwgfx",
    NULL,
  };

  return g_strv_contains (atomic_driver_hotspots, driver_name);
}

static MetaDeviceFile *
meta_kms_impl_device_atomic_open_device_file (MetaKmsImplDevice  *impl_device,
                                              const char         *path,
                                              GError            **error)
{
  MetaKmsDevice *device = meta_kms_impl_device_get_device (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (device);
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (META_BACKEND_NATIVE (backend));
  g_autoptr (MetaDeviceFile) device_file = NULL;

  device_file = meta_device_pool_open (device_pool, path,
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       error);
  if (!device_file)
    return NULL;

  if (!meta_device_file_has_tag (device_file,
                                 META_DEVICE_FILE_TAG_KMS,
                                 META_KMS_DEVICE_FILE_TAG_ATOMIC))
    {
      int fd = meta_device_file_get_fd (device_file);

      g_warn_if_fail (!meta_device_file_has_tag (device_file,
                                                 META_DEVICE_FILE_TAG_KMS,
                                                 META_KMS_DEVICE_FILE_TAG_SIMPLE));

      if (drmSetClientCap (fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
        {
          g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_NOT_SUPPORTED,
                       "DRM_CLIENT_CAP_UNIVERSAL_PLANES not supported");
          return NULL;
        }

      if (drmSetClientCap (fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0)
        {
          g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_NOT_SUPPORTED,
                       "DRM_CLIENT_CAP_ATOMIC not supported");
          return NULL;
        }

      meta_device_file_tag (device_file,
                            META_DEVICE_FILE_TAG_KMS,
                            META_KMS_DEVICE_FILE_TAG_ATOMIC);
    }

  return g_steal_pointer (&device_file);
}

static gboolean
has_cursor_hotspot_properties (MetaKmsImplDevice *impl_device)
{
  GList *planes;
  GList *l;

  planes = meta_kms_impl_device_peek_planes (impl_device);
  for (l = planes; l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (meta_kms_plane_get_plane_type (plane) != META_KMS_PLANE_TYPE_CURSOR)
        continue;

      return meta_kms_plane_supports_cursor_hotspot (plane);
    }

  return FALSE;
}

static gboolean
is_atomic_allowed (const char *driver_name)
{
  const char *atomic_driver_deny_list[] = {
    "xlnx",
    NULL,
  };

  return !g_strv_contains (atomic_driver_deny_list, driver_name);
}

static gboolean
meta_kms_impl_device_atomic_initable_init (GInitable     *initable,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (initable);

  if (!initable_parent_iface->init (initable, cancellable, error))
    return FALSE;

  if (!is_atomic_allowed (meta_kms_impl_device_get_driver_name (impl_device)))
    {
      g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_DENY_LISTED,
                   "Atomic mode setting disable via driver deny list");
      return FALSE;
    }

  if (!meta_kms_impl_device_init_mode_setting (impl_device, error))
    return FALSE;

  if (requires_hotspots (meta_kms_impl_device_get_driver_name (impl_device)))
    {
      if (drmSetClientCap (meta_kms_impl_device_get_fd (impl_device),
                           DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT, 1) != 0)
        {
          g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_NOT_SUPPORTED,
                       "Kernel has no support for virtual cursor plane on %s",
                       meta_kms_impl_device_get_driver_name (impl_device));
          return FALSE;
        }
      if (!has_cursor_hotspot_properties (impl_device))
        {
          g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_NOT_SUPPORTED,
                       "Plane cursor with hotspot properties is missing on %s",
                       meta_kms_impl_device_get_driver_name (impl_device));
          return FALSE;
        }
    }

  g_message ("Added device '%s' (%s) using atomic mode setting.",
             meta_kms_impl_device_get_path (impl_device),
             meta_kms_impl_device_get_driver_name (impl_device));

  return TRUE;
}

static void
meta_kms_impl_device_atomic_init (MetaKmsImplDeviceAtomic *impl_device_atomic)
{
  impl_device_atomic->page_flip_datas = g_hash_table_new (NULL, NULL);
}

static void
initable_iface_init (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_kms_impl_device_atomic_initable_init;
}

static void
meta_kms_impl_device_atomic_class_init (MetaKmsImplDeviceAtomicClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaKmsImplDeviceClass *impl_device_class =
    META_KMS_IMPL_DEVICE_CLASS (klass);

  object_class->finalize = meta_kms_impl_device_atomic_finalize;

  impl_device_class->open_device_file =
    meta_kms_impl_device_atomic_open_device_file;
  impl_device_class->setup_drm_event_context =
    meta_kms_impl_device_atomic_setup_drm_event_context;
  impl_device_class->process_update =
    meta_kms_impl_device_atomic_process_update;
  impl_device_class->disable =
    meta_kms_impl_device_atomic_disable;
  impl_device_class->handle_page_flip_callback =
    meta_kms_impl_device_atomic_handle_page_flip_callback;
  impl_device_class->discard_pending_page_flips =
    meta_kms_impl_device_atomic_discard_pending_page_flips;
  impl_device_class->prepare_shutdown =
    meta_kms_impl_device_atomic_prepare_shutdown;
}
