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

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-crtc-private.h"

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-update-private.h"

struct _MetaKmsCrtc
{
  GObject parent;

  MetaKmsDevice *device;

  uint32_t id;
  int idx;

  MetaKmsCrtcState current_state;
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

static void
meta_kms_crtc_read_state (MetaKmsCrtc       *crtc,
                          MetaKmsImplDevice *impl_device,
                          drmModeCrtc       *drm_crtc)
{
  g_clear_pointer (&crtc->current_state.gamma.red, g_free);
  g_clear_pointer (&crtc->current_state.gamma.green, g_free);
  g_clear_pointer (&crtc->current_state.gamma.blue, g_free);

  crtc->current_state = (MetaKmsCrtcState) {
    .rect = {
      .x = drm_crtc->x,
      .y = drm_crtc->y,
      .width = drm_crtc->width,
      .height = drm_crtc->height,
    },
    .is_drm_mode_valid = drm_crtc->mode_valid,
    .drm_mode = drm_crtc->mode,
    .gamma = {
      .size = drm_crtc->gamma_size,
      .red = g_new0 (unsigned short, drm_crtc->gamma_size),
      .green = g_new0 (unsigned short, drm_crtc->gamma_size),
      .blue = g_new0 (unsigned short, drm_crtc->gamma_size),
    },
  };

  drmModeCrtcGetGamma (meta_kms_impl_device_get_fd (impl_device),
                       crtc->id,
                       drm_crtc->gamma_size,
                       crtc->current_state.gamma.red,
                       crtc->current_state.gamma.green,
                       crtc->current_state.gamma.blue);
}

void
meta_kms_crtc_update_state (MetaKmsCrtc *crtc)
{
  MetaKmsImplDevice *impl_device;
  drmModeCrtc *drm_crtc;

  impl_device = meta_kms_device_get_impl_device (crtc->device);
  drm_crtc = drmModeGetCrtc (meta_kms_impl_device_get_fd (impl_device),
                             crtc->id);
  meta_kms_crtc_read_state (crtc, impl_device, drm_crtc);
  drmModeFreeCrtc (drm_crtc);
}

MetaKmsCrtc *
meta_kms_crtc_new (MetaKmsImplDevice *impl_device,
                   drmModeCrtc       *drm_crtc,
                   int                idx)
{
  MetaKmsCrtc *crtc;

  crtc = g_object_new (META_TYPE_KMS_CRTC, NULL);
  crtc->device = meta_kms_impl_device_get_device (impl_device);
  crtc->id = drm_crtc->crtc_id;
  crtc->idx = idx;

  return crtc;
}

static void
meta_kms_crtc_init (MetaKmsCrtc *crtc)
{
}

static void
meta_kms_crtc_class_init (MetaKmsCrtcClass *klass)
{
}

void
meta_kms_crtc_get_gamma (MetaKmsCrtc     *crtc,
                         gsize           *size,
                         unsigned short **red,
                         unsigned short **green,
                         unsigned short **blue)
{
  unsigned int n_gamma_values = crtc->current_state.gamma.size;
  unsigned int i;

  *size = n_gamma_values;
  *red = g_new0 (unsigned short, n_gamma_values);
  *green = g_new0 (unsigned short, n_gamma_values);
  *blue = g_new0 (unsigned short, n_gamma_values);

  for (i = 0; i < n_gamma_values; i++)
    {
      *red[i] = crtc->current_state.gamma.red[i];
      *green[i] = crtc->current_state.gamma.green[i];
      *blue[i] = crtc->current_state.gamma.blue[i];
    }
}
