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

#include "backends/native/meta-kms-impl-device-simple.h"

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-kms-connector-private.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane-private.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-thread-impl.h"

typedef gboolean (* MetaKmsSimpleProcessFunc) (MetaKmsImplDevice  *impl_device,
                                               MetaKmsUpdate      *update,
                                               gpointer            entry_data,
                                               GError            **error);

typedef struct _CachedModeSet
{
  GList *connectors;
  drmModeModeInfo *drm_mode;

  int width;
  int height;
  int stride;
  uint32_t format;
  uint64_t modifier;
} CachedModeSet;

struct _MetaKmsImplDeviceSimple
{
  MetaKmsImplDevice parent;

  GSource *mode_set_fallback_feedback_source;
  GList *mode_set_fallback_page_flip_datas;

  GList *pending_page_flip_retries;
  GSource *retry_page_flips_source;

  GList *postponed_page_flip_datas;
  GList *postponed_mode_set_fallback_datas;

  GList *posted_page_flip_datas;

  GHashTable *cached_mode_sets;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaKmsImplDeviceSimple, meta_kms_impl_device_simple,
                         META_TYPE_KMS_IMPL_DEVICE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static void
flush_postponed_page_flip_datas (MetaKmsImplDeviceSimple *impl_device_simple);

static gboolean
get_connector_property (MetaKmsImplDevice     *impl_device,
                        MetaKmsConnector      *connector,
                        MetaKmsConnectorProp   prop,
                        uint64_t              *value,
                        GError               **error)
{
  uint32_t prop_id;
  int fd;
  drmModeConnector *drm_connector;
  int i;
  gboolean found;

  prop_id = meta_kms_connector_get_prop_id (connector, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Property (%s) not found on connector %u",
                   meta_kms_connector_get_prop_name (connector, prop),
                   meta_kms_connector_get_id (connector));
      return FALSE;
    }

  fd = meta_kms_impl_device_get_fd (impl_device);

  drm_connector = drmModeGetConnector (fd,
                                       meta_kms_connector_get_id (connector));
  if (!drm_connector)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to get connector %u resources: %s",
                   meta_kms_connector_get_id (connector),
                   g_strerror (errno));
      return FALSE;
    }

  found = FALSE;
  for (i = 0; i < drm_connector->count_props; i++)
    {
      if (drm_connector->props[i] == prop_id)
        {
          *value = drm_connector->prop_values[i];
          found = TRUE;
          break;
        }
    }

  drmModeFreeConnector (drm_connector);

  if (!found)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Connector property %u not found", prop_id);
      return FALSE;
    }

  return TRUE;
}

static gboolean
set_connector_property (MetaKmsImplDevice     *impl_device,
                        MetaKmsConnector      *connector,
                        MetaKmsConnectorProp   prop,
                        uint64_t               value,
                        GError               **error)
{
  uint32_t prop_id;
  int fd;
  int ret;

  prop_id = meta_kms_connector_get_prop_id (connector, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Property (%s) not found on connector %u",
                   meta_kms_connector_get_prop_name (connector, prop),
                   meta_kms_connector_get_id (connector));
      return FALSE;
    }

  fd = meta_kms_impl_device_get_fd (impl_device);

  ret = drmModeObjectSetProperty (fd,
                                  meta_kms_connector_get_id (connector),
                                  DRM_MODE_OBJECT_CONNECTOR,
                                  prop_id,
                                  value);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to set connector %u property %u: %s",
                   meta_kms_connector_get_id (connector),
                   prop_id,
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static gboolean
set_crtc_property (MetaKmsImplDevice  *impl_device,
                   MetaKmsCrtc        *crtc,
                   MetaKmsCrtcProp     prop,
                   uint64_t            value,
                   GError            **error)
{
  uint32_t prop_id;
  int fd;
  int ret;

  prop_id = meta_kms_crtc_get_prop_id (crtc, prop);
  if (!prop_id)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Property (%s) not found on CRTC %u",
                   meta_kms_crtc_get_prop_name (crtc, prop),
                   meta_kms_crtc_get_id (crtc));
      return FALSE;
    }

  fd = meta_kms_impl_device_get_fd (impl_device);

  ret = drmModeObjectSetProperty (fd,
                                  meta_kms_crtc_get_id (crtc),
                                  DRM_MODE_OBJECT_CRTC,
                                  prop_id,
                                  value);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to set CRTC %u property %u: %s",
                   meta_kms_crtc_get_id (crtc),
                   prop_id,
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_connector_update (MetaKmsImplDevice  *impl_device,
                          MetaKmsUpdate      *update,
                          gpointer            update_entry,
                          GError            **error)
{
  MetaKmsConnectorUpdate *connector_update = update_entry;
  MetaKmsConnector *connector = connector_update->connector;

  if (connector_update->underscanning.has_update &&
      connector_update->underscanning.is_active)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Setting underscanning on connector %u (%s) to "
                  "%" G_GUINT64_FORMAT "x%" G_GUINT64_FORMAT,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device),
                  connector_update->underscanning.hborder,
                  connector_update->underscanning.vborder);

      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN,
                                   1,
                                   error))
        return FALSE;
      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN_HBORDER,
                                   connector_update->underscanning.hborder,
                                   error))
        return FALSE;
      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN_VBORDER,
                                   connector_update->underscanning.vborder,
                                   error))
        return FALSE;
    }
  else if (connector_update->underscanning.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Unsetting underscanning on connector %u (%s)",
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_UNDERSCAN,
                                   0,
                                   error))
        return FALSE;
    }

  if (connector_update->privacy_screen.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Toggling privacy screen to %d on connector %u (%s)",
                  connector_update->privacy_screen.is_enabled,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_SW_STATE,
                                   connector_update->privacy_screen.is_enabled,
                                   error))
        return FALSE;
    }

  if (connector_update->max_bpc.has_update)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Setting max BPC to %u on connector %u (%s)",
                  (unsigned int) connector_update->max_bpc.value,
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_MAX_BPC,
                                   connector_update->max_bpc.value,
                                   error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
process_crtc_update (MetaKmsImplDevice  *impl_device,
                     MetaKmsUpdate      *update,
                     gpointer            update_entry,
                     GError            **error)
{
  MetaKmsCrtcUpdate *crtc_update = update_entry;
  MetaKmsCrtc *crtc = crtc_update->crtc;

  if (crtc_update->vrr.has_update)
    {
      if (!set_crtc_property (impl_device,
                              crtc,
                              META_KMS_CRTC_PROP_VRR_ENABLED,
                              !!crtc_update->vrr.is_enabled,
                              error))
        return FALSE;
    }

  return TRUE;
}

