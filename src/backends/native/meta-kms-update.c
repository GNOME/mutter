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

  gboolean is_locked;
  uint64_t sequence_number;

  gboolean power_save;

  GList *mode_sets;
  GList *plane_assignments;
  GList *connector_updates;
  GList *crtc_gammas;

  MetaKmsCustomPageFlip *custom_page_flip;

  GList *page_flip_listeners;
  GList *result_listeners;
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
meta_kms_feedback_get_result (const MetaKmsFeedback *feedback)
{
  return feedback->result;
}

GList *
meta_kms_feedback_get_failed_planes (const MetaKmsFeedback *feedback)
{
  return feedback->failed_planes;
}

const GError *
meta_kms_feedback_get_error (const MetaKmsFeedback *feedback)
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

static void
meta_kms_page_flip_listener_free (MetaKmsPageFlipListener *listener)
{
  g_clear_pointer (&listener->user_data, listener->destroy_notify);
  g_free (listener);
}

static gboolean
drop_plane_assignment (MetaKmsUpdate          *update,
                       MetaKmsPlane           *plane,
                       MetaKmsAssignPlaneFlag *out_flags)
{
  GList *l;

  for (l = update->plane_assignments; l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (plane_assignment->plane == plane)
        {
          update->plane_assignments =
            g_list_delete_link (update->plane_assignments, l);
          if (out_flags)
            *out_flags = plane_assignment->flags;
          meta_kms_plane_assignment_free (plane_assignment);
          return TRUE;
        }
    }

  return FALSE;
}

void
meta_kms_update_drop_plane_assignment (MetaKmsUpdate *update,
                                       MetaKmsPlane  *plane)
{
  drop_plane_assignment (update, plane, NULL);
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
  MetaKmsAssignPlaneFlag old_flags;

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (!update->power_save);
  g_assert (meta_kms_plane_get_device (plane) == update->device);
  g_assert (meta_kms_plane_get_plane_type (plane) !=
            META_KMS_PLANE_TYPE_PRIMARY ||
            !(flags & META_KMS_ASSIGN_PLANE_FLAG_ALLOW_FAIL));

  if (drop_plane_assignment (update, plane, &old_flags))
    {
      if (!(old_flags & META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED))
        flags &= ~META_KMS_ASSIGN_PLANE_FLAG_FB_UNCHANGED;
    }

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

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (meta_kms_plane_get_device (plane) == update->device);
  g_assert (!update->power_save);

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

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (!update->power_save);

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

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_connector_get_device (connector) == update->device);
  g_assert (!update->power_save);

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

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_connector_get_device (connector) == update->device);
  g_assert (!update->power_save);

  connector_update = ensure_connector_update (update, connector);
  connector_update->underscanning.has_update = TRUE;
  connector_update->underscanning.is_active = FALSE;
}

void
meta_kms_update_set_power_save (MetaKmsUpdate *update)
{
  g_assert (!meta_kms_update_is_locked (update));
  g_assert (!update->mode_sets);
  g_assert (!update->plane_assignments);
  g_assert (!update->connector_updates);
  g_assert (!update->crtc_gammas);

  update->power_save = TRUE;
}

void
meta_kms_crtc_gamma_free (MetaKmsCrtcGamma *gamma)
{
  g_free (gamma->red);
  g_free (gamma->green);
  g_free (gamma->blue);
  g_free (gamma);
}

MetaKmsCrtcGamma *
meta_kms_crtc_gamma_new (MetaKmsCrtc    *crtc,
                         int             size,
                         const uint16_t *red,
                         const uint16_t *green,
                         const uint16_t *blue)
{
  MetaKmsCrtcGamma *gamma;

  gamma = g_new0 (MetaKmsCrtcGamma, 1);
  *gamma = (MetaKmsCrtcGamma) {
    .crtc = crtc,
    .size = size,
    .red = g_memdup2 (red, size * sizeof (*red)),
    .green = g_memdup2 (green, size * sizeof (*green)),
    .blue = g_memdup2 (blue, size * sizeof (*blue)),
  };

  return gamma;
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

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (!update->power_save);

  gamma = meta_kms_crtc_gamma_new (crtc, size, red, green, blue);

  update->crtc_gammas = g_list_prepend (update->crtc_gammas, gamma);
}

void
meta_kms_update_add_page_flip_listener (MetaKmsUpdate                       *update,
                                        MetaKmsCrtc                         *crtc,
                                        const MetaKmsPageFlipListenerVtable *vtable,
                                        MetaKmsPageFlipListenerFlag          flags,
                                        gpointer                             user_data,
                                        GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipListener *listener;

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  listener = g_new0 (MetaKmsPageFlipListener, 1);
  *listener = (MetaKmsPageFlipListener) {
    .crtc = crtc,
    .vtable = vtable,
    .flags = flags,
    .user_data = user_data,
    .destroy_notify = destroy_notify,
  };

  update->page_flip_listeners = g_list_prepend (update->page_flip_listeners,
                                                listener);
}

