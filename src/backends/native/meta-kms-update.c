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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-update-private.h"

#include "backends/meta-display-config-shared.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsUpdate
{
  MetaKmsDevice *device;

  gboolean is_sealed;

  gboolean is_latchable;
  MetaKmsCrtc *latch_crtc;

  GList *mode_sets;
  GList *plane_assignments;
  GList *connector_updates;
  GList *crtc_color_updates;

  MetaKmsCustomPageFlip *custom_page_flip;

  GList *page_flip_listeners;
  GList *result_listeners;

  gboolean needs_modeset;

  MetaKmsImplDevice *impl_device;
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

MetaKmsPlaneFeedback *
meta_kms_plane_feedback_new_failed (MetaKmsPlane *plane,
                                    MetaKmsCrtc  *crtc,
                                    const char   *error_message)
{
  GError *error;

  error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, error_message);
  return meta_kms_plane_feedback_new_take_error (plane, crtc, error);
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
  g_atomic_ref_count_init (&feedback->ref_count);

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
  g_atomic_ref_count_init (&feedback->ref_count);

  return feedback;
}

MetaKmsFeedback *
meta_kms_feedback_ref (MetaKmsFeedback *feedback)
{
  g_atomic_ref_count_inc (&feedback->ref_count);
  return feedback;
}

void
meta_kms_feedback_unref (MetaKmsFeedback *feedback)
{
  if (g_atomic_ref_count_dec (&feedback->ref_count))
    {
      g_list_free_full (feedback->failed_planes,
                        (GDestroyNotify) meta_kms_plane_feedback_free);
      g_clear_error (&feedback->error);
      g_free (feedback);
    }
}

MetaKmsFeedbackResult
meta_kms_feedback_get_result (const MetaKmsFeedback *feedback)
{
  return feedback->result;
}