static CachedModeSet *
cached_mode_set_new (GList                 *connectors,
                     const drmModeModeInfo *drm_mode,
                     MetaDrmBuffer         *buffer)
{
  CachedModeSet *cached_mode_set;


  cached_mode_set = g_new0 (CachedModeSet, 1);
  *cached_mode_set = (CachedModeSet) {
    .connectors = g_list_copy (connectors),
    .drm_mode = g_memdup2 (drm_mode, sizeof *drm_mode),
    .width = meta_drm_buffer_get_width (buffer),
    .height = meta_drm_buffer_get_height (buffer),
    .stride = meta_drm_buffer_get_stride (buffer),
    .format = meta_drm_buffer_get_format (buffer),
    .modifier = meta_drm_buffer_get_modifier (buffer),
  };

  return cached_mode_set;
}

static void
cached_mode_set_free (CachedModeSet *cached_mode_set)
{
  g_list_free (cached_mode_set->connectors);
  g_free (cached_mode_set->drm_mode);
  g_free (cached_mode_set);
}

static void
fill_connector_ids_array (GList     *connectors,
                          uint32_t **out_connectors,
                          int       *out_n_connectors)
{
  GList *l;
  int i;

  *out_n_connectors = g_list_length (connectors);
  *out_connectors = g_new0 (uint32_t, *out_n_connectors);
  i = 0;
  for (l = connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      (*out_connectors)[i++] = meta_kms_connector_get_id (connector);
    }
}

static gboolean
set_plane_rotation (MetaKmsImplDevice  *impl_device,
                    MetaKmsPlane       *plane,
                    uint64_t            rotation,
                    GError            **error)
{
  int fd;
  uint32_t rotation_prop_id;
  int ret;

  fd = meta_kms_impl_device_get_fd (impl_device);

  rotation_prop_id = meta_kms_plane_get_prop_id (plane,
                                                 META_KMS_PLANE_PROP_ROTATION);

  meta_topic (META_DEBUG_KMS,
              "[simple] Setting plane %u (%s) rotation to %" G_GUINT64_FORMAT,
              meta_kms_plane_get_id (plane),
              meta_kms_impl_device_get_path (impl_device),
              rotation);

  ret = drmModeObjectSetProperty (fd,
                                  meta_kms_plane_get_id (plane),
                                  DRM_MODE_OBJECT_PLANE,
                                  rotation_prop_id,
                                  rotation);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to rotation property (%u) to %" G_GUINT64_FORMAT
                   " on plane %u: %s",
                   rotation_prop_id,
                   rotation,
                   meta_kms_plane_get_id (plane),
                   g_strerror (-ret));
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_mode_set (MetaKmsImplDevice  *impl_device,
                  MetaKmsUpdate      *update,
                  gpointer            update_entry,
                  GError            **error)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);
  MetaKmsModeSet *mode_set = update_entry;
  MetaKmsCrtc *crtc = mode_set->crtc;
  g_autofree uint32_t *connectors = NULL;
  int n_connectors;
  MetaKmsPlaneAssignment *plane_assignment;
  MetaDrmBuffer *buffer;
  drmModeModeInfo *drm_mode;
  uint32_t x, y;
  uint32_t fb_id;
  int fd;
  int ret;

  crtc = mode_set->crtc;

  if (mode_set->mode)
    {
      GList *l;

      drm_mode = g_alloca (sizeof *drm_mode);
      *drm_mode = *meta_kms_mode_get_drm_mode (mode_set->mode);

      fill_connector_ids_array (mode_set->connectors,
                                &connectors,
                                &n_connectors);

      plane_assignment = meta_kms_update_get_primary_plane_assignment (update,
                                                                       crtc);
      if (!plane_assignment)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Missing primary plane assignment for legacy mode set on CRTC %u",
                       meta_kms_crtc_get_id (crtc));
          return FALSE;
        }

      x = meta_fixed_16_to_int (plane_assignment->src_rect.x);
      y = meta_fixed_16_to_int (plane_assignment->src_rect.y);

      if (plane_assignment->rotation)
        {
          if (!set_plane_rotation (impl_device,
                                   plane_assignment->plane,
                                   plane_assignment->rotation,
                                   error))
            return FALSE;
        }

      buffer = plane_assignment->buffer;
      if (!meta_drm_buffer_ensure_fb_id (buffer, error))
        return FALSE;

      fb_id = meta_drm_buffer_get_fb_id (buffer);

      for (l = mode_set->connectors; l; l = l->next)
        {
          MetaKmsConnector *connector = l->data;
          uint64_t dpms_value;

          if (!get_connector_property (impl_device,
                                       connector,
                                       META_KMS_CONNECTOR_PROP_DPMS,
                                       &dpms_value,
                                       error))
            return FALSE;

          if (dpms_value != DRM_MODE_DPMS_ON)
            {
              meta_topic (META_DEBUG_KMS,
                          "[simple] Setting DPMS of connector %u (%s) to ON",
                          meta_kms_connector_get_id (connector),
                          meta_kms_impl_device_get_path (impl_device));

              if (!set_connector_property (impl_device,
                                           connector,
                                           META_KMS_CONNECTOR_PROP_DPMS,
                                           DRM_MODE_DPMS_ON,
                                           error))
                return FALSE;
            }
        }

      meta_topic (META_DEBUG_KMS,
                  "[simple] Setting mode of CRTC %u (%s) to %s",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device),
                  drm_mode->name);
    }
  else
    {
      buffer = NULL;
      drm_mode = NULL;
      x = y = 0;
      n_connectors = 0;
      connectors = NULL;
      fb_id = 0;

      meta_topic (META_DEBUG_KMS,
                  "[simple] Unsetting mode of CRTC %u (%s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));
    }

  fd = meta_kms_impl_device_get_fd (impl_device);
  ret = drmModeSetCrtc (fd,
                        meta_kms_crtc_get_id (crtc),
                        fb_id,
                        x, y,
                        connectors, n_connectors,
                        drm_mode);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to set mode %s on CRTC %u: %s",
                   drm_mode ? drm_mode->name : "off",
                   meta_kms_crtc_get_id (crtc),
                   g_strerror (-ret));
      return FALSE;
    }

  if (drm_mode)
    {
      g_hash_table_replace (impl_device_simple->cached_mode_sets,
                            crtc,
                            cached_mode_set_new (mode_set->connectors,
                                                 drm_mode,
                                                 buffer));
    }
  else
    {
      g_hash_table_remove (impl_device_simple->cached_mode_sets, crtc);
    }

  return TRUE;
}