void
meta_kms_update_set_custom_page_flip (MetaKmsUpdate             *update,
                                      MetaKmsCustomPageFlipFunc  func,
                                      gpointer                   user_data)
{
  MetaKmsCustomPageFlip *custom_page_flip;

  g_assert (!meta_kms_update_is_locked (update));
  g_assert (!update->power_save);

  custom_page_flip = g_new0 (MetaKmsCustomPageFlip, 1);
  custom_page_flip->func = func;
  custom_page_flip->user_data = user_data;

  update->custom_page_flip = custom_page_flip;
}

void
meta_kms_plane_assignment_set_rotation (MetaKmsPlaneAssignment *plane_assignment,
                                        uint64_t                rotation)
{
  g_assert (!meta_kms_update_is_locked (plane_assignment->update));
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

void
meta_kms_update_add_result_listener (MetaKmsUpdate             *update,
                                     MetaKmsResultListenerFunc  func,
                                     gpointer                   user_data)
{
  MetaKmsResultListener *listener;

  listener = g_new0 (MetaKmsResultListener, 1);
  *listener = (MetaKmsResultListener) {
    .func = func,
    .user_data = user_data,
  };

  update->result_listeners = g_list_append (update->result_listeners,
                                            listener);
}

GList *
meta_kms_update_take_result_listeners (MetaKmsUpdate *update)
{
  return g_steal_pointer (&update->result_listeners);
}

void
meta_kms_result_listener_notify (MetaKmsResultListener *listener,
                                 const MetaKmsFeedback *feedback)
{
  listener->func (feedback, listener->user_data);
}

void
meta_kms_result_listener_free (MetaKmsResultListener *listener)
{
  g_free (listener);
}

MetaKmsPlaneAssignment *
meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                              MetaKmsCrtc   *crtc)
{
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (meta_kms_plane_get_plane_type (plane_assignment->plane) !=
          META_KMS_PLANE_TYPE_PRIMARY)
        continue;

      if (plane_assignment->crtc != crtc)
        continue;

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
meta_kms_update_get_page_flip_listeners (MetaKmsUpdate *update)
{
  return update->page_flip_listeners;
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

gboolean
meta_kms_update_is_power_save (MetaKmsUpdate *update)
{
  return update->power_save;
}

void
meta_kms_update_lock (MetaKmsUpdate *update)
{
  update->is_locked = TRUE;
}

void
meta_kms_update_unlock (MetaKmsUpdate *update)
{
  update->is_locked = FALSE;
}

gboolean
meta_kms_update_is_locked (MetaKmsUpdate *update)
{
  return update->is_locked;
}

MetaKmsDevice *
meta_kms_update_get_device (MetaKmsUpdate *update)
{
  return update->device;
}

MetaKmsCustomPageFlip *
meta_kms_update_take_custom_page_flip_func (MetaKmsUpdate *update)
{
  return g_steal_pointer (&update->custom_page_flip);
}

void
meta_kms_custom_page_flip_free (MetaKmsCustomPageFlip *custom_page_flip)
{
  g_free (custom_page_flip);
}

uint64_t
meta_kms_update_get_sequence_number (MetaKmsUpdate *update)
{
  return update->sequence_number;
}

MetaKmsUpdate *
meta_kms_update_new (MetaKmsDevice *device)
{
  MetaKmsUpdate *update;
  static uint64_t sequence_number = 0;

  update = g_new0 (MetaKmsUpdate, 1);
  update->device = device;
  update->sequence_number = sequence_number++;

  return update;
}

void
meta_kms_update_free (MetaKmsUpdate *update)
{
  g_list_free_full (update->result_listeners,
                    (GDestroyNotify) meta_kms_result_listener_free);
  g_list_free_full (update->plane_assignments,
                    (GDestroyNotify) meta_kms_plane_assignment_free);
  g_list_free_full (update->mode_sets,
                    (GDestroyNotify) meta_kms_mode_set_free);
  g_list_free_full (update->page_flip_listeners,
                    (GDestroyNotify) meta_kms_page_flip_listener_free);
  g_list_free_full (update->connector_updates, g_free);
  g_list_free_full (update->crtc_gammas, (GDestroyNotify) meta_kms_crtc_gamma_free);
  g_clear_pointer (&update->custom_page_flip, meta_kms_custom_page_flip_free);

  g_free (update);
}
