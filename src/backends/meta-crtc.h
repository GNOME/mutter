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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_CRTC_H
#define META_CRTC_H

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
};

META_EXPORT_TEST
uint64_t meta_crtc_get_id (MetaCrtc *crtc);

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
void meta_crtc_set_config (MetaCrtc             *crtc,
                           graphene_rect_t      *layout,
                           MetaCrtcMode         *mode,
                           MetaMonitorTransform  transform);

META_EXPORT_TEST
void meta_crtc_unset_config (MetaCrtc *crtc);

META_EXPORT_TEST
const MetaCrtcConfig * meta_crtc_get_config (MetaCrtc *crtc);

#endif /* META_CRTC_H */
