/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-update-private.h"

#include "backends/meta-display-config-shared.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane.h"

struct _MetaKmsUpdate
{
  MetaKmsDevice *device;

  gboolean is_sealed;

  MetaPowerSave power_save;
  GList *mode_sets;
  GList *plane_assignments;
  GList *page_flips;
  GList *connector_updates;
  GList *crtc_gammas;

  MetaKmsCustomPageFlipFunc custom_page_flip_func;
  gpointer custom_page_flip_user_data;
};

void
meta_kms_plane_feedback_free (MetaKmsPlaneFeedback *plane_feedback)
{
  g_error_free (plane_feedback->error);
  g_free (plane_feedback);
}

MetaKmsPlaneFeedback *
meta_kms_plane_feedback_new_take_error (MetaKmsPlane *plane,
                                        MetaKmsCrtc  *crtc,
                                        GError       *error)
{
  MetaKmsPlaneFeedback *plane_feedback;

  plane_feedback = g_new0 (MetaKmsPlaneFeedback, 1);
  *plane_feedback = (MetaKmsPlaneFeedback) {
    .plane = plane,
    .crtc = crtc,
    .error = error,
  };

  return plane_feedback;
}

MetaKmsFeedback *
meta_kms_feedback_new_passed (GList *failed_planes)
{
  MetaKmsFeedback *feedback;

  feedback = g_new0 (MetaKmsFeedback, 1);
  *feedback = (MetaKmsFeedback) {
    .result = META_KMS_FEEDBACK_PASSED,
    .failed_planes = failed_planes,
  };

  return feedback;
}

MetaKmsFeedback *
meta_kms_feedback_new_failed (GList  *failed_planes,
                              GError *error)
{
  MetaKmsFeedback *feedback;

  feedback = g_new0 (MetaKmsFeedback, 1);
  *feedback = (MetaKmsFeedback) {
    .result = META_KMS_FEEDBACK_FAILED,
    .error = error,
    .failed_planes = failed_planes,
  };

  return feedback;
}

void
meta_kms_feedback_free (MetaKmsFeedback *feedback)
{
  g_list_free_full (feedback->failed_planes,
                    (GDestroyNotify) meta_kms_plane_feedback_free);
  g_clear_error (&feedback->error);
  g_free (feedback);
}

MetaKmsFeedbackResult
meta_kms_feedback_get_result (MetaKmsFeedback *feedback)
{
  return feedback->result;
}

GList *
meta_kms_feedback_get_failed_planes (MetaKmsFeedback *feedback)
{
  return feedback->failed_planes;
}

const GError *
meta_kms_feedback_get_error (MetaKmsFeedback *feedback)
{
  return feedback->error;
}

static void
meta_kms_plane_assignment_free (MetaKmsPlaneAssignment *plane_assignment)
{
  g_free (plane_assignment);
}

static void
meta_kms_mode_set_free (MetaKmsModeSet *mode_set)
{
  g_list_free (mode_set->connectors);
  g_free (mode_set);
}

MetaKmsPlaneAssignment *
meta_kms_update_assign_plane (MetaKmsUpdate          *update,
                              MetaKmsCrtc            *crtc,
                              MetaKmsPlane           *plane,
                              MetaDrmBuffer          *buffer,
                              MetaFixed16Rectangle    src_rect,
                              MetaRectangle           dst_rect,
                              MetaKmsAssignPlaneFlag  flags)
{
  MetaKmsPlaneAssignment *plane_assignment;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (meta_kms_plane_get_device (plane) == update->device);
  g_assert (meta_kms_plane_get_plane_type (plane) !=
            META_KMS_PLANE_TYPE_PRIMARY ||
            !(flags & META_KMS_ASSIGN_PLANE_FLAG_ALLOW_FAIL));

  plane_assignment = g_new0 (MetaKmsPlaneAssignment, 1);
  *plane_assignment = (MetaKmsPlaneAssignment) {
    .update = update,
    .crtc = crtc,
    .plane = plane,
    .buffer = buffer,
    .src_rect = src_rect,
    .dst_rect = dst_rect,
    .flags = flags,
  };

  update->plane_assignments = g_list_prepend (update->plane_assignments,
                                              plane_assignment);

  return plane_assignment;
}

