/*
 * presentation-time protocol
 *
 * Copyright (C) 2020 Ivan Molodetskikh <yalterz@gmail.com>
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
 *
 */

#include "config.h"

#include <glib.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-cursor-surface.h"
#include "wayland/meta-wayland-presentation-time-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-outputs.h"
#include "wayland/meta-wayland-versions.h"

#include "presentation-time-server-protocol.h"

static void
wp_presentation_feedback_destructor (struct wl_resource *resource)
{
  MetaWaylandPresentationFeedback *feedback =
    wl_resource_get_user_data (resource);

  wl_list_remove (&feedback->link);
  g_clear_object (&feedback->surface);
  g_free (feedback);
}

static void
wp_presentation_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
wp_presentation_feedback (struct wl_client   *client,
                          struct wl_resource *resource,
                          struct wl_resource *surface_resource,
                          uint32_t            callback_id)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandSurfaceState *pending;
  MetaWaylandPresentationFeedback *feedback;

  feedback = g_new0 (MetaWaylandPresentationFeedback, 1);
  wl_list_init (&feedback->link);
  feedback->resource = wl_resource_create (client,
                                           &wp_presentation_feedback_interface,
                                           wl_resource_get_version (resource),
                                           callback_id);
  wl_resource_set_implementation (feedback->resource,
                                  NULL,
                                  feedback,
                                  wp_presentation_feedback_destructor);

  if (surface == NULL)
    {
      g_warn_if_reached ();
      meta_wayland_presentation_feedback_discard (feedback);
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  wl_list_insert (&pending->presentation_feedback_list, &feedback->link);

  feedback->surface = g_object_ref (surface);
}

static const struct wp_presentation_interface
meta_wayland_presentation_interface = {
  wp_presentation_destroy,
  wp_presentation_feedback,
};

static void
wp_presentation_bind (struct wl_client *client,
                      void             *data,
                      uint32_t          version,
                      uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_presentation_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_presentation_interface,
                                  NULL,
                                  NULL);

  /* Presentation timestamps in Mutter are guaranteed to be CLOCK_MONOTONIC. */
  wp_presentation_send_clock_id (resource, CLOCK_MONOTONIC);
}

static struct wl_list *
meta_wayland_presentation_time_ensure_feedbacks (MetaWaylandPresentationTime *presentation_time,
                                                 ClutterStageView            *stage_view,
                                                 int64_t                      view_frame_counter);

static void
on_after_paint (ClutterStage          *stage,
                ClutterStageView      *stage_view,
                ClutterFrame          *frame,
                MetaWaylandCompositor *compositor)
{
  struct wl_list *feedbacks;
  GList *l;

  feedbacks =
    meta_wayland_presentation_time_ensure_feedbacks (&compositor->presentation_time,
                                                     stage_view, frame->frame_count);

  l = compositor->presentation_time.feedback_surfaces;
  while (l)
    {
      GList *l_cur = l;
      MetaWaylandSurface *surface = l->data;
      MetaSurfaceActor *actor;

      l = l->next;

      actor = meta_wayland_surface_get_actor (surface);
      if (!actor)
        continue;

      if (!meta_surface_actor_wayland_is_view_primary (actor,
                                                       stage_view))
        continue;

      if (!wl_list_empty (&surface->presentation_time.feedback_list))
        {
          /* Add feedbacks to the list to be fired on presentation. */
          wl_list_insert_list (feedbacks,
                               &surface->presentation_time.feedback_list);
          wl_list_init (&surface->presentation_time.feedback_list);
        }

      compositor->presentation_time.feedback_surfaces =
        g_list_delete_link (compositor->presentation_time.feedback_surfaces,
                            l_cur);
    }
}

static void
destroy_feedback_list (gpointer data)
{
  struct wl_list *feedbacks = data;

  while (!wl_list_empty (feedbacks))
    {
      MetaWaylandPresentationFeedback *feedback =
        wl_container_of (feedbacks->next, feedback, link);

      meta_wayland_presentation_feedback_discard (feedback);
    }

  g_free (feedbacks);
}

static void
on_monitors_changed (MetaMonitorManager    *manager,
                     MetaWaylandCompositor *compositor)
{
  /* All ClutterStageViews were re-created, so clear our map. */
  g_hash_table_remove_all (compositor->presentation_time.feedbacks);
}