static gboolean
process_crtc_color_updates (MetaKmsImplDevice  *impl_device,
                            MetaKmsUpdate      *update,
                            gpointer            update_entry,
                            GError            **error)
{
  MetaKmsCrtcColorUpdate *color_update = update_entry;
  MetaKmsCrtc *crtc = color_update->crtc;

  if (color_update->gamma.has_update)
    {
      MetaGammaLut *gamma = color_update->gamma.state;
      int fd;
      int ret;

      fd = meta_kms_impl_device_get_fd (impl_device);

      if (gamma)
        {
          meta_topic (META_DEBUG_KMS,
                      "[simple] Setting CRTC %u (%s) gamma, size: %zu",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device),
                      gamma->size);

          ret = drmModeCrtcSetGamma (fd, meta_kms_crtc_get_id (crtc),
                                     gamma->size,
                                     gamma->red,
                                     gamma->green,
                                     gamma->blue);
        }
      else
        {
          const MetaKmsCrtcState *crtc_state =
            meta_kms_crtc_get_current_state (crtc);
          g_autoptr (MetaGammaLut) identity_lut =
            meta_gamma_lut_new_identity (crtc_state->gamma.size);

          meta_topic (META_DEBUG_KMS,
                      "[simple] Setting CRTC (%u, %s) gamma to bypass",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device));

          ret = drmModeCrtcSetGamma (fd, meta_kms_crtc_get_id (crtc),
                                     identity_lut->size,
                                     identity_lut->red,
                                     identity_lut->green,
                                     identity_lut->blue);
        }

      if (ret != 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                       "drmModeCrtcSetGamma on CRTC %u failed: %s",
                       meta_kms_crtc_get_id (crtc),
                       g_strerror (-ret));
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
is_timestamp_earlier_than (uint64_t ts1,
                           uint64_t ts2)
{
  if (ts1 == ts2)
    return FALSE;
  else
    return ts2 - ts1 < UINT64_MAX / 2;
}

typedef struct _RetryPageFlipData
{
  MetaKmsCrtc *crtc;
  uint32_t fb_id;
  MetaKmsPageFlipData *page_flip_data;
  float refresh_rate;
  uint64_t retry_time_us;
  MetaKmsCustomPageFlip *custom_page_flip;
} RetryPageFlipData;

static void
retry_page_flip_data_free (RetryPageFlipData *retry_page_flip_data)
{
  g_assert (!retry_page_flip_data->page_flip_data);
  g_clear_pointer (&retry_page_flip_data->custom_page_flip,
                   meta_kms_custom_page_flip_free);
  g_free (retry_page_flip_data);
}

static CachedModeSet *
get_cached_mode_set (MetaKmsImplDeviceSimple *impl_device_simple,
                     MetaKmsCrtc             *crtc)
{
  return g_hash_table_lookup (impl_device_simple->cached_mode_sets, crtc);
}

static float
get_cached_crtc_refresh_rate (MetaKmsImplDeviceSimple *impl_device_simple,
                              MetaKmsCrtc             *crtc)
{
  CachedModeSet *cached_mode_set;

  cached_mode_set = g_hash_table_lookup (impl_device_simple->cached_mode_sets,
                                         crtc);
  g_assert (cached_mode_set);

  return meta_calculate_drm_mode_refresh_rate (cached_mode_set->drm_mode);
}

#define meta_assert_in_kms_impl(kms) \
  g_assert (meta_kms_in_impl_task (kms))

static gboolean
retry_page_flips (gpointer user_data)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (user_data);
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (impl_device_simple);
  uint64_t now_us;
  GList *l;

  now_us = g_source_get_time (impl_device_simple->retry_page_flips_source);

  l = impl_device_simple->pending_page_flip_retries;
  while (l)
    {
      RetryPageFlipData *retry_page_flip_data = l->data;
      MetaKmsCrtc *crtc = retry_page_flip_data->crtc;
      GList *l_next = l->next;
      int fd;
      int ret;
      MetaKmsPageFlipData *page_flip_data;
      MetaKmsCustomPageFlip *custom_page_flip;

      if (is_timestamp_earlier_than (now_us,
                                     retry_page_flip_data->retry_time_us))
        {
          l = l_next;
          continue;
        }

      custom_page_flip = retry_page_flip_data->custom_page_flip;
      if (custom_page_flip)
        {
          meta_topic (META_DEBUG_KMS,
                      "[simple] Retrying custom page flip on CRTC %u (%s)",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device));
          ret = custom_page_flip->func (custom_page_flip->user_data,
                                        retry_page_flip_data->page_flip_data);
        }
      else
        {
          meta_topic (META_DEBUG_KMS,
                      "[simple] Retrying page flip on CRTC %u (%s) with %u",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device),
                      retry_page_flip_data->fb_id);

          fd = meta_kms_impl_device_get_fd (impl_device);
          ret = drmModePageFlip (fd,
                                 meta_kms_crtc_get_id (crtc),
                                 retry_page_flip_data->fb_id,
                                 DRM_MODE_PAGE_FLIP_EVENT,
                                 retry_page_flip_data->page_flip_data);
        }

      if (ret == -EBUSY)
        {
          float refresh_rate;

          meta_topic (META_DEBUG_KMS,
                      "[simple] Rescheduling page flip retry on CRTC %u (%s)",
                      meta_kms_crtc_get_id (crtc),
                      meta_kms_impl_device_get_path (impl_device));

          refresh_rate =
            get_cached_crtc_refresh_rate (impl_device_simple, crtc);
          retry_page_flip_data->retry_time_us +=
            (uint64_t) (G_USEC_PER_SEC / refresh_rate);
          l = l_next;
          continue;
        }

      impl_device_simple->pending_page_flip_retries =
        g_list_remove_link (impl_device_simple->pending_page_flip_retries, l);

      page_flip_data = g_steal_pointer (&retry_page_flip_data->page_flip_data);
      if (ret != 0)
        {
          g_autoptr (GError) error = NULL;

          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-ret),
                       "drmModePageFlip on CRTC %u failed: %s",
                       meta_kms_crtc_get_id (crtc),
                       g_strerror (-ret));
          if (!g_error_matches (error,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            g_critical ("Failed to page flip: %s", error->message);

          meta_kms_page_flip_data_discard_in_impl (page_flip_data, error);
          meta_kms_impl_device_unhold_fd (impl_device);
        }
      else
        {
          impl_device_simple->posted_page_flip_datas =
            g_list_prepend (impl_device_simple->posted_page_flip_datas,
                            page_flip_data);
        }

      retry_page_flip_data_free (retry_page_flip_data);

      l = l_next;
    }

  if (impl_device_simple->pending_page_flip_retries)
    {
      GList *l;
      uint64_t earliest_retry_time_us = 0;

      for (l = impl_device_simple->pending_page_flip_retries; l; l = l->next)
        {
          RetryPageFlipData *retry_page_flip_data = l->data;

          if (l == impl_device_simple->pending_page_flip_retries ||
              is_timestamp_earlier_than (retry_page_flip_data->retry_time_us,
                                         earliest_retry_time_us))
            earliest_retry_time_us = retry_page_flip_data->retry_time_us;
        }

      g_source_set_ready_time (impl_device_simple->retry_page_flips_source,
                               earliest_retry_time_us);
      return G_SOURCE_CONTINUE;
    }
  else
    {
      g_clear_pointer (&impl_device_simple->retry_page_flips_source,
                       g_source_unref);

      flush_postponed_page_flip_datas (impl_device_simple);

      return G_SOURCE_REMOVE;
    }
}

