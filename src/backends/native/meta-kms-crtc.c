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

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-crtc-private.h"

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "backends/native/meta-kms-impl-device-simple.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-kms-utils.h"

#define DEADLINE_EVASION_US 800
#define DEADLINE_EVASION_WITH_KMS_TOPIC_US 1000

#define MINIMUM_REFRESH_RATE 30.f

typedef struct _MetaKmsCrtcPropTable
{
  MetaKmsProp props[META_KMS_CRTC_N_PROPS];
} MetaKmsCrtcPropTable;

struct _MetaKmsCrtc
{
  GObject parent;

  MetaKmsDevice *device;

  uint32_t id;
  int idx;

  MetaKmsCrtcState current_state;

  MetaKmsCrtcPropTable prop_table;
};

G_DEFINE_TYPE (MetaKmsCrtc, meta_kms_crtc, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_crtc_get_device (MetaKmsCrtc *crtc)
{
  return crtc->device;
}

const MetaKmsCrtcState *
meta_kms_crtc_get_current_state (MetaKmsCrtc *crtc)
{
  return &crtc->current_state;
}

uint32_t
meta_kms_crtc_get_id (MetaKmsCrtc *crtc)
{
  return crtc->id;
}

int
meta_kms_crtc_get_idx (MetaKmsCrtc *crtc)
{
  return crtc->idx;
}

uint32_t
meta_kms_crtc_get_prop_id (MetaKmsCrtc     *crtc,
                           MetaKmsCrtcProp  prop)
{
  return crtc->prop_table.props[prop].prop_id;
}

const char *
meta_kms_crtc_get_prop_name (MetaKmsCrtc     *crtc,
                             MetaKmsCrtcProp  prop)
{
  return crtc->prop_table.props[prop].name;
}

uint64_t
meta_kms_crtc_get_prop_drm_value (MetaKmsCrtc     *crtc,
                                  MetaKmsCrtcProp  property,
                                  uint64_t         value)
{
  MetaKmsProp *prop = &crtc->prop_table.props[property];
  return meta_kms_prop_convert_value (prop, value);
}

gboolean
meta_kms_crtc_is_active (MetaKmsCrtc *crtc)
{
  return crtc->current_state.is_active;
}

static void
read_crtc_gamma (MetaKmsCrtc       *crtc,
                 MetaKmsCrtcState  *crtc_state,
                 MetaKmsImplDevice *impl_device,
                 drmModeCrtc       *drm_crtc)
{
  MetaKmsProp *prop_lut;
  MetaKmsProp *prop_size;
  uint64_t blob_id;
  uint64_t lut_size;
  int fd;
  drmModePropertyBlobPtr blob;
  int drm_lut_size;
  struct drm_color_lut *drm_lut;
  int i;

  prop_lut = &crtc->prop_table.props[META_KMS_CRTC_PROP_GAMMA_LUT];
  prop_size = &crtc->prop_table.props[META_KMS_CRTC_PROP_GAMMA_LUT_SIZE];

  if (!prop_lut->prop_id || !prop_size->prop_id)
    return;

  lut_size = prop_size->value;
  if (lut_size <= 0)
    return;

  crtc_state->gamma.size = lut_size;
  crtc_state->gamma.supported = TRUE;

  blob_id = prop_lut->value;
  if (blob_id == 0)
    return;

  fd = meta_kms_impl_device_get_fd (impl_device);
  blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!blob)
    return;

  drm_lut_size = blob->length / sizeof (struct drm_color_lut);
  if (drm_lut_size == 0)
    {
      drmModeFreePropertyBlob (blob);
      return;
    }

  drm_lut = blob->data;

  crtc_state->gamma.value = meta_gamma_lut_new_sized (drm_lut_size);

  for (i = 0; i < drm_lut_size; i++)
    {
      crtc_state->gamma.value->red[i] = drm_lut[i].red;
      crtc_state->gamma.value->green[i] = drm_lut[i].green;
      crtc_state->gamma.value->blue[i] = drm_lut[i].blue;
    }

  drmModeFreePropertyBlob (blob);
}

static void
read_crtc_legacy_gamma (MetaKmsCrtc       *crtc,
                        MetaKmsCrtcState  *crtc_state,
                        MetaKmsImplDevice *impl_device,
                        drmModeCrtc       *drm_crtc)
{
  crtc_state->gamma.size = drm_crtc->gamma_size;
  crtc_state->gamma.supported = drm_crtc->gamma_size != 0;
  crtc_state->gamma.value = meta_gamma_lut_new_sized (drm_crtc->gamma_size);

  drmModeCrtcGetGamma (meta_kms_impl_device_get_fd (impl_device),
                       crtc->id,
                       crtc_state->gamma.value->size,
                       crtc_state->gamma.value->red,
                       crtc_state->gamma.value->green,
                       crtc_state->gamma.value->blue);

  if (meta_gamma_lut_is_identity (crtc_state->gamma.value))
    g_clear_pointer (&crtc_state->gamma.value, meta_gamma_lut_free);
}