void
meta_wayland_presentation_time_finalize (MetaWaylandCompositor *compositor)
{
  MetaBackend *backend = meta_context_get_backend (compositor->context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_hash_table_destroy (compositor->presentation_time.feedbacks);

  g_signal_handlers_disconnect_by_func (monitor_manager, on_monitors_changed,
                                        compositor);
  g_signal_handlers_disconnect_by_func (monitor_manager, on_after_paint,
                                        compositor);
}

void
meta_wayland_init_presentation_time (MetaWaylandCompositor *compositor)
{
  MetaContext *context = compositor->context;
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);

  compositor->presentation_time.feedbacks =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) g_hash_table_destroy);

  g_signal_connect (monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed), compositor);

  g_signal_connect (stage, "after-paint",
                    G_CALLBACK (on_after_paint), compositor);

  if (wl_global_create (compositor->wayland_display,
                        &wp_presentation_interface,
                        META_WP_PRESENTATION_VERSION,
                        NULL,
                        wp_presentation_bind) == NULL)
    g_error ("Failed to register a global wp_presentation object");
}

void
meta_wayland_presentation_feedback_discard (MetaWaylandPresentationFeedback *feedback)
{
  wp_presentation_feedback_send_discarded (feedback->resource);
  wl_resource_destroy (feedback->resource);
}

static void
maybe_update_presentation_sequence (MetaWaylandSurface *surface,
                                    ClutterFrameInfo   *frame_info,
                                    MetaWaylandOutput  *output)
{
  unsigned int sequence_delta;

  if (surface->presentation_time.last_output == output &&
      surface->presentation_time.last_view_frame_counter ==
      frame_info->view_frame_counter)
    return;

  surface->presentation_time.last_view_frame_counter =
    frame_info->view_frame_counter;

  if (!(frame_info->flags & CLUTTER_FRAME_INFO_FLAG_VSYNC))
    goto invalid_sequence;

  /* Getting sequence = 0 after sequence = UINT_MAX is likely valid (32-bit
   * overflow, on a 144 Hz display that's ~173 days of operation). Getting it
   * otherwise is usually a driver bug.
   */
  if (frame_info->sequence == 0 &&
      !(surface->presentation_time.is_last_output_sequence_valid &&
        surface->presentation_time.last_output_sequence == UINT_MAX))
    {
      g_warning_once ("Invalid sequence for VSYNC frame info");
      goto invalid_sequence;
    }

  if (surface->presentation_time.is_last_output_sequence_valid &&
      surface->presentation_time.last_output == output)
    {
      sequence_delta =
        frame_info->sequence - surface->presentation_time.last_output_sequence;
    }
  else
    {
      /* Sequence generally has different base between different outputs, but we
       * want to keep it monotonic and without sudden jumps when the surface is
       * moved between outputs. This matches the Xorg behavior with regards to
       * the GLX_OML_sync_control implementation.
       */
      sequence_delta = 1;
    }

  surface->presentation_time.sequence += sequence_delta;
  surface->presentation_time.last_output = output;
  surface->presentation_time.last_output_sequence = frame_info->sequence;
  surface->presentation_time.is_last_output_sequence_valid = TRUE;

  return;

invalid_sequence:
  surface->presentation_time.sequence += 1;
  surface->presentation_time.last_output = output;
  surface->presentation_time.is_last_output_sequence_valid = FALSE;
}

void
meta_wayland_presentation_feedback_present (MetaWaylandPresentationFeedback *feedback,
                                            ClutterFrameInfo                *frame_info,
                                            MetaWaylandOutput               *output)
{
  MetaWaylandSurface *surface = feedback->surface;
  int64_t time_us = frame_info->presentation_time;
  uint64_t time_s;
  uint32_t tv_sec_hi, tv_sec_lo, tv_nsec;
  uint32_t refresh_interval_ns;
  uint32_t seq_hi, seq_lo;
  uint32_t flags;
  const GList *l;
  MetaMonitorMode *mode;
  gboolean is_vrr;

  if (output == NULL)
    {
      g_warning ("Output is NULL while sending presentation feedback");
      meta_wayland_presentation_feedback_discard (feedback);
      return;
    }

  time_s = us2s (time_us);

  tv_sec_hi = time_s >> 32;
  tv_sec_lo = time_s;
  tv_nsec = (uint32_t) us2ns (time_us - s2us (time_s));

  mode = meta_wayland_output_get_monitor_mode (output);

  is_vrr = meta_monitor_mode_get_refresh_rate_mode (mode) ==
           META_CRTC_REFRESH_RATE_MODE_VARIABLE;

  /* The refresh rate interval is required to be 0 for vrr in version 1.*/
  if (is_vrr && wl_resource_get_version (feedback->resource) == 1)
    refresh_interval_ns = 0;
  else
    refresh_interval_ns = (uint32_t) (0.5 + s2ns (1) / frame_info->refresh_rate);

  maybe_update_presentation_sequence (surface, frame_info, output);

  seq_hi = surface->presentation_time.sequence >> 32;
  seq_lo = surface->presentation_time.sequence;

  flags = WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;

  if (frame_info->flags & CLUTTER_FRAME_INFO_FLAG_HW_CLOCK)
    flags |= WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

  if (frame_info->flags & CLUTTER_FRAME_INFO_FLAG_ZERO_COPY)
    flags |= WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;

  if (frame_info->flags & CLUTTER_FRAME_INFO_FLAG_VSYNC)
    flags |= WP_PRESENTATION_FEEDBACK_KIND_VSYNC;

  for (l = meta_wayland_output_get_resources (output); l; l = l->next)
    {
      struct wl_resource *output_resource = l->data;

      if (feedback->resource->client == output_resource->client)
        {
          wp_presentation_feedback_send_sync_output (feedback->resource,
                                                     output_resource);
        }
    }

  wp_presentation_feedback_send_presented (feedback->resource,
                                           tv_sec_hi,
                                           tv_sec_lo,
                                           tv_nsec,
                                           refresh_interval_ns,
                                           seq_hi,
                                           seq_lo,
                                           flags);

  wl_resource_destroy (feedback->resource);
}