static void
schedule_retry_page_flip (MetaKmsImplDeviceSimple *impl_device_simple,
                          MetaKmsCrtc             *crtc,
                          uint32_t                 fb_id,
                          float                    refresh_rate,
                          MetaKmsPageFlipData     *page_flip_data,
                          MetaKmsCustomPageFlip   *custom_page_flip)
{
  RetryPageFlipData *retry_page_flip_data;
  uint64_t now_us;
  uint64_t retry_time_us;

  now_us = g_get_monotonic_time ();
  retry_time_us = now_us + (uint64_t) (G_USEC_PER_SEC / refresh_rate);

  retry_page_flip_data = g_new0 (RetryPageFlipData, 1);
  *retry_page_flip_data = (RetryPageFlipData) {
    .crtc = crtc,
    .fb_id = fb_id,
    .page_flip_data = page_flip_data,
    .refresh_rate = refresh_rate,
    .retry_time_us = retry_time_us,
    .custom_page_flip = custom_page_flip,
  };

  if (!impl_device_simple->retry_page_flips_source)
    {
      MetaKmsImplDevice *impl_device =
        META_KMS_IMPL_DEVICE (impl_device_simple);
      MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
      MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
      GSource *source;

      source = meta_thread_impl_add_source (thread_impl, retry_page_flips,
                                            impl_device_simple, NULL);
      g_source_set_ready_time (source, retry_time_us);

      impl_device_simple->retry_page_flips_source = source;
    }
  else
    {
      GList *l;

      for (l = impl_device_simple->pending_page_flip_retries; l; l = l->next)
        {
          RetryPageFlipData *pending_retry_page_flip_data = l->data;
          uint64_t pending_retry_time_us =
            pending_retry_page_flip_data->retry_time_us;

          if (is_timestamp_earlier_than (retry_time_us, pending_retry_time_us))
            {
              g_source_set_ready_time (impl_device_simple->retry_page_flips_source,
                                       retry_time_us);
              break;
            }
        }
    }

  impl_device_simple->pending_page_flip_retries =
    g_list_append (impl_device_simple->pending_page_flip_retries,
                   retry_page_flip_data);
}

static void
dispatch_page_flip_datas (GList    **page_flip_datas,
                          GFunc      func,
                          gpointer   user_data)
{
  g_list_foreach (*page_flip_datas, func, user_data);
  g_clear_pointer (page_flip_datas, g_list_free);
}

static gboolean
mode_set_fallback_feedback_idle (gpointer user_data)
{
  MetaKmsImplDeviceSimple *impl_device_simple = user_data;

  g_clear_pointer (&impl_device_simple->mode_set_fallback_feedback_source,
                   g_source_unref);

  if (impl_device_simple->pending_page_flip_retries)
    {
      impl_device_simple->postponed_mode_set_fallback_datas =
        g_steal_pointer (&impl_device_simple->mode_set_fallback_page_flip_datas);
    }
  else
    {
      dispatch_page_flip_datas (&impl_device_simple->mode_set_fallback_page_flip_datas,
                                (GFunc) meta_kms_page_flip_data_mode_set_fallback_in_impl,
                                NULL);
    }

  return G_SOURCE_REMOVE;
}

static gboolean
mode_set_fallback (MetaKmsImplDeviceSimple  *impl_device_simple,
                   MetaKmsUpdate            *update,
                   MetaKmsPlaneAssignment   *plane_assignment,
                   MetaKmsPageFlipData      *page_flip_data,
                   GError                  **error)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (impl_device_simple);
  MetaKmsCrtc *crtc = meta_kms_page_flip_data_get_crtc (page_flip_data);
  CachedModeSet *cached_mode_set;
  g_autofree uint32_t *connectors = NULL;
  int n_connectors;
  uint32_t fb_id;
  uint32_t x, y;
  int fd;
  int ret;

  cached_mode_set = g_hash_table_lookup (impl_device_simple->cached_mode_sets,
                                         crtc);
  if (!cached_mode_set)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing mode set for page flip fallback");
      return FALSE;
    }

  if (!meta_drm_buffer_ensure_fb_id (plane_assignment->buffer, error))
    return FALSE;

  fill_connector_ids_array (cached_mode_set->connectors,
                            &connectors,
                            &n_connectors);

  fb_id = meta_drm_buffer_get_fb_id (plane_assignment->buffer);

  x = meta_fixed_16_to_int (plane_assignment->src_rect.x);
  y = meta_fixed_16_to_int (plane_assignment->src_rect.y);

  fd = meta_kms_impl_device_get_fd (impl_device);
  ret = drmModeSetCrtc (fd,
                        meta_kms_crtc_get_id (crtc),
                        fb_id,
                        x, y,
                        connectors, n_connectors,
                        cached_mode_set->drm_mode);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeSetCrtc mode '%s' on CRTC %u failed: %s",
                   cached_mode_set->drm_mode->name,
                   meta_kms_crtc_get_id (crtc),
                   g_strerror (-ret));
      return FALSE;
    }

  if (!impl_device_simple->mode_set_fallback_feedback_source)
    {
      MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
      MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
      GSource *source;

      source = meta_thread_impl_add_source (thread_impl,
                                            mode_set_fallback_feedback_idle,
                                            impl_device_simple,
                                            NULL);
      impl_device_simple->mode_set_fallback_feedback_source = source;
    }

  impl_device_simple->mode_set_fallback_page_flip_datas =
    g_list_prepend (impl_device_simple->mode_set_fallback_page_flip_datas,
                    page_flip_data);

  return TRUE;
}

static gboolean
symbolic_page_flip_idle (gpointer user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  MetaKmsImplDevice *impl_device;
  MetaKmsCrtc *crtc;

  impl_device = meta_kms_page_flip_data_get_impl_device (page_flip_data);
  crtc = meta_kms_page_flip_data_get_crtc (page_flip_data);

  meta_topic (META_DEBUG_KMS,
              "[simple] Handling symbolic page flip callback from %s, data: %p, CRTC: %u",
              meta_kms_impl_device_get_path (impl_device),
              page_flip_data,
              meta_kms_crtc_get_id (crtc));

  meta_kms_impl_device_handle_page_flip_callback (impl_device, page_flip_data);

  return G_SOURCE_REMOVE;
}