static void
read_gamma_state (MetaKmsCrtc       *crtc,
                  MetaKmsCrtcState  *crtc_state,
                  MetaKmsImplDevice *impl_device,
                  drmModeCrtc       *drm_crtc)
{
  g_assert_null (crtc_state->gamma.value);

  crtc_state->gamma.size = 0;
  crtc_state->gamma.supported = FALSE;

  if (META_IS_KMS_IMPL_DEVICE_ATOMIC (impl_device))
    read_crtc_gamma (crtc, crtc_state, impl_device, drm_crtc);
  else if (META_IS_KMS_IMPL_DEVICE_SIMPLE (impl_device))
    read_crtc_legacy_gamma (crtc, crtc_state, impl_device, drm_crtc);
}

static gboolean
gamma_equal (MetaKmsCrtcState *state,
             MetaKmsCrtcState *other_state)
{
  return state->gamma.size == other_state->gamma.size &&
         state->gamma.supported == other_state->gamma.supported &&
         meta_gamma_lut_equal (state->gamma.value, other_state->gamma.value);
}

static MetaKmsResourceChanges
meta_kms_crtc_state_changes (MetaKmsCrtcState *state,
                             MetaKmsCrtcState *other_state)
{
  if (state->is_active != other_state->is_active)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!mtk_rectangle_equal (&state->rect, &other_state->rect))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->is_drm_mode_valid != other_state->is_drm_mode_valid)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!meta_drm_mode_equal (&state->drm_mode, &other_state->drm_mode))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->vrr_enabled != other_state->vrr_enabled)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!gamma_equal (state, other_state))
    return META_KMS_RESOURCE_CHANGE_GAMMA;

  return META_KMS_RESOURCE_CHANGE_NONE;
}

static MetaKmsResourceChanges
meta_kms_crtc_read_state (MetaKmsCrtc             *crtc,
                          MetaKmsImplDevice       *impl_device,
                          drmModeCrtc             *drm_crtc,
                          drmModeObjectProperties *drm_props)
{
  MetaKmsCrtcState crtc_state = {0};
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  MetaKmsProp *prop;

  meta_kms_impl_device_update_prop_table (impl_device,
                                          drm_props->props,
                                          drm_props->prop_values,
                                          drm_props->count_props,
                                          crtc->prop_table.props,
                                          META_KMS_CRTC_N_PROPS);

  crtc_state.rect = (MtkRectangle) {
    .x = drm_crtc->x,
    .y = drm_crtc->y,
    .width = drm_crtc->width,
    .height = drm_crtc->height,
  };

  crtc_state.is_drm_mode_valid = drm_crtc->mode_valid;
  crtc_state.drm_mode = drm_crtc->mode;

  prop = &crtc->prop_table.props[META_KMS_CRTC_PROP_ACTIVE];

  if (prop->prop_id)
    crtc_state.is_active = !!prop->value;
  else
    crtc_state.is_active = drm_crtc->mode_valid;

  prop = &crtc->prop_table.props[META_KMS_CRTC_PROP_VRR_ENABLED];
  if (prop->prop_id)
    crtc_state.vrr_enabled = !!prop->value;

  read_gamma_state (crtc, &crtc_state, impl_device, drm_crtc);

  if (!crtc_state.is_active)
    {
      if (crtc->current_state.is_active)
        changes |= META_KMS_RESOURCE_CHANGE_FULL;
    }
  else
    {
      changes = meta_kms_crtc_state_changes (&crtc->current_state, &crtc_state);
    }

  g_clear_pointer (&crtc->current_state.gamma.value,
                   meta_gamma_lut_free);
  crtc->current_state = crtc_state;

  meta_topic (META_DEBUG_KMS,
              "Read CRTC %u state: active: %d, mode: %s, changed: %s",
              crtc->id, crtc->current_state.is_active,
              crtc->current_state.is_drm_mode_valid
                ? crtc->current_state.drm_mode.name
                : "(nil)",
              changes == META_KMS_RESOURCE_CHANGE_NONE
                ? "no"
                : "yes");

  return changes;
}

