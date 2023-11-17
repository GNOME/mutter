/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include "clutter/clutter.h"
#include "core/meta-context-private.h"
#include "core/util-private.h"
#include "meta/types.h"
#include "meta/meta-wayland-compositor.h"
#include "wayland/meta-wayland-text-input.h"
#include "wayland/meta-wayland-types.h"

META_EXPORT_TEST
void                    meta_wayland_override_display_name (const char *display_name);

MetaWaylandCompositor * meta_wayland_compositor_new             (MetaContext *context);

void                    meta_wayland_compositor_prepare_shutdown (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_update          (MetaWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);

gboolean                meta_wayland_compositor_handle_event    (MetaWaylandCompositor *compositor,
                                                                 const ClutterEvent    *event);

void                    meta_wayland_compositor_update_key_state (MetaWaylandCompositor *compositor,
                                                                 char                  *key_vector,
                                                                  int                    key_vector_len,
                                                                  int                    offset);

void                    meta_wayland_compositor_set_input_focus (MetaWaylandCompositor *compositor,
                                                                 MetaWindow            *window);

void                    meta_wayland_compositor_paint_finished  (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_add_frame_callback_surface (MetaWaylandCompositor *compositor,
                                                                            MetaWaylandSurface    *surface);

void                    meta_wayland_compositor_remove_frame_callback_surface (MetaWaylandCompositor *compositor,
                                                                               MetaWaylandSurface    *surface);

void                    meta_wayland_compositor_add_presentation_feedback_surface (MetaWaylandCompositor *compositor,
                                                                                   MetaWaylandSurface    *surface);

void                    meta_wayland_compositor_remove_presentation_feedback_surface (MetaWaylandCompositor *compositor,
                                                                                      MetaWaylandSurface    *surface);

GQueue                 *meta_wayland_compositor_get_committed_transactions (MetaWaylandCompositor *compositor);

META_EXPORT_TEST
const char             *meta_wayland_get_wayland_display_name   (MetaWaylandCompositor *compositor);

#ifdef HAVE_XWAYLAND
META_EXPORT_TEST
const char             *meta_wayland_get_public_xwayland_display_name  (MetaWaylandCompositor *compositor);

const char             *meta_wayland_get_private_xwayland_display_name (MetaWaylandCompositor *compositor);
#endif

void                    meta_wayland_compositor_restore_shortcuts      (MetaWaylandCompositor *compositor,
                                                                        ClutterInputDevice    *source);

gboolean                meta_wayland_compositor_is_shortcuts_inhibited (MetaWaylandCompositor *compositor,
                                                                        ClutterInputDevice    *source);

void                    meta_wayland_compositor_flush_clients (MetaWaylandCompositor *compositor);

void                    meta_wayland_compositor_schedule_surface_association (MetaWaylandCompositor *compositor,
                                                                              int                    id,
                                                                              MetaWindow            *window);

MetaWaylandTextInput *  meta_wayland_compositor_get_text_input (MetaWaylandCompositor *compositor);

#ifdef HAVE_XWAYLAND
void                    meta_wayland_compositor_notify_surface_id (MetaWaylandCompositor *compositor,
                                                                   int                    id,
                                                                   MetaWaylandSurface    *surface);

META_EXPORT_TEST
MetaXWaylandManager *   meta_wayland_compositor_get_xwayland_manager (MetaWaylandCompositor *compositor);
#endif

META_EXPORT_TEST
MetaContext * meta_wayland_compositor_get_context (MetaWaylandCompositor *compositor);

META_EXPORT_TEST
MetaWaylandFilterManager * meta_wayland_compositor_get_filter_manager (MetaWaylandCompositor *compositor);

void meta_wayland_compositor_sync_focus (MetaWaylandCompositor *compositor);