MetaKmsPlaneAssignment *
meta_kms_update_unassign_plane (MetaKmsUpdate *update,
                                MetaKmsCrtc   *crtc,
                                MetaKmsPlane  *plane)
{
  MetaKmsPlaneAssignment *plane_assignment;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (meta_kms_plane_get_device (plane) == update->device);

  plane_assignment = g_new0 (MetaKmsPlaneAssignment, 1);
  *plane_assignment = (MetaKmsPlaneAssignment) {
    .update = update,
    .crtc = crtc,
    .plane = plane,
    .buffer = NULL,
  };

  update->plane_assignments = g_list_prepend (update->plane_assignments,
                                              plane_assignment);

  return plane_assignment;
}

void
meta_kms_update_mode_set (MetaKmsUpdate *update,
                          MetaKmsCrtc   *crtc,
                          GList         *connectors,
                          MetaKmsMode   *mode)
{
  MetaKmsModeSet *mode_set;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  mode_set = g_new0 (MetaKmsModeSet, 1);
  *mode_set = (MetaKmsModeSet) {
    .crtc = crtc,
    .connectors = connectors,
    .mode = mode,
  };

  update->mode_sets = g_list_prepend (update->mode_sets, mode_set);
}

static MetaKmsConnectorUpdate *
ensure_connector_update (MetaKmsUpdate    *update,
                         MetaKmsConnector *connector)
{
  GList *l;
  MetaKmsConnectorUpdate *connector_update;

  for (l = update->connector_updates; l; l = l->next)
    {
      connector_update = l->data;

      if (connector_update->connector == connector)
        return connector_update;
    }

  connector_update = g_new0 (MetaKmsConnectorUpdate, 1);
  connector_update->connector = connector;

  update->connector_updates = g_list_prepend (update->connector_updates,
                                              connector_update);

  return connector_update;
}

void
meta_kms_update_set_underscanning (MetaKmsUpdate    *update,
                                   MetaKmsConnector *connector,
                                   uint64_t          hborder,
                                   uint64_t          vborder)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->underscanning.has_update = TRUE;
  connector_update->underscanning.is_active = TRUE;
  connector_update->underscanning.hborder = hborder;
  connector_update->underscanning.vborder = vborder;
}

void
meta_kms_update_unset_underscanning (MetaKmsUpdate    *update,
                                     MetaKmsConnector *connector)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->underscanning.has_update = TRUE;
  connector_update->underscanning.is_active = FALSE;
}

void
meta_kms_update_set_dpms_state (MetaKmsUpdate    *update,
                                MetaKmsConnector *connector,
                                uint64_t          state)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->dpms.has_update = TRUE;
  connector_update->dpms.state = state;
}

static void
meta_kms_crtc_gamma_free (MetaKmsCrtcGamma *gamma)
{
  g_free (gamma->red);
  g_free (gamma->green);
  g_free (gamma->blue);
  g_free (gamma);
}

void
meta_kms_update_set_crtc_gamma (MetaKmsUpdate  *update,
                                MetaKmsCrtc    *crtc,
                                int             size,
                                const uint16_t *red,
                                const uint16_t *green,
                                const uint16_t *blue)
{
  MetaKmsCrtcGamma *gamma;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  gamma = g_new0 (MetaKmsCrtcGamma, 1);
  *gamma = (MetaKmsCrtcGamma) {
    .crtc = crtc,
    .size = size,
    .red = g_memdup (red, size * sizeof *red),
    .green = g_memdup (green, size * sizeof *green),
    .blue = g_memdup (blue, size * sizeof *blue),
  };

  update->crtc_gammas = g_list_prepend (update->crtc_gammas, gamma);
}

