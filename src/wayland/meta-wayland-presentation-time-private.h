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

#pragma once

#include <wayland-server.h>

#include "clutter/clutter.h"
#include "wayland/meta-wayland-cursor-surface.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWaylandPresentationFeedback
{
  struct wl_list link;
  struct wl_resource *resource;

  MetaWaylandSurface *surface;
} MetaWaylandPresentationFeedback;

typedef struct _MetaWaylandPresentationTime
{
  GList *feedback_surfaces;

  /*
   * A mapping from (ClutterStageView *) to a
   * (MetaWaylandPresentationFeedback *) wl_list of presentation-time feedbacks
   * that are scheduled to be presented.
   */
  GHashTable *feedbacks;
} MetaWaylandPresentationTime;

void meta_wayland_presentation_time_finalize (MetaWaylandCompositor *compositor);

void meta_wayland_init_presentation_time (MetaWaylandCompositor *compositor);

void meta_wayland_presentation_feedback_discard (MetaWaylandPresentationFeedback *feedback);

void meta_wayland_presentation_feedback_present (MetaWaylandPresentationFeedback *feedback,
                                                 ClutterFrameInfo                *frame_info,
                                                 MetaWaylandOutput               *output);

void meta_wayland_presentation_time_present_feedbacks (MetaWaylandCompositor *compositor,
                                                       ClutterStageView      *stage_view,
                                                       ClutterFrameInfo      *frame_info);

void meta_wayland_presentation_time_cursor_painted (MetaWaylandPresentationTime *presentation_time,
                                                    ClutterStageView            *stage_view,
                                                    int64_t                      view_frame_counter,
                                                    MetaWaylandCursorSurface    *cursor_surface);