static gboolean
dispatch_page_flip (MetaKmsImplDevice    *impl_device,
                    MetaKmsUpdate        *update,
                    MetaKmsPageFlipData  *page_flip_data,
                    GError              **error)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);
  MetaKmsCrtc *crtc;
  MetaKmsPlaneAssignment *plane_assignment;
  g_autoptr (MetaKmsCustomPageFlip) custom_page_flip = NULL;
  int fd;
  int ret;

  crtc = meta_kms_page_flip_data_get_crtc (page_flip_data);
  plane_assignment = meta_kms_update_get_primary_plane_assignment (update,
                                                                   crtc);

  custom_page_flip = meta_kms_update_take_custom_page_flip_func (update);

  if (!plane_assignment && !custom_page_flip)
    {
      MetaKmsImpl *impl = meta_kms_impl_device_get_impl (impl_device);
      MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);
      GSource *source;

      meta_kms_page_flip_data_make_symbolic (page_flip_data);

      source = meta_thread_impl_add_source (thread_impl,
                                            symbolic_page_flip_idle,
                                            page_flip_data,
                                            NULL);

      g_source_set_ready_time (source, 0);
      g_source_unref (source);

      return TRUE;
    }

  if (plane_assignment && plane_assignment->buffer &&
      !meta_drm_buffer_ensure_fb_id (plane_assignment->buffer, error))
    return FALSE;

  fd = meta_kms_impl_device_get_fd (impl_device);
  if (custom_page_flip)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Invoking custom page flip on CRTC %u (%s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));
      ret = custom_page_flip->func (custom_page_flip->user_data,
                                    page_flip_data);
    }
  else
    {
      uint32_t fb_id;

      fb_id = meta_drm_buffer_get_fb_id (plane_assignment->buffer);

      meta_topic (META_DEBUG_KMS,
                  "[simple] Page flipping CRTC %u (%s) with %u, data: %p",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device),
                  fb_id,
                  page_flip_data);

      ret = drmModePageFlip (fd,
                             meta_kms_crtc_get_id (crtc),
                             fb_id,
                             DRM_MODE_PAGE_FLIP_EVENT,
                             page_flip_data);
    }

  if (ret == -EBUSY)
    {
      CachedModeSet *cached_mode_set;

      meta_topic (META_DEBUG_KMS,
                  "[simple] Scheduling page flip retry on CRTC %u (%s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));

      cached_mode_set = get_cached_mode_set (impl_device_simple, crtc);
      if (cached_mode_set)
        {
          uint32_t fb_id;
          drmModeModeInfo *drm_mode;
          float refresh_rate;

          if (plane_assignment)
            fb_id = meta_drm_buffer_get_fb_id (plane_assignment->buffer);
          else
            fb_id = 0;
          drm_mode = cached_mode_set->drm_mode;
          refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
          meta_kms_impl_device_hold_fd (impl_device);
          schedule_retry_page_flip (impl_device_simple,
                                    crtc,
                                    fb_id,
                                    refresh_rate,
                                    page_flip_data,
                                    g_steal_pointer (&custom_page_flip));
          return TRUE;
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Page flip of %u failed, and no mode set available",
                       meta_kms_crtc_get_id (crtc));
          return FALSE;
        }
    }
  else if (ret == -EINVAL)
    {
      meta_topic (META_DEBUG_KMS,
                  "[simple] Falling back to mode set on CRTC %u (%s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));

      return mode_set_fallback (impl_device_simple,
                                update,
                                plane_assignment,
                                page_flip_data,
                                error);
    }
  else if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModePageFlip on CRTC %u failed: %s",
                   meta_kms_crtc_get_id (crtc),
                   g_strerror (-ret));
      return FALSE;
    }
  else
    {
      meta_kms_impl_device_hold_fd (impl_device);

      impl_device_simple->posted_page_flip_datas =
        g_list_prepend (impl_device_simple->posted_page_flip_datas,
                        page_flip_data);

      return TRUE;
    }
}

static GList *
generate_page_flip_datas (MetaKmsImplDevice  *impl_device,
                          MetaKmsUpdate      *update)
{
  GList *listeners;
  GList *page_flip_datas = NULL;

  listeners = g_list_copy (meta_kms_update_get_page_flip_listeners (update));

  while (listeners)
    {
      MetaKmsPageFlipListener *listener = listeners->data;
      MetaKmsCrtc *crtc = listener->crtc;
      MetaKmsPageFlipData *page_flip_data;
      gpointer user_data;
      GDestroyNotify destroy_notify;
      GList *l;

      page_flip_data = meta_kms_page_flip_data_new (impl_device, crtc);
      page_flip_datas = g_list_append (page_flip_datas, page_flip_data);

      user_data = g_steal_pointer (&listener->user_data);
      destroy_notify = g_steal_pointer (&listener->destroy_notify);
      meta_kms_page_flip_data_add_listener (page_flip_data,
                                            listener->vtable,
                                            listener->main_context,
                                            user_data,
                                            destroy_notify);

      listeners = g_list_delete_link (listeners, listeners);

      l = listeners;
      while (l)
        {
          MetaKmsPageFlipListener *other_listener = l->data;
          GList *l_next = l->next;

          if (other_listener->crtc == crtc)
            {
              gpointer other_user_data;
              GDestroyNotify other_destroy_notify;

              other_user_data = g_steal_pointer (&other_listener->user_data);
              other_destroy_notify =
                g_steal_pointer (&other_listener->destroy_notify);
              meta_kms_page_flip_data_add_listener (page_flip_data,
                                                    other_listener->vtable,
                                                    other_listener->main_context,
                                                    other_user_data,
                                                    other_destroy_notify);
              listeners = g_list_delete_link (listeners, l);
            }

          l = l_next;
        }
    }

  return page_flip_datas;
}

static gboolean
maybe_dispatch_page_flips (MetaKmsImplDevice  *impl_device,
                           MetaKmsUpdate      *update,
                           GList             **failed_planes,
                           MetaKmsUpdateFlag   flags,
                           GError            **error)
{
  g_autolist (MetaKmsPageFlipData) page_flip_datas = NULL;

  page_flip_datas = generate_page_flip_datas (impl_device, update);

  while (page_flip_datas)
    {
      g_autoptr (GList) l = NULL;
      g_autoptr (MetaKmsPageFlipData) page_flip_data = NULL;

      l = page_flip_datas;
      page_flip_datas = g_list_remove_link (page_flip_datas, l);
      page_flip_data = g_steal_pointer (&l->data);

      if (!dispatch_page_flip (impl_device, update, page_flip_data, error))
        {
          if (!g_error_matches (*error,
                                G_IO_ERROR,
                                G_IO_ERROR_PERMISSION_DENIED))
            {
              MetaKmsCrtc *crtc =
                meta_kms_page_flip_data_get_crtc (page_flip_data);
              MetaKmsPlaneAssignment *plane_assignment;
              MetaKmsPlaneFeedback *plane_feedback;

              plane_assignment =
                meta_kms_update_get_primary_plane_assignment (update, crtc);

              plane_feedback =
                meta_kms_plane_feedback_new_take_error (plane_assignment->plane,
                                                        plane_assignment->crtc,
                                                        g_error_copy (*error));
              *failed_planes = g_list_prepend (*failed_planes, plane_feedback);
            }

          goto err;
        }
      else
        {
          meta_kms_page_flip_data_ref (page_flip_data);
        }
    }

  return TRUE;

err:
  g_list_free (page_flip_datas);

  return FALSE;
}