gboolean
meta_kms_feedback_did_pass (const MetaKmsFeedback *feedback)
{
  return feedback->result == META_KMS_FEEDBACK_PASSED;
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

void
meta_kms_feedback_dispatch_result (MetaKmsFeedback *feedback,
                                   MetaKms         *kms,
                                   GList           *result_listeners)
{
  GList *l;

  for (l = result_listeners; l; l = l->next)
    {
      MetaKmsResultListener *listener = l->data;

      meta_kms_result_listener_set_feedback (listener, feedback);
      meta_kms_queue_result_callback (kms, listener);
    }
  g_list_free (result_listeners);
}

static void
meta_kms_fb_damage_free (MetaKmsFbDamage *fb_damage)
{
  g_free (fb_damage->rects);
  g_free (fb_damage);
}

static void
meta_kms_plane_assignment_free (MetaKmsPlaneAssignment *plane_assignment)
{
  g_clear_pointer (&plane_assignment->fb_damage, meta_kms_fb_damage_free);
  g_free (plane_assignment);
}

static void
meta_kms_mode_set_free (MetaKmsModeSet *mode_set)
{
  g_list_free (mode_set->connectors);
  g_free (mode_set);
}

void
meta_kms_page_flip_listener_unref (MetaKmsPageFlipListener *listener)
{
  MetaKmsDevice *device;

  if (!g_atomic_ref_count_dec (&listener->ref_count))
    return;

  device = meta_kms_crtc_get_device (listener->crtc);
  meta_kms_queue_callback (meta_kms_device_get_kms (device),
                           listener->main_context,
                           NULL,
                           g_steal_pointer (&listener->user_data),
                           g_steal_pointer (&listener->destroy_notify));
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

static void
update_latch_crtc (MetaKmsUpdate *update,
                   MetaKmsCrtc   *crtc)
{
  if (update->is_latchable)
    {
      if (update->latch_crtc)
        {
          if (update->latch_crtc != crtc)
            {
              update->is_latchable = FALSE;
              update->latch_crtc = NULL;
            }
        }
      else
        {
          update->latch_crtc = crtc;
        }
    }
}

MetaKmsPlaneAssignment *
meta_kms_update_assign_plane (MetaKmsUpdate          *update,
                              MetaKmsCrtc            *crtc,
                              MetaKmsPlane           *plane,
                              MetaDrmBuffer          *buffer,
                              MetaFixed16Rectangle    src_rect,
                              MtkRectangle            dst_rect,
                              MetaKmsAssignPlaneFlag  flags)
{
  MetaKmsPlaneAssignment *plane_assignment;
  MetaKmsAssignPlaneFlag old_flags;

  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
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

  update_latch_crtc (update, crtc);

  return plane_assignment;
}

MetaKmsPlaneAssignment *
meta_kms_update_unassign_plane (MetaKmsUpdate *update,
                                MetaKmsCrtc   *crtc,
                                MetaKmsPlane  *plane)
{
  MetaKmsPlaneAssignment *plane_assignment;

  g_assert (meta_kms_crtc_get_device (crtc) == update->device);
  g_assert (meta_kms_plane_get_device (plane) == update->device);

  drop_plane_assignment (update, plane, NULL);

  plane_assignment = g_new0 (MetaKmsPlaneAssignment, 1);
  *plane_assignment = (MetaKmsPlaneAssignment) {
    .update = update,
    .crtc = crtc,
    .plane = plane,
    .buffer = NULL,
  };

  update->plane_assignments = g_list_prepend (update->plane_assignments,
                                              plane_assignment);

  update_latch_crtc (update, crtc);

  return plane_assignment;
}

void
meta_kms_update_mode_set (MetaKmsUpdate *update,
                          MetaKmsCrtc   *crtc,
                          GList         *connectors,
                          MetaKmsMode   *mode)
{
  MetaKmsModeSet *mode_set;

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

  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->underscanning.has_update = TRUE;
  connector_update->underscanning.is_active = FALSE;
}

void
meta_kms_update_set_privacy_screen (MetaKmsUpdate    *update,
                                    MetaKmsConnector *connector,
                                    gboolean          enabled)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->privacy_screen.has_update = TRUE;
  connector_update->privacy_screen.is_enabled = enabled;
}

void
meta_kms_update_set_max_bpc (MetaKmsUpdate    *update,
                             MetaKmsConnector *connector,
                             uint64_t          max_bpc)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (meta_kms_connector_get_device (connector) == update->device);

  connector_update = ensure_connector_update (update, connector);
  connector_update->max_bpc.value = max_bpc;
  connector_update->max_bpc.has_update = TRUE;
}

void
meta_kms_update_set_color_space (MetaKmsUpdate        *update,
                                 MetaKmsConnector     *connector,
                                 MetaOutputColorspace  color_space)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (meta_kms_connector_get_device (connector) == update->device);
  g_return_if_fail (meta_kms_connector_is_color_space_supported (connector,
                                                                 color_space));

  connector_update = ensure_connector_update (update, connector);
  connector_update->colorspace.has_update = TRUE;
  connector_update->colorspace.value = color_space;
}

void
meta_kms_update_set_hdr_metadata (MetaKmsUpdate         *update,
                                  MetaKmsConnector      *connector,
                                  MetaOutputHdrMetadata *metadata)
{
  MetaKmsConnectorUpdate *connector_update;

  g_assert (meta_kms_connector_get_device (connector) == update->device);
  g_return_if_fail (meta_kms_connector_is_hdr_metadata_supported (connector));

  connector_update = ensure_connector_update (update, connector);
  connector_update->hdr.has_update = TRUE;
  connector_update->hdr.value = *metadata;

  /* Currently required on AMDGPU but should in general not require mode sets */
  update->needs_modeset = TRUE;
}

static MetaKmsCrtcColorUpdate *
ensure_color_update (MetaKmsUpdate *update,
                     MetaKmsCrtc   *crtc)
{
  GList *l;
  MetaKmsCrtcColorUpdate *color_update;

  for (l = update->crtc_color_updates; l; l = l->next)
    {
      color_update = l->data;

      if (color_update->crtc == crtc)
        return color_update;
    }

  color_update = g_new0 (MetaKmsCrtcColorUpdate, 1);
  color_update->crtc = crtc;

  update->crtc_color_updates = g_list_prepend (update->crtc_color_updates,
                                               color_update);

  return color_update;
}