MetaKmsResourceChanges
meta_kms_crtc_update_state_in_impl (MetaKmsCrtc *crtc)
{
  MetaKmsImplDevice *impl_device;
  MetaKmsResourceChanges changes;
  int fd;
  drmModeCrtc *drm_crtc;
  drmModeObjectProperties *drm_props;

  impl_device = meta_kms_device_get_impl_device (crtc->device);
  fd = meta_kms_impl_device_get_fd (impl_device);

  drm_crtc = drmModeGetCrtc (fd, crtc->id);
  drm_props = drmModeObjectGetProperties (fd, crtc->id, DRM_MODE_OBJECT_CRTC);

  if (!drm_crtc || !drm_props)
    {
      crtc->current_state.is_active = FALSE;
      crtc->current_state.rect = (MtkRectangle) { };
      crtc->current_state.is_drm_mode_valid = FALSE;
      changes = META_KMS_RESOURCE_CHANGE_FULL;
      goto out;
    }

  changes = meta_kms_crtc_read_state (crtc, impl_device, drm_crtc, drm_props);

out:
  g_clear_pointer (&drm_props, drmModeFreeObjectProperties);
  g_clear_pointer (&drm_crtc, drmModeFreeCrtc);

  return changes;
}

void
meta_kms_crtc_disable_in_impl (MetaKmsCrtc *crtc)
{
  crtc->current_state.is_active = FALSE;
  crtc->current_state.rect = (MtkRectangle) { 0 };
  crtc->current_state.is_drm_mode_valid = FALSE;
  crtc->current_state.drm_mode = (drmModeModeInfo) { 0 };
}

void
meta_kms_crtc_predict_state_in_impl (MetaKmsCrtc   *crtc,
                                     MetaKmsUpdate *update)
{
  GList *mode_sets;
  GList *crtc_updates;
  GList *crtc_color_updates;
  GList *l;

  mode_sets = meta_kms_update_get_mode_sets (update);
  for (l = mode_sets; l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;

      if (mode_set->crtc != crtc)
        continue;

      if (mode_set->mode)
        {
          MetaKmsPlaneAssignment *plane_assignment;
          const drmModeModeInfo *drm_mode;

          plane_assignment =
            meta_kms_update_get_primary_plane_assignment (update, crtc);
          drm_mode = meta_kms_mode_get_drm_mode (mode_set->mode);

          crtc->current_state.is_active = TRUE;
          crtc->current_state.rect =
            meta_fixed_16_rectangle_to_rectangle (plane_assignment->src_rect);
          crtc->current_state.is_drm_mode_valid = TRUE;
          crtc->current_state.drm_mode = *drm_mode;
        }
      else
        {
          crtc->current_state.is_active = FALSE;
          crtc->current_state.rect = (MtkRectangle) { 0 };
          crtc->current_state.is_drm_mode_valid = FALSE;
          crtc->current_state.drm_mode = (drmModeModeInfo) { 0 };
        }

      break;
    }

  crtc_updates = meta_kms_update_get_crtc_updates (update);
  for (l = crtc_updates; l; l = l->next)
    {
      MetaKmsCrtcUpdate *crtc_update = l->data;

      if (crtc_update->crtc != crtc)
        continue;

      if (crtc_update->vrr.has_update)
        crtc->current_state.vrr_enabled = !!crtc_update->vrr.is_enabled;

      break;
    }

  crtc_color_updates = meta_kms_update_get_crtc_color_updates (update);
  for (l = crtc_color_updates; l; l = l->next)
    {
      MetaKmsCrtcColorUpdate *color_update = l->data;
      MetaGammaLut *gamma = color_update->gamma.state;

      if (color_update->crtc != crtc)
        continue;

      if (color_update->gamma.has_update)
        {
          if (gamma)
            gamma = meta_gamma_lut_copy (gamma);

          g_clear_pointer (&crtc->current_state.gamma.value, meta_gamma_lut_free);
          crtc->current_state.gamma.value = gamma;
        }
      break;
    }
}

static void
init_properties (MetaKmsCrtc       *crtc,
                 MetaKmsImplDevice *impl_device,
                 drmModeCrtc       *drm_crtc)
{
  MetaKmsCrtcPropTable *prop_table = &crtc->prop_table;

  *prop_table = (MetaKmsCrtcPropTable) {
    .props = {
      [META_KMS_CRTC_PROP_MODE_ID] =
        {
          .name = "MODE_ID",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CRTC_PROP_ACTIVE] =
        {
          .name = "ACTIVE",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CRTC_PROP_GAMMA_LUT] =
        {
          .name = "GAMMA_LUT",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CRTC_PROP_GAMMA_LUT_SIZE] =
        {
          .name = "GAMMA_LUT_SIZE",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CRTC_PROP_VRR_ENABLED] =
        {
          .name = "VRR_ENABLED",
          .type = DRM_MODE_PROP_RANGE,
        },
    }
  };
}