static gboolean
process_entries (MetaKmsImplDevice         *impl_device,
                 MetaKmsUpdate             *update,
                 GList                     *entries,
                 MetaKmsSimpleProcessFunc   func,
                 GError                   **error)
{
  GList *l;

  for (l = entries; l; l = l->next)
    {
      if (!func (impl_device, update, l->data, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
process_cursor_plane_assignment (MetaKmsImplDevice       *impl_device,
                                 MetaKmsUpdate           *update,
                                 MetaKmsPlaneAssignment  *plane_assignment,
                                 GError                 **error)
{
  uint32_t crtc_id;
  int fd;

  crtc_id = meta_kms_crtc_get_id (plane_assignment->crtc),
  fd = meta_kms_impl_device_get_fd (impl_device);

  if (!(plane_assignment->flags & META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED))
    {
      int width, height;
      int ret = -1;
      uint32_t handle_u32;

      width = plane_assignment->dst_rect.width;
      height = plane_assignment->dst_rect.height;

      if (plane_assignment->buffer)
        {
          if (!meta_drm_buffer_ensure_fb_id (plane_assignment->buffer, error))
            return FALSE;

          handle_u32 = meta_drm_buffer_get_handle (plane_assignment->buffer);
        }
      else
        {
          handle_u32 = 0;
        }

      meta_topic (META_DEBUG_KMS,
                  "[simple] Setting HW cursor of CRTC %u (%s) to %u "
                  "(size: %dx%d, hot: (%d, %d))",
                  crtc_id,
                  meta_kms_impl_device_get_path (impl_device),
                  handle_u32,
                  width, height,
                  plane_assignment->cursor_hotspot.x,
                  plane_assignment->cursor_hotspot.y);

      if (plane_assignment->cursor_hotspot.is_valid)
        {
          ret = drmModeSetCursor2 (fd,
                                   crtc_id,
                                   handle_u32,
                                   width, height,
                                   plane_assignment->cursor_hotspot.x,
                                   plane_assignment->cursor_hotspot.y);
        }

      if (ret != 0)
        {
          ret = drmModeSetCursor (fd, crtc_id,
                                  handle_u32,
                                  width, height);
        }

      if (ret != 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                       "drmModeSetCursor failed: %s", g_strerror (-ret));
          return FALSE;
        }
    }

  meta_topic (META_DEBUG_KMS,
              "[simple] Moving HW cursor of CRTC %u (%s) to (%d, %d)",
              crtc_id,
              meta_kms_impl_device_get_path (impl_device),
              plane_assignment->dst_rect.x,
              plane_assignment->dst_rect.y);

  drmModeMoveCursor (fd,
                     crtc_id,
                     plane_assignment->dst_rect.x,
                     plane_assignment->dst_rect.y);

  return TRUE;
}

static gboolean
process_plane_assignment (MetaKmsImplDevice       *impl_device,
                          MetaKmsUpdate           *update,
                          MetaKmsPlaneAssignment  *plane_assignment,
                          MetaKmsPlaneFeedback   **plane_feedback)
{
  MetaKmsPlane *plane;
  MetaKmsPlaneType plane_type;
  GError *error = NULL;

  plane = plane_assignment->plane;
  plane_type = meta_kms_plane_get_plane_type (plane);
  switch (plane_type)
    {
    case META_KMS_PLANE_TYPE_PRIMARY:
      /* Handled as part of the mode-set and page flip. */
      return TRUE;
    case META_KMS_PLANE_TYPE_CURSOR:
      if (!process_cursor_plane_assignment (impl_device, update,
                                            plane_assignment,
                                            &error))
        {
          *plane_feedback =
            meta_kms_plane_feedback_new_take_error (plane,
                                                    plane_assignment->crtc,
                                                    g_steal_pointer (&error));
          return FALSE;
        }
      else
        {
          return TRUE;
        }
    case META_KMS_PLANE_TYPE_OVERLAY:
      error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED,
                                   "Overlay planes cannot be assigned");
      *plane_feedback =
        meta_kms_plane_feedback_new_take_error (plane,
                                                plane_assignment->crtc,
                                                g_steal_pointer (&error));
      return TRUE;
    }

  g_assert_not_reached ();
}

static gboolean
process_plane_assignments (MetaKmsImplDevice  *impl_device,
                           MetaKmsUpdate      *update,
                           GList             **failed_planes,
                           GError            **error)
{
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;
      MetaKmsPlaneFeedback *plane_feedback;

      if (!process_plane_assignment (impl_device, update, plane_assignment,
                                     &plane_feedback))
        {
          if (g_error_matches (plane_feedback->error,
                               G_IO_ERROR,
                               G_IO_ERROR_PERMISSION_DENIED))
            {
              g_propagate_error (error,
                                 g_steal_pointer (&plane_feedback->error));
              meta_kms_plane_feedback_free (plane_feedback);
              return FALSE;
            }

          *failed_planes = g_list_prepend (*failed_planes, plane_feedback);
          if (plane_assignment->flags & META_KMS_ASSIGN_PLANE_FLAG_ALLOW_FAIL)
            {
              continue;
            }
          else
            {
              g_propagate_error (error, g_error_copy (plane_feedback->error));
              return FALSE;
            }
        }
    }

  return TRUE;
}

static void
page_flip_handler (int           fd,
                   unsigned int  sequence,
                   unsigned int  tv_sec,
                   unsigned int  tv_usec,
                   void         *user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  MetaKmsImplDevice *impl_device;
  MetaKmsImplDeviceSimple *impl_device_simple;
  MetaKmsCrtc *crtc;
  uint32_t crtc_id;

  meta_kms_page_flip_data_set_timings_in_impl (page_flip_data,
                                               sequence, tv_sec, tv_usec);

  impl_device = meta_kms_page_flip_data_get_impl_device (page_flip_data);
  impl_device_simple = META_KMS_IMPL_DEVICE_SIMPLE (impl_device);
  crtc = meta_kms_page_flip_data_get_crtc (page_flip_data);
  crtc_id = meta_kms_crtc_get_id (crtc);

  COGL_TRACE_MESSAGE ("page_flip_handler()",
                      "[simple] Page flip callback for CRTC (%u, %s)",
                      crtc_id, meta_kms_impl_device_get_path (impl_device));

  meta_topic (META_DEBUG_KMS,
              "[simple] Handling page flip callback from %s, data: %p, CRTC: %u",
              meta_kms_impl_device_get_path (impl_device),
              page_flip_data, crtc_id);

  meta_kms_impl_device_unhold_fd (impl_device);

  meta_kms_impl_device_handle_page_flip_callback (impl_device, page_flip_data);
  impl_device_simple->posted_page_flip_datas =
    g_list_remove (impl_device_simple->posted_page_flip_datas,
                   page_flip_data);
}

static void
meta_kms_impl_device_simple_setup_drm_event_context (MetaKmsImplDevice *impl_device,
                                                     drmEventContext   *drm_event_context)
{
  drm_event_context->version = 2;
  drm_event_context->page_flip_handler = page_flip_handler;
}

static MetaKmsFeedback *
perform_update_test (MetaKmsImplDevice *impl_device,
                     MetaKmsUpdate     *update)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);
  GList *failed_planes = NULL;
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;
      MetaKmsPlane *plane = plane_assignment->plane;
      MetaKmsCrtc *crtc = plane_assignment->crtc;
      MetaDrmBuffer *buffer = plane_assignment->buffer;
      CachedModeSet *cached_mode_set;
      g_autoptr (GError) error = NULL;

      if (!plane_assignment->crtc ||
          !plane_assignment->buffer)
        continue;

      cached_mode_set = get_cached_mode_set (impl_device_simple,
                                             plane_assignment->crtc);
      if (!cached_mode_set)
        {
          MetaKmsPlaneFeedback *plane_feedback;

          plane_feedback =
            meta_kms_plane_feedback_new_failed (plane, crtc,
                                                "No existing mode set");
          failed_planes = g_list_append (failed_planes, plane_feedback);
          continue;
        }

      if (!meta_drm_buffer_ensure_fb_id (plane_assignment->buffer, &error))
        {
          MetaKmsPlaneFeedback *plane_feedback;

          plane_feedback =
            meta_kms_plane_feedback_new_take_error (plane, crtc,
                                                    g_steal_pointer (&error));
          failed_planes = g_list_append (failed_planes, plane_feedback);
          continue;
        }

      if (meta_drm_buffer_get_width (buffer) != cached_mode_set->width ||
          meta_drm_buffer_get_height (buffer) != cached_mode_set->height ||
          meta_drm_buffer_get_stride (buffer) != cached_mode_set->stride ||
          meta_drm_buffer_get_format (buffer) != cached_mode_set->format ||
          meta_drm_buffer_get_modifier (buffer) != cached_mode_set->modifier)
        {
          MetaKmsPlaneFeedback *plane_feedback;

          plane_feedback =
            meta_kms_plane_feedback_new_failed (plane, crtc,
                                                "Incompatible buffer");
          failed_planes = g_list_append (failed_planes, plane_feedback);
          continue;
        }
    }

  if (failed_planes)
    {
      GError *error;

      error = g_error_new_literal (G_IO_ERROR,
                                   G_IO_ERROR_FAILED,
                                   "One or more buffers incompatible");
      return meta_kms_feedback_new_failed (failed_planes, error);
    }
  else
    {
      return meta_kms_feedback_new_passed (NULL);
    }
}