void
meta_kms_update_set_crtc_gamma (MetaKmsUpdate      *update,
                                MetaKmsCrtc        *crtc,
                                const MetaGammaLut *gamma)
{
  MetaKmsCrtcColorUpdate *color_update;
  MetaGammaLut *gamma_update = NULL;
  const MetaKmsCrtcState *crtc_state = meta_kms_crtc_get_current_state (crtc);

  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  if (gamma)
    gamma_update = meta_gamma_lut_copy_to_size (gamma, crtc_state->gamma.size);

  color_update = ensure_color_update (update, crtc);
  color_update->gamma.state = gamma_update;
  color_update->gamma.has_update = TRUE;

  update_latch_crtc (update, crtc);
}

static void
meta_kms_crtc_color_updates_free (MetaKmsCrtcColorUpdate *color_update)
{
  if (color_update->gamma.has_update)
    g_clear_pointer (&color_update->gamma.state, meta_gamma_lut_free);
  g_free (color_update);
}

void
meta_kms_update_add_page_flip_listener (MetaKmsUpdate                       *update,
                                        MetaKmsCrtc                         *crtc,
                                        const MetaKmsPageFlipListenerVtable *vtable,
                                        MetaKmsPageFlipListenerFlag          flags,
                                        GMainContext                        *main_context,
                                        gpointer                             user_data,
                                        GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipListener *listener;

  g_assert (meta_kms_crtc_get_device (crtc) == update->device);

  if (!main_context)
    main_context = g_main_context_default ();

  listener = g_new0 (MetaKmsPageFlipListener, 1);
  *listener = (MetaKmsPageFlipListener) {
    .crtc = crtc,
    .vtable = vtable,
    .flags = flags,
    .main_context = main_context,
    .user_data = user_data,
    .destroy_notify = destroy_notify,
  };
  g_atomic_ref_count_init (&listener->ref_count);

  update->page_flip_listeners = g_list_prepend (update->page_flip_listeners,
                                                listener);
}

void
meta_kms_update_set_custom_page_flip (MetaKmsUpdate             *update,
                                      MetaKmsCustomPageFlipFunc  func,
                                      gpointer                   user_data)
{
  MetaKmsCustomPageFlip *custom_page_flip;

  custom_page_flip = g_new0 (MetaKmsCustomPageFlip, 1);
  custom_page_flip->func = func;
  custom_page_flip->user_data = user_data;

  update->custom_page_flip = custom_page_flip;
}

void
meta_kms_plane_assignment_set_fb_damage (MetaKmsPlaneAssignment *plane_assignment,
                                         const int              *rectangles,
                                         int                     n_rectangles)
{
  MetaKmsFbDamage *fb_damage;
  struct drm_mode_rect *mode_rects;
  int i;

  mode_rects = g_new0 (struct drm_mode_rect, n_rectangles);
  for (i = 0; i < n_rectangles; ++i)
    {
      mode_rects[i].x1 = rectangles[i * 4];
      mode_rects[i].y1 = rectangles[i * 4 + 1];
      mode_rects[i].x2 = mode_rects[i].x1 + rectangles[i * 4 + 2];
      mode_rects[i].y2 = mode_rects[i].y1 + rectangles[i * 4 + 3];
    }

  fb_damage = g_new0 (MetaKmsFbDamage, 1);
  *fb_damage = (MetaKmsFbDamage) {
    .rects = mode_rects,
    .n_rects = n_rectangles,
  };

  plane_assignment->fb_damage = fb_damage;
}