MetaKmsCrtc *
meta_kms_crtc_new (MetaKmsImplDevice  *impl_device,
                   drmModeCrtc        *drm_crtc,
                   int                 idx,
                   GError            **error)
{
  int fd;
  drmModeObjectProperties *drm_props;
  MetaKmsCrtc *crtc;

  fd = meta_kms_impl_device_get_fd (impl_device);
  drm_props = drmModeObjectGetProperties (fd, drm_crtc->crtc_id,
                                          DRM_MODE_OBJECT_CRTC);
  if (!drm_props)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "drmModeObjectGetProperties: %s", g_strerror (errno));
      return NULL;
    }

  crtc = g_object_new (META_TYPE_KMS_CRTC, NULL);
  crtc->device = meta_kms_impl_device_get_device (impl_device);
  crtc->id = drm_crtc->crtc_id;
  crtc->idx = idx;

  init_properties (crtc, impl_device, drm_crtc);

  meta_kms_crtc_read_state (crtc, impl_device, drm_crtc, drm_props);

  drmModeFreeObjectProperties (drm_props);

  return crtc;
}

static void
meta_kms_crtc_finalize (GObject *object)
{
  MetaKmsCrtc *crtc = META_KMS_CRTC (object);

  g_clear_pointer (&crtc->current_state.gamma.value, meta_gamma_lut_free);

  G_OBJECT_CLASS (meta_kms_crtc_parent_class)->finalize (object);
}

static void
meta_kms_crtc_init (MetaKmsCrtc *crtc)
{
  crtc->current_state.gamma.size = 0;
  crtc->current_state.gamma.value = NULL;
}

static void
meta_kms_crtc_class_init (MetaKmsCrtcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_crtc_finalize;
}

static drmVBlankSeqType
get_crtc_type_bitmask (MetaKmsCrtc *crtc)
{
  if (crtc->idx > 1)
    {
      return ((crtc->idx << DRM_VBLANK_HIGH_CRTC_SHIFT) &
              DRM_VBLANK_HIGH_CRTC_MASK);
    }
  else if (crtc->idx > 0)
    {
      return DRM_VBLANK_SECONDARY;
    }
  else
    {
      return 0;
    }
}

gboolean
meta_kms_crtc_determine_deadline (MetaKmsCrtc  *crtc,
                                  int64_t      *out_next_deadline_us,
                                  int64_t      *out_next_presentation_us,
                                  GError      **error)
{
  MetaKmsImplDevice *impl_device;
  int fd;
  drmVBlank vblank;
  int ret;
  int64_t next_presentation_us;
  int64_t next_deadline_us;

  if (!crtc->current_state.is_drm_mode_valid)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Mode invalid");
      return FALSE;
    }

  impl_device = meta_kms_device_get_impl_device (crtc->device);
  fd = meta_kms_impl_device_get_fd (impl_device);

  vblank = (drmVBlank) {
    .request.type = DRM_VBLANK_RELATIVE | get_crtc_type_bitmask (crtc),
    .request.sequence = 0,
    .request.signal = 0,
  };

  ret = drmWaitVBlank (fd, &vblank);
  if (ret != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmWaitVBlank failed: %s", g_strerror (-ret));
      return FALSE;
    }

  if (crtc->current_state.vrr_enabled)
    {
      next_presentation_us = 0;
      next_deadline_us =
        s2us (vblank.reply.tval_sec) + vblank.reply.tval_usec + 0.5 +
        G_USEC_PER_SEC / MINIMUM_REFRESH_RATE;
    }
  else
    {
      drmModeModeInfo *drm_mode;
      int64_t vblank_duration_us;
      int64_t deadline_evasion_us;

      drm_mode = &crtc->current_state.drm_mode;

      next_presentation_us =
        s2us (vblank.reply.tval_sec) + vblank.reply.tval_usec + 0.5 +
        G_USEC_PER_SEC / meta_calculate_drm_mode_refresh_rate (drm_mode);

      /*
       *                         1
       * time per pixel = -----------------
       *                   Pixel clock (Hz)
       *
       * number of pixels = vdisplay * htotal
       *
       * time spent scanning out = time per pixel * number of pixels
       *
       */

      if (meta_is_topic_enabled (META_DEBUG_KMS))
        deadline_evasion_us = DEADLINE_EVASION_WITH_KMS_TOPIC_US;
      else
        deadline_evasion_us = DEADLINE_EVASION_US;

      vblank_duration_us = meta_calculate_drm_mode_vblank_duration_us (drm_mode);
      next_deadline_us = next_presentation_us - (vblank_duration_us +
                                                 deadline_evasion_us);
    }

  *out_next_presentation_us = next_presentation_us;
  *out_next_deadline_us = next_deadline_us;

  return TRUE;
}