void
meta_kms_update_page_flip (MetaKmsUpdate                 *update,
                           MetaKmsCrtc                   *crtc,
                           const MetaKmsPageFlipFeedback *feedback,
                           gpointer                       user_data)
{
  MetaKmsPageFlip *page_flip;

  g_assert (!meta_kms_update_is_sealed (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  page_flip = g_new0 (MetaKmsPageFlip, 1);
  *page_flip = (MetaKmsPageFlip) {
    .crtc = crtc,
    .feedback = feedback,
    .user_data = user_data,
  };

  update->page_flips = g_list_prepend (update->page_flips, page_flip);
}

void
meta_kms_update_set_custom_page_flip (MetaKmsUpdate             *update,
                                      MetaKmsCustomPageFlipFunc  func,
                                      gpointer                   user_data)
{
  g_assert (!meta_kms_update_is_sealed (update));

  update->custom_page_flip_func = func;
  update->custom_page_flip_user_data = user_data;
}

void
meta_kms_plane_assignment_set_rotation (MetaKmsPlaneAssignment *plane_assignment,
                                        uint64_t                rotation)
{
  g_assert (!meta_kms_update_is_sealed (plane_assignment->update));
  g_warn_if_fail (rotation);

  plane_assignment->rotation = rotation;
}

void
meta_kms_plane_assignment_set_cursor_hotspot (MetaKmsPlaneAssignment *plane_assignment,
                                              int                     x,
                                              int                     y)
{
  plane_assignment->cursor_hotspot.is_valid = TRUE;
  plane_assignment->cursor_hotspot.x = x;
  plane_assignment->cursor_hotspot.y = y;
}

MetaKmsPlaneAssignment *
meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                              MetaKmsCrtc   *crtc)
{
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (plane_assignment->crtc == crtc)
        return plane_assignment;
    }

  return NULL;
}

GList *
meta_kms_update_get_plane_assignments (MetaKmsUpdate *update)
{
  return update->plane_assignments;
}

GList *
meta_kms_update_get_mode_sets (MetaKmsUpdate *update)
{
  return update->mode_sets;
}

GList *
meta_kms_update_get_page_flips (MetaKmsUpdate *update)
{
  return update->page_flips;
}

GList *
meta_kms_update_get_connector_updates (MetaKmsUpdate *update)
{
  return update->connector_updates;
}

GList *
meta_kms_update_get_crtc_gammas (MetaKmsUpdate *update)
{
  return update->crtc_gammas;
}

void
meta_kms_update_seal (MetaKmsUpdate *update)
{
  update->is_sealed = TRUE;
}

gboolean
meta_kms_update_is_sealed (MetaKmsUpdate *update)
{
  return update->is_sealed;
}

MetaKmsDevice *
meta_kms_update_get_device (MetaKmsUpdate *update)
{
  return update->device;
}

void
meta_kms_update_get_custom_page_flip_func (MetaKmsUpdate             *update,
                                           MetaKmsCustomPageFlipFunc *custom_page_flip_func,
                                           gpointer                  *custom_page_flip_user_data)
{
  *custom_page_flip_func = update->custom_page_flip_func;
  *custom_page_flip_user_data = update->custom_page_flip_user_data;
}

MetaKmsUpdate *
meta_kms_update_new (MetaKmsDevice *device)
{
  MetaKmsUpdate *update;

  update = g_new0 (MetaKmsUpdate, 1);
  update->device = device;

  return update;
}

void
meta_kms_update_free (MetaKmsUpdate *update)
{
  g_list_free_full (update->plane_assignments,
                    (GDestroyNotify) meta_kms_plane_assignment_free);
  g_list_free_full (update->mode_sets,
                    (GDestroyNotify) meta_kms_mode_set_free);
  g_list_free_full (update->page_flips, g_free);
  g_list_free_full (update->connector_updates, g_free);
  g_list_free_full (update->crtc_gammas, (GDestroyNotify) meta_kms_crtc_gamma_free);

  g_free (update);
}