void
meta_kms_plane_assignment_set_rotation (MetaKmsPlaneAssignment *plane_assignment,
                                        MetaKmsPlaneRotation    rotation)
{
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
meta_kms_update_add_result_listener (MetaKmsUpdate                     *update,
                                     const MetaKmsResultListenerVtable *vtable,
                                     GMainContext                      *main_context,
                                     gpointer                           user_data,
                                     GDestroyNotify                     destroy_notify)
{
  MetaKmsResultListener *listener;

  listener = g_new0 (MetaKmsResultListener, 1);
  *listener = (MetaKmsResultListener) {
    .main_context = main_context,
    .vtable = vtable,
    .user_data = user_data,
    .destroy_notify = destroy_notify,
  };

  update->result_listeners = g_list_append (update->result_listeners,
                                            listener);
}

GList *
meta_kms_update_take_result_listeners (MetaKmsUpdate *update)
{
  return g_steal_pointer (&update->result_listeners);
}

GMainContext *
meta_kms_result_listener_get_main_context (MetaKmsResultListener *listener)
{
  return listener->main_context;
}

void
meta_kms_result_listener_set_feedback (MetaKmsResultListener *listener,
                                       MetaKmsFeedback       *feedback)
{
  g_return_if_fail (!listener->feedback);

  listener->feedback = meta_kms_feedback_ref (feedback);
}

void
meta_kms_result_listener_notify (MetaKmsResultListener *listener)
{
  g_return_if_fail (listener->feedback);

  if (listener->vtable->feedback)
    listener->vtable->feedback (listener->feedback, listener->user_data);
}

void
meta_kms_result_listener_free (MetaKmsResultListener *listener)
{
  if (listener->destroy_notify)
    listener->destroy_notify (listener->user_data);
  g_clear_pointer (&listener->feedback, meta_kms_feedback_unref);
  g_free (listener);
}

static MetaKmsPlaneAssignment *
get_first_plane_assignment (MetaKmsUpdate    *update,
                            MetaKmsCrtc      *crtc,
                            MetaKmsPlaneType  plane_type)
{
  GList *l;

  for (l = meta_kms_update_get_plane_assignments (update); l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (meta_kms_plane_get_plane_type (plane_assignment->plane) !=
          plane_type)
        continue;

      if (plane_assignment->crtc != crtc)
        continue;

      return plane_assignment;
    }

  return NULL;
}

MetaKmsPlaneAssignment *
meta_kms_update_get_primary_plane_assignment (MetaKmsUpdate *update,
                                              MetaKmsCrtc   *crtc)
{
  return get_first_plane_assignment (update, crtc, META_KMS_PLANE_TYPE_PRIMARY);
}

MetaKmsPlaneAssignment *
meta_kms_update_get_cursor_plane_assignment (MetaKmsUpdate *update,
                                             MetaKmsCrtc   *crtc)
{
  return get_first_plane_assignment (update, crtc, META_KMS_PLANE_TYPE_CURSOR);
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
meta_kms_update_get_crtc_color_updates (MetaKmsUpdate *update)
{
  return update->crtc_color_updates;
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

static GList *
find_mode_set_link_for (MetaKmsUpdate *update,
                        MetaKmsCrtc   *crtc)
{
  GList *l;

  for (l = update->mode_sets; l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;

      if (mode_set->crtc == crtc)
        return l;
    }

  return NULL;
}

static void
merge_mode_sets (MetaKmsUpdate *update,
                 MetaKmsUpdate *other_update)
{
  while (other_update->mode_sets)
    {
      GList *l = other_update->mode_sets;
      MetaKmsModeSet *other_mode_set = l->data;
      MetaKmsCrtc *crtc = other_mode_set->crtc;
      GList *el;

      other_update->mode_sets =
        g_list_remove_link (other_update->mode_sets, l);

      el = find_mode_set_link_for (update, crtc);
      if (el)
        {
          meta_kms_mode_set_free (el->data);
          update->mode_sets =
            g_list_insert_before_link (update->mode_sets, el, l);
          update->mode_sets =
            g_list_delete_link (update->mode_sets, el);
        }
      else
        {
          update->mode_sets =
            g_list_insert_before_link (update->mode_sets,
                                       update->mode_sets,
                                       l);
        }
    }
}

static GList *
find_plane_assignment_link_for (MetaKmsUpdate *update,
                                MetaKmsPlane  *plane)
{
  GList *l;

  for (l = update->plane_assignments; l; l = l->next)
    {
      MetaKmsPlaneAssignment *plane_assignment = l->data;

      if (plane_assignment->plane == plane)
        return l;
    }

  return NULL;
}

static void
merge_plane_assignments_from (MetaKmsUpdate *update,
                              MetaKmsUpdate *other_update)
{
  while (other_update->plane_assignments)
    {
      GList *l = other_update->plane_assignments;
      MetaKmsPlaneAssignment *other_plane_assignment = l->data;
      MetaKmsPlane *plane = other_plane_assignment->plane;
      GList *el;

      other_update->plane_assignments =
        g_list_remove_link (other_update->plane_assignments, l);

      el = find_plane_assignment_link_for (update, plane);
      if (el)
        {
          meta_kms_plane_assignment_free (el->data);
          update->plane_assignments =
            g_list_insert_before_link (update->plane_assignments, el, l);
          update->plane_assignments =
            g_list_delete_link (update->plane_assignments, el);
        }
      else
        {
          update->plane_assignments =
            g_list_insert_before_link (update->plane_assignments,
                                       update->plane_assignments,
                                       l);
        }
      other_plane_assignment->update = update;
    }
}

static GList *
find_color_update_link_for (MetaKmsUpdate *update,
                            MetaKmsCrtc   *crtc)
{
  GList *l;

  for (l = update->crtc_color_updates; l; l = l->next)
    {
      MetaKmsCrtcColorUpdate *color_update = l->data;

      if (color_update->crtc == crtc)
        return l;
    }

  return NULL;
}

static void
merge_crtc_color_updates_from (MetaKmsUpdate *update,
                               MetaKmsUpdate *other_update)
{
  while (other_update->crtc_color_updates)
    {
      GList *l = other_update->crtc_color_updates;
      MetaKmsCrtcColorUpdate *other_crtc_color_update = l->data;
      MetaKmsCrtc *crtc = other_crtc_color_update->crtc;
      GList *el;

      other_update->crtc_color_updates =
        g_list_remove_link (other_update->crtc_color_updates, l);

      el = find_color_update_link_for (update, crtc);
      if (el)
        {
          meta_kms_crtc_color_updates_free (el->data);
          update->crtc_color_updates =
            g_list_insert_before_link (update->crtc_color_updates, el, l);
          update->crtc_color_updates =
            g_list_delete_link (update->crtc_color_updates, el);
        }
      else
        {
          update->crtc_color_updates =
            g_list_insert_before_link (update->crtc_color_updates,
                                       update->crtc_color_updates,
                                       l);
        }
    }
}

static GList *
find_connector_update_link_for (MetaKmsUpdate    *update,
                                MetaKmsConnector *connector)
{
  GList *l;

  for (l = update->connector_updates; l; l = l->next)
    {
      MetaKmsConnectorUpdate *connector_update = l->data;

      if (connector_update->connector == connector)
        return l;
    }

  return NULL;
}

static void
merge_connector_updates_from (MetaKmsUpdate *update,
                              MetaKmsUpdate *other_update)
{
  while (other_update->connector_updates)
    {
      GList *l = other_update->connector_updates;
      MetaKmsConnectorUpdate *other_connector_update = l->data;
      MetaKmsConnector *connector = other_connector_update->connector;
      GList *el;

      other_update->connector_updates =
        g_list_remove_link (other_update->connector_updates, l);
      el = find_connector_update_link_for (update, connector);
      if (el)
        {
          MetaKmsConnectorUpdate *connector_update = el->data;

          if (other_connector_update->underscanning.has_update)
            {
              connector_update->underscanning =
                other_connector_update->underscanning;
            }

          if (other_connector_update->privacy_screen.has_update)
            {
              connector_update->privacy_screen =
                other_connector_update->privacy_screen;
            }

          if (other_connector_update->max_bpc.has_update)
            {
              connector_update->max_bpc =
                other_connector_update->max_bpc;
            }

          if (other_connector_update->colorspace.has_update)
            {
              connector_update->colorspace =
                other_connector_update->colorspace;
            }

          if (other_connector_update->hdr.has_update)
            {
              connector_update->hdr = other_connector_update->hdr;
            }
        }
      else
        {
          update->connector_updates =
            g_list_insert_before_link (update->connector_updates,
                                       update->connector_updates,
                                       l);
        }
    }
}

static void
merge_custom_page_flip_from (MetaKmsUpdate *update,
                             MetaKmsUpdate *other_update)
{
  g_warn_if_fail ((!update->custom_page_flip &&
                   !other_update->custom_page_flip) ||
                  ((!!update->custom_page_flip) ^
                   (!!other_update->custom_page_flip)));

  g_clear_pointer (&update->custom_page_flip, meta_kms_custom_page_flip_free);
  update->custom_page_flip = g_steal_pointer (&other_update->custom_page_flip);
}

static void
merge_page_flip_listeners_from (MetaKmsUpdate *update,
                                MetaKmsUpdate *other_update)
{
  update->page_flip_listeners =
    g_list_concat (update->page_flip_listeners,
                   g_steal_pointer (&other_update->page_flip_listeners));
}

static void
merge_result_listeners_from (MetaKmsUpdate *update,
                             MetaKmsUpdate *other_update)
{
  update->result_listeners =
    g_list_concat (update->result_listeners,
                   g_steal_pointer (&other_update->result_listeners));
}

void
meta_kms_update_merge_from (MetaKmsUpdate *update,
                            MetaKmsUpdate *other_update)
{
  g_return_if_fail (update->device == other_update->device);

  merge_mode_sets (update, other_update);
  merge_plane_assignments_from (update, other_update);
  merge_crtc_color_updates_from (update, other_update);
  merge_connector_updates_from (update, other_update);
  merge_custom_page_flip_from (update, other_update);
  merge_page_flip_listeners_from (update, other_update);
  merge_result_listeners_from (update, other_update);
}

gboolean
meta_kms_update_get_needs_modeset (MetaKmsUpdate *update)
{
  return update->needs_modeset || update->mode_sets;
}

MetaKmsUpdate *
meta_kms_update_new (MetaKmsDevice *device)
{
  MetaKmsUpdate *update;

  update = g_new0 (MetaKmsUpdate, 1);
  update->device = device;
  update->is_latchable = TRUE;

  return update;
}

void
meta_kms_update_free (MetaKmsUpdate *update)
{
  if (update->impl_device)
    meta_kms_impl_device_unhold_fd (update->impl_device);

  g_list_free_full (update->result_listeners,
                    (GDestroyNotify) meta_kms_result_listener_free);
  g_list_free_full (update->plane_assignments,
                    (GDestroyNotify) meta_kms_plane_assignment_free);
  g_list_free_full (update->mode_sets,
                    (GDestroyNotify) meta_kms_mode_set_free);
  g_list_free_full (update->page_flip_listeners,
                    (GDestroyNotify) meta_kms_page_flip_listener_unref);
  g_list_free_full (update->connector_updates, g_free);
  g_list_free_full (update->crtc_color_updates,
                    (GDestroyNotify) meta_kms_crtc_color_updates_free);
  g_clear_pointer (&update->custom_page_flip, meta_kms_custom_page_flip_free);

  g_free (update);
}

void
meta_kms_update_realize (MetaKmsUpdate     *update,
                         MetaKmsImplDevice *impl_device)
{
  update->impl_device = impl_device;
  meta_kms_impl_device_hold_fd (impl_device);
}

void
meta_kms_update_set_flushing (MetaKmsUpdate *update,
                              MetaKmsCrtc   *crtc)
{
  update_latch_crtc (update, crtc);
}

MetaKmsCrtc *
meta_kms_update_get_latch_crtc (MetaKmsUpdate *update)
{
  return update->latch_crtc;
}

gboolean
meta_kms_update_is_empty (MetaKmsUpdate *update)
{
  return (!update->mode_sets &&
          !update->plane_assignments &&
          !update->connector_updates &&
          !update->crtc_color_updates);
}
