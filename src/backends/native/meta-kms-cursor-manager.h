/*
 * Copyright (C) 2021 Red Hat
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
#include <graphene.h>

#include "backends/meta-monitor-transform.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-kms-types.h"
#include "core/util-private.h"

typedef struct _MetaKmsCrtcLayout
{
  MetaKmsCrtc *crtc;
  MetaKmsPlane *cursor_plane;
  graphene_rect_t layout;
  float scale;
} MetaKmsCrtcLayout;

typedef void (* MetaKmsCursorQueryInImpl) (float    *x,
                                           float    *y,
                                           gpointer  user_data);

#define META_TYPE_KMS_CURSOR_MANAGER (meta_kms_cursor_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsCursorManager, meta_kms_cursor_manager,
                      META, KMS_CURSOR_MANAGER, GObject)

MetaKmsCursorManager * meta_kms_cursor_manager_new (MetaKms *kms);

void meta_kms_cursor_manager_set_query_func (MetaKmsCursorManager     *cursor_manager,
                                             MetaKmsCursorQueryInImpl  func,
                                             gpointer                  user_data);

META_EXPORT_TEST
void meta_kms_cursor_manager_position_changed_in_input_impl (MetaKmsCursorManager   *cursor_manager,
                                                             const graphene_point_t *position);

void meta_kms_cursor_manager_update_sprite (MetaKmsCursorManager   *cursor_manager,
                                            MetaKmsCrtc            *crtc,
                                            MetaDrmBuffer          *buffer,
                                            MetaMonitorTransform    transform,
                                            const graphene_point_t *hotspot);

META_EXPORT_TEST
void meta_kms_cursor_manager_update_crtc_layout (MetaKmsCursorManager *cursor_manager,
                                                 GArray               *layouts);