static MetaKmsFeedback *
meta_kms_impl_device_simple_process_update (MetaKmsImplDevice *impl_device,
                                            MetaKmsUpdate     *update,
                                            MetaKmsUpdateFlag  flags)
{
  GError *error = NULL;
  GList *failed_planes = NULL;

  meta_topic (META_DEBUG_KMS, "[simple] Processing update");

  if (flags & META_KMS_UPDATE_FLAG_TEST_ONLY)
    return perform_update_test (impl_device, update);

  if (!process_entries (impl_device,
                        update,
                        meta_kms_update_get_mode_sets (update),
                        process_mode_set,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        meta_kms_update_get_connector_updates (update),
                        process_connector_update,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        meta_kms_update_get_crtc_color_updates (update),
                        process_crtc_color_updates,
                        &error))
    goto err;

  if (!process_entries (impl_device,
                        update,
                        meta_kms_update_get_crtc_updates (update),
                        process_crtc_update,
                        &error))
    goto err;

  if (!process_plane_assignments (impl_device, update, &failed_planes, &error))
    goto err;

  if (!maybe_dispatch_page_flips (impl_device, update, &failed_planes, flags,
                                  &error))
    goto err;

  return meta_kms_feedback_new_passed (failed_planes);

err:
  return meta_kms_feedback_new_failed (failed_planes, error);
}

static gboolean
set_dpms_to_off (MetaKmsImplDevice  *impl_device,
                 GError            **error)
{
  GList *l;

  for (l = meta_kms_impl_device_peek_connectors (impl_device); l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;

      meta_topic (META_DEBUG_KMS,
                  "[simple] Setting DPMS of connector %u (%s) to OFF",
                  meta_kms_connector_get_id (connector),
                  meta_kms_impl_device_get_path (impl_device));

      if (!set_connector_property (impl_device,
                                   connector,
                                   META_KMS_CONNECTOR_PROP_DPMS,
                                   DRM_MODE_DPMS_OFF,
                                   error))
        return FALSE;
    }

  return TRUE;
}

static void
meta_kms_impl_device_simple_disable (MetaKmsImplDevice *impl_device)
{
  g_autoptr (GError) error = NULL;

  meta_topic (META_DEBUG_KMS, "[simple] Disabling '%s'",
              meta_kms_impl_device_get_path (impl_device));

  if (!set_dpms_to_off (impl_device, &error))
    {
      g_warning ("Failed to set DPMS to off on device '%s': %s",
                 meta_kms_impl_device_get_path (impl_device),
                 error->message);
    }
}

static void
flush_postponed_page_flip_datas (MetaKmsImplDeviceSimple *impl_device_simple)
{
  dispatch_page_flip_datas (&impl_device_simple->postponed_page_flip_datas,
                            (GFunc) meta_kms_page_flip_data_flipped_in_impl,
                            NULL);
  dispatch_page_flip_datas (&impl_device_simple->postponed_mode_set_fallback_datas,
                            (GFunc) meta_kms_page_flip_data_mode_set_fallback_in_impl,
                            NULL);
}

