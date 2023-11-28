/*
 * Copyright (C) 2017 Red Hat
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

#pragma once

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-crtc-mode.h"
#include "backends/meta-monitor-transform.h"
#include "core/util-private.h"
#include "meta/boxes.h"

typedef struct _MetaCrtcConfig
{
  graphene_rect_t layout;
  MetaMonitorTransform transform;
  MetaCrtcMode *mode;
} MetaCrtcConfig;

#define META_TYPE_CRTC (meta_crtc_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaCrtc, meta_crtc, META, CRTC, GObject)

struct _MetaCrtcClass
{
  GObjectClass parent_class;

  size_t (* get_gamma_lut_size) (MetaCrtc *crtc);

  MetaGammaLut * (* get_gamma_lut) (MetaCrtc *crtc);

  void (* set_gamma_lut) (MetaCrtc           *crtc,
                          const MetaGammaLut *lut);

  gboolean (* assign_extra) (MetaCrtc            *crtc,
                             MetaCrtcAssignment  *crtc_assignment,
                             GPtrArray           *crtc_assignments,
                             GError             **error);

  void (* set_config) (MetaCrtc             *crtc,
                       const MetaCrtcConfig *config,
                       gpointer              backend_private);
};

META_EXPORT_TEST
uint64_t meta_crtc_get_id (MetaCrtc *crtc);

META_EXPORT_TEST
MetaBackend * meta_crtc_get_backend (MetaCrtc *crtc);

META_EXPORT_TEST
MetaGpu * meta_crtc_get_gpu (MetaCrtc *crtc);

META_EXPORT_TEST
const GList * meta_crtc_get_outputs (MetaCrtc *crtc);

void meta_crtc_assign_output (MetaCrtc   *crtc,
                              MetaOutput *output);

META_EXPORT_TEST
void meta_crtc_unassign_output (MetaCrtc   *crtc,
                                MetaOutput *output);

MetaMonitorTransform meta_crtc_get_all_transforms (MetaCrtc *crtc);

META_EXPORT_TEST
void meta_crtc_set_config (MetaCrtc       *crtc,
                           MetaCrtcConfig *config,
                           gpointer        backend_private);

META_EXPORT_TEST
void meta_crtc_unset_config (MetaCrtc *crtc);

META_EXPORT_TEST
const MetaCrtcConfig * meta_crtc_get_config (MetaCrtc *crtc);

gboolean meta_crtc_assign_extra (MetaCrtc            *crtc,
                                 MetaCrtcAssignment  *crtc_assignment,
                                 GPtrArray           *crtc_assignments,
                                 GError             **error);

size_t meta_crtc_get_gamma_lut_size (MetaCrtc *crtc);

MetaGammaLut * meta_crtc_get_gamma_lut (MetaCrtc *crtc);

void meta_crtc_set_gamma_lut (MetaCrtc           *crtc,
                              const MetaGammaLut *lut);

META_EXPORT_TEST
void meta_gamma_lut_free (MetaGammaLut *lut);

META_EXPORT_TEST
MetaGammaLut * meta_gamma_lut_new (int             size,
                                   const uint16_t *red,
                                   const uint16_t *green,
                                   const uint16_t *blue);

MetaGammaLut * meta_gamma_lut_new_sized (int size);

META_EXPORT_TEST
MetaGammaLut * meta_gamma_lut_copy (const MetaGammaLut *gamma);

MetaGammaLut * meta_gamma_lut_copy_to_size (const MetaGammaLut *gamma,
                                            int                 target_size);

META_EXPORT_TEST
gboolean meta_gamma_lut_equal (const MetaGammaLut *gamma,
                               const MetaGammaLut *other_gamma);

META_EXPORT_TEST
MetaCrtcConfig * meta_crtc_config_new (graphene_rect_t      *layout,
                                       MetaCrtcMode         *mode,
                                       MetaMonitorTransform  transform);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaGammaLut, meta_gamma_lut_free)
