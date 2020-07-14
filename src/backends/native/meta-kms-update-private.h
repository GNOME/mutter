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

#ifndef META_KMS_UPDATE_PRIVATE_H
#define META_KMS_UPDATE_PRIVATE_H

#include <glib.h>
#include <stdint.h>

#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsFeedback
{
  MetaKmsFeedbackResult result;

  GList *failed_planes;
  GError *error;
} MetaKmsFeedback;

typedef struct _MetaKmsPlaneAssignment
{
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsPlane *plane;
  uint32_t fb_id;
  MetaFixed16Rectangle src_rect;
  MetaFixed16Rectangle dst_rect;
  MetaKmsAssignPlaneFlag flags;

  uint64_t rotation;

  struct {
    gboolean is_valid;
    int x;
    int y;
  } cursor_hotspot;
} MetaKmsPlaneAssignment;

typedef struct _MetaKmsModeSet
{
  MetaKmsCrtc *crtc;
  GList *connectors;
  MetaKmsMode *mode;
} MetaKmsModeSet;

typedef struct _MetaKmsConnectorUpdate
{
  MetaKmsConnector *connector;

  struct {
    gboolean has_update;
    gboolean is_active;
    uint64_t hborder;
    uint64_t vborder;
  } underscanning;

  struct {
    gboolean has_update;
    uint64_t state;
  } dpms;
} MetaKmsConnectorUpdate;

typedef struct _MetaKmsCrtcGamma
{
  MetaKmsCrtc *crtc;
  int size;
  uint16_t *red;
  uint16_t *green;
  uint16_t *blue;
} MetaKmsCrtcGamma;

typedef struct _MetaKmsPageFlip
{
  MetaKmsCrtc *crtc;
  const MetaKmsPageFlipFeedback *feedback;
  MetaKmsPageFlipFlag flags;
  gpointer user_data;
  MetaKmsCustomPageFlipFunc custom_page_flip_func;
  gpointer custom_page_flip_user_data;
} MetaKmsPageFlip;

void meta_kms_plane_feedback_free (MetaKmsPlaneFeedback *plane_feedback);

MetaKmsPlaneFeedback * meta_kms_plane_feedback_new_take_error (MetaKmsPlane *plane,
                                                               MetaKmsCrtc  *crtc,
                                                               GError       *error);

MetaKmsFeedback * meta_kms_feedback_new_passed (void);

MetaKmsFeedback * meta_kms_feedback_new_failed (GList  *failed_planes,
                                                GError *error);

void meta_kms_update_seal (MetaKmsUpdate *update);

gboolean meta_kms_update_is_sealed (MetaKmsUpdate *update);

void meta_kms_plane_assignment_set_rotation (MetaKmsPlaneAssignment *plane_assignment,
                                             uint64_t                rotation);

MetaKmsPlaneAssignment * meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                                                       MetaKmsCrtc   *crtc);

GList * meta_kms_update_get_plane_assignments (MetaKmsUpdate *update);

GList * meta_kms_update_get_mode_sets (MetaKmsUpdate *update);

GList * meta_kms_update_get_page_flips (MetaKmsUpdate *update);

GList * meta_kms_update_get_connector_updates (MetaKmsUpdate *update);

GList * meta_kms_update_get_crtc_gammas (MetaKmsUpdate *update);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsPlaneFeedback,
                               meta_kms_plane_feedback_free)

#endif /* META_KMS_UPDATE_PRIVATE_H */