static void
meta_kms_impl_device_simple_handle_page_flip_callback (MetaKmsImplDevice   *impl_device,
                                                       MetaKmsPageFlipData *page_flip_data)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);

  if (impl_device_simple->pending_page_flip_retries)
    {
      impl_device_simple->postponed_page_flip_datas =
        g_list_append (impl_device_simple->postponed_page_flip_datas,
                       page_flip_data);
    }
  else
    {
      meta_kms_page_flip_data_flipped_in_impl (page_flip_data);
    }
}

static void
dispose_page_flip_data (MetaKmsPageFlipData *page_flip_data,
                        MetaKmsImplDevice   *impl_device)
{
  meta_kms_page_flip_data_discard_in_impl (page_flip_data, NULL);
  meta_kms_impl_device_unhold_fd (impl_device);
}

static void
meta_kms_impl_device_simple_discard_pending_page_flips (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);
  GList *l;

  if (!impl_device_simple->pending_page_flip_retries)
    return;

  for (l = impl_device_simple->pending_page_flip_retries; l; l = l->next)
    {
      RetryPageFlipData *retry_page_flip_data = l->data;
      MetaKmsPageFlipData *page_flip_data;

      page_flip_data = g_steal_pointer (&retry_page_flip_data->page_flip_data);

      meta_topic (META_DEBUG_KMS,
                  "[simple] Discarding page flip retry for CRTC %u (%s)",
                  meta_kms_crtc_get_id (
                    meta_kms_page_flip_data_get_crtc (page_flip_data)),
                  meta_kms_impl_device_get_path (
                    meta_kms_page_flip_data_get_impl_device (page_flip_data)));

      dispose_page_flip_data (page_flip_data, impl_device);
      retry_page_flip_data_free (retry_page_flip_data);
    }
  g_clear_pointer (&impl_device_simple->pending_page_flip_retries, g_list_free);

  g_clear_pointer (&impl_device_simple->retry_page_flips_source,
                   g_source_destroy);
}

static void
meta_kms_impl_device_simple_prepare_shutdown (MetaKmsImplDevice *impl_device)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (impl_device);

  g_list_foreach (impl_device_simple->posted_page_flip_datas,
                  (GFunc) dispose_page_flip_data,
                  impl_device);
  g_clear_list (&impl_device_simple->posted_page_flip_datas, NULL);
}

static void
meta_kms_impl_device_simple_finalize (GObject *object)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (object);
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (impl_device_simple);

  g_list_free_full (impl_device_simple->pending_page_flip_retries,
                    (GDestroyNotify) retry_page_flip_data_free);
  dispatch_page_flip_datas (&impl_device_simple->postponed_page_flip_datas,
                            (GFunc) dispose_page_flip_data,
                            impl_device);
  dispatch_page_flip_datas (&impl_device_simple->postponed_mode_set_fallback_datas,
                            (GFunc) dispose_page_flip_data,
                            impl_device);

  g_assert (!impl_device_simple->posted_page_flip_datas);

  g_clear_pointer (&impl_device_simple->mode_set_fallback_feedback_source,
                   g_source_destroy);
  g_clear_pointer (&impl_device_simple->cached_mode_sets, g_hash_table_destroy);

  G_OBJECT_CLASS (meta_kms_impl_device_simple_parent_class)->finalize (object);
}

static MetaDeviceFile *
meta_kms_impl_device_simple_open_device_file (MetaKmsImplDevice  *impl_device,
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
                                 META_KMS_DEVICE_FILE_TAG_SIMPLE))
    {
      int fd = meta_device_file_get_fd (device_file);

      g_warn_if_fail (!meta_device_file_has_tag (device_file,
                                                 META_DEVICE_FILE_TAG_KMS,
                                                 META_KMS_DEVICE_FILE_TAG_ATOMIC));

      if (drmSetClientCap (fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
        {
          g_set_error (error, META_KMS_ERROR, META_KMS_ERROR_NOT_SUPPORTED,
                       "DRM_CLIENT_CAP_UNIVERSAL_PLANES not supported");
          return NULL;
        }

      meta_device_file_tag (device_file,
                            META_DEVICE_FILE_TAG_KMS,
                            META_KMS_DEVICE_FILE_TAG_SIMPLE);
    }

  return g_steal_pointer (&device_file);
}

static gboolean
meta_kms_impl_device_simple_initable_init (GInitable     *initable,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  MetaKmsImplDeviceSimple *impl_device_simple =
    META_KMS_IMPL_DEVICE_SIMPLE (initable);
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (impl_device_simple);
  MetaKmsDevice *device = meta_kms_impl_device_get_device (impl_device);
  GList *l;

  if (!initable_parent_iface->init (initable, cancellable, error))
    return FALSE;

  if (!meta_kms_impl_device_init_mode_setting (impl_device, error))
    return FALSE;

  impl_device_simple->cached_mode_sets =
    g_hash_table_new_full (NULL,
                           NULL,
                           NULL,
                           (GDestroyNotify) cached_mode_set_free);

  for (l = meta_kms_device_get_crtcs (device); l; l = l->next)
    {
      MetaKmsCrtc *crtc = l->data;

      if (meta_kms_device_has_cursor_plane_for (device, crtc))
        continue;

      meta_topic (META_DEBUG_KMS,
                  "[simple] Adding fake cursor plane for CRTC %u (%s)",
                  meta_kms_crtc_get_id (crtc),
                  meta_kms_impl_device_get_path (impl_device));

      meta_kms_device_add_fake_plane_in_impl (device,
                                              META_KMS_PLANE_TYPE_CURSOR,
                                              crtc);
    }

  g_message ("Added device '%s' (%s) using non-atomic mode setting.",
             meta_kms_impl_device_get_path (impl_device),
             meta_kms_impl_device_get_driver_name (impl_device));

  return TRUE;
}

static void
meta_kms_impl_device_simple_init (MetaKmsImplDeviceSimple *impl_device_simple)
{
}

static void
initable_iface_init (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_kms_impl_device_simple_initable_init;
}

static void
meta_kms_impl_device_simple_class_init (MetaKmsImplDeviceSimpleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaKmsImplDeviceClass *impl_device_class =
    META_KMS_IMPL_DEVICE_CLASS (klass);

  object_class->finalize = meta_kms_impl_device_simple_finalize;

  impl_device_class->open_device_file =
    meta_kms_impl_device_simple_open_device_file;
  impl_device_class->setup_drm_event_context =
    meta_kms_impl_device_simple_setup_drm_event_context;
  impl_device_class->process_update =
    meta_kms_impl_device_simple_process_update;
  impl_device_class->disable =
    meta_kms_impl_device_simple_disable;
  impl_device_class->handle_page_flip_callback =
    meta_kms_impl_device_simple_handle_page_flip_callback;
  impl_device_class->discard_pending_page_flips =
    meta_kms_impl_device_simple_discard_pending_page_flips;
  impl_device_class->prepare_shutdown =
    meta_kms_impl_device_simple_prepare_shutdown;
}