static struct wl_list *
meta_wayland_presentation_time_ensure_feedbacks (MetaWaylandPresentationTime *presentation_time,
                                                 ClutterStageView            *stage_view,
                                                 int64_t                      view_frame_counter)
{
  GHashTable *hash_table;
  struct wl_list *list;

  hash_table = g_hash_table_lookup (presentation_time->feedbacks, stage_view);
  if (!hash_table)
    {
      hash_table = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
                                          destroy_feedback_list);
      g_hash_table_insert (presentation_time->feedbacks, stage_view, hash_table);
    }

  list = g_hash_table_lookup (hash_table, &view_frame_counter);
  if (!list)
    {
      int64_t *key;

      key = g_new (int64_t, 1);
      *key = view_frame_counter;
      list = g_new0 (struct wl_list, 1);
      wl_list_init (list);
      g_hash_table_insert (hash_table, key, list);
    }

  return list;
}

static MetaWaylandOutput *
get_output_for_stage_view (MetaWaylandCompositor *compositor,
                           ClutterStageView      *stage_view)
{
  MetaCrtc *crtc;
  MetaOutput *output;
  MetaMonitor *monitor;

  crtc = meta_renderer_view_get_crtc (META_RENDERER_VIEW (stage_view));

  /*
   * All outputs occupy the same region of the screen, as their contents are
   * the same, so pick the first one.
   */
  output = meta_crtc_get_outputs (crtc)->data;

  monitor = meta_output_get_monitor (output);
  return g_hash_table_lookup (compositor->outputs,
                              meta_monitor_get_spec (monitor));
}

void
meta_wayland_presentation_time_present_feedbacks (MetaWaylandCompositor *compositor,
                                                  ClutterStageView      *stage_view,
                                                  ClutterFrameInfo      *frame_info)
{
  struct wl_list *feedbacks;
  GHashTable *hash_table;
  GHashTableIter iter;
  int64_t *key;

  hash_table = g_hash_table_lookup (compositor->presentation_time.feedbacks,
                                    stage_view);
  if (!hash_table)
    return;

  g_hash_table_iter_init (&iter, hash_table);
  while (g_hash_table_iter_next (&iter, (gpointer *) &key, (gpointer *)&feedbacks))
    {
      MetaWaylandPresentationFeedback *feedback, *next;

      if (*key > frame_info->view_frame_counter)
        continue;

      if (*key == frame_info->view_frame_counter)
        {
          MetaWaylandOutput *output;

          output = get_output_for_stage_view (compositor, stage_view);
          wl_list_for_each_safe (feedback, next, feedbacks, link)
            {
              meta_wayland_presentation_feedback_present (feedback,
                                                          frame_info,
                                                          output);
            }
        }

      /* This discards feedbacks for older frames which were never presented */
      g_hash_table_iter_remove (&iter);
    }

  g_hash_table_remove (hash_table, &frame_info->view_frame_counter);
}

void
meta_wayland_presentation_time_cursor_painted (MetaWaylandPresentationTime *presentation_time,
                                               ClutterStageView            *stage_view,
                                               int64_t                      view_frame_counter,
                                               MetaWaylandCursorSurface    *cursor_surface)
{
  MetaWaylandSurfaceRole *role = META_WAYLAND_SURFACE_ROLE (cursor_surface);
  MetaWaylandSurface *surface = meta_wayland_surface_role_get_surface (role);
  struct wl_list *feedbacks;

  if (wl_list_empty (&surface->presentation_time.feedback_list))
    return;

  /* Add new feedbacks. */
  feedbacks =
    meta_wayland_presentation_time_ensure_feedbacks (presentation_time,
                                                     stage_view,
                                                     view_frame_counter);
  wl_list_insert_list (feedbacks,
                       &surface->presentation_time.feedback_list);
  wl_list_init (&surface->presentation_time.feedback_list);
}
