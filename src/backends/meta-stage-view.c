/*
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.

 * Authors:
 *  Matthew Allum
 *  Robert Bragg
 *  Neil Roberts
 *  Emmanuele Bassi
 */

#include "config.h"

#include "backends/meta-stage-view-private.h"

typedef struct _MetaStageViewPrivate
{
  /* Damage history, in stage view render target framebuffer coordinate space.
   */
  ClutterDamageHistory *damage_history;

  guint notify_presented_handle_id;

  CoglFrameClosure *frame_cb_closure;

  int inhibit_cursor_overlay_count;
} MetaStageViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaStageView, meta_stage_view,
                            CLUTTER_TYPE_STAGE_VIEW)

static void
frame_cb (CoglOnscreen  *onscreen,
          CoglFrameEvent frame_event,
          CoglFrameInfo *frame_info,
          void          *user_data)
{
  ClutterStageView *view = user_data;

  if (frame_event == COGL_FRAME_EVENT_SYNC)
    return;

  if (cogl_frame_info_get_is_symbolic (frame_info))
    {
      clutter_stage_view_notify_ready (view);
    }
  else
    {
      ClutterFrameInfo clutter_frame_info;
      ClutterFrameInfoFlag flags = CLUTTER_FRAME_INFO_FLAG_NONE;

      if (cogl_frame_info_is_hw_clock (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_HW_CLOCK;

      if (cogl_frame_info_is_zero_copy (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_ZERO_COPY;

      if (cogl_frame_info_is_vsync (frame_info))
        flags |= CLUTTER_FRAME_INFO_FLAG_VSYNC;

      clutter_frame_info = (ClutterFrameInfo) {
        .frame_counter = cogl_frame_info_get_global_frame_counter (frame_info),
        .refresh_rate = cogl_frame_info_get_refresh_rate (frame_info),
        .presentation_time =
          cogl_frame_info_get_presentation_time_us (frame_info),
        .flags = flags,
        .sequence = cogl_frame_info_get_sequence (frame_info),
        .gpu_rendering_duration_ns =
          cogl_frame_info_get_rendering_duration_ns (frame_info),
        .cpu_time_before_buffer_swap_us =
          cogl_frame_info_get_time_before_buffer_swap_us (frame_info),
      };
      clutter_stage_view_notify_presented (view, &clutter_frame_info);
    }
}

static void
meta_stage_view_dispose (GObject *object)
{
  MetaStageView *view = META_STAGE_VIEW (object);
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);

  g_clear_handle_id (&priv->notify_presented_handle_id, g_source_remove);
  g_clear_pointer (&priv->damage_history, clutter_damage_history_free);

  if (priv->frame_cb_closure)
    {
      CoglFramebuffer *framebuffer;

      framebuffer = clutter_stage_view_get_onscreen (stage_view);
      cogl_onscreen_remove_frame_callback (COGL_ONSCREEN (framebuffer),
                                           priv->frame_cb_closure);
      priv->frame_cb_closure = NULL;
    }

  G_OBJECT_CLASS (meta_stage_view_parent_class)->dispose (object);
}

static void
meta_stage_view_constructed (GObject *object)
{
  MetaStageView *view = META_STAGE_VIEW (object);
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);
  ClutterStageView *stage_view = CLUTTER_STAGE_VIEW (view);
  CoglFramebuffer *framebuffer;

  framebuffer = clutter_stage_view_get_onscreen (stage_view);
  if (framebuffer && COGL_IS_ONSCREEN (framebuffer))
    {
      priv->frame_cb_closure =
        cogl_onscreen_add_frame_callback (COGL_ONSCREEN (framebuffer),
                                          frame_cb,
                                          view,
                                          NULL);
    }

  G_OBJECT_CLASS (meta_stage_view_parent_class)->constructed (object);
}

static ClutterPaintFlag
meta_stage_view_get_default_paint_flags (ClutterStageView *clutter_view)
{
  MetaStageView *view = META_STAGE_VIEW (clutter_view);
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  if (priv->inhibit_cursor_overlay_count > 0)
    return CLUTTER_PAINT_FLAG_NO_CURSORS;
  else
    return CLUTTER_PAINT_FLAG_NONE;
}

static void
meta_stage_view_init (MetaStageView *view)
{
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  priv->damage_history = clutter_damage_history_new ();
}

static void
meta_stage_view_class_init (MetaStageViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterStageViewClass *view_class = CLUTTER_STAGE_VIEW_CLASS (klass);

  object_class->constructed = meta_stage_view_constructed;
  object_class->dispose = meta_stage_view_dispose;

  view_class->get_default_paint_flags =
    meta_stage_view_get_default_paint_flags;
}

ClutterDamageHistory *
meta_stage_view_get_damage_history (MetaStageView *view)
{
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  return priv->damage_history;
}

typedef struct _NotifyPresentedClosure
{
  ClutterStageView *view;
  ClutterFrameInfo frame_info;
} NotifyPresentedClosure;

static gboolean
notify_presented_idle (gpointer user_data)
{
  NotifyPresentedClosure *closure = user_data;
  MetaStageView *view = META_STAGE_VIEW (closure->view);
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  priv->notify_presented_handle_id = 0;
  clutter_stage_view_notify_presented (closure->view, &closure->frame_info);

  return G_SOURCE_REMOVE;
}

void
meta_stage_view_perform_fake_swap (MetaStageView *view,
                                   int64_t        counter)
{
  ClutterStageView *clutter_view = CLUTTER_STAGE_VIEW (view);
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);
  NotifyPresentedClosure *closure;

  closure = g_new0 (NotifyPresentedClosure, 1);
  closure->view = clutter_view;
  closure->frame_info = (ClutterFrameInfo) {
    .frame_counter = counter,
    .refresh_rate = clutter_stage_view_get_refresh_rate (clutter_view),
    .presentation_time = g_get_monotonic_time (),
    .flags = CLUTTER_FRAME_INFO_FLAG_NONE,
    .sequence = 0,
  };

  g_warn_if_fail (priv->notify_presented_handle_id == 0);
  priv->notify_presented_handle_id =
    g_idle_add_full (G_PRIORITY_DEFAULT,
                     notify_presented_idle,
                     closure, g_free);
}

void
meta_stage_view_inhibit_cursor_overlay (MetaStageView *view)
{
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  priv->inhibit_cursor_overlay_count++;
}

void
meta_stage_view_uninhibit_cursor_overlay (MetaStageView *view)
{
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  g_return_if_fail (priv->inhibit_cursor_overlay_count > 0);

  priv->inhibit_cursor_overlay_count--;
}

gboolean
meta_stage_view_is_cursor_overlay_inhibited (MetaStageView *view)
{
  MetaStageViewPrivate *priv =
    meta_stage_view_get_instance_private (view);

  return priv->inhibit_cursor_overlay_count > 0;
}
