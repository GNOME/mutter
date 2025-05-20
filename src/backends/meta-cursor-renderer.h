/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-cursor.h"
#include "core/util-private.h"

#define META_TYPE_HW_CURSOR_INHIBITOR (meta_hw_cursor_inhibitor_get_type ())
G_DECLARE_INTERFACE (MetaHwCursorInhibitor, meta_hw_cursor_inhibitor,
                     META, HW_CURSOR_INHIBITOR, GObject)

struct _MetaHwCursorInhibitorInterface
{
  GTypeInterface parent_iface;

  gboolean (* is_cursor_inhibited) (MetaHwCursorInhibitor *inhibitor);
};

gboolean meta_hw_cursor_inhibitor_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor);

#define META_TYPE_CURSOR_RENDERER (meta_cursor_renderer_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCursorRenderer, meta_cursor_renderer,
                          META, CURSOR_RENDERER, GObject);

struct _MetaCursorRendererClass
{
  GObjectClass parent_class;

  gboolean (* update_cursor) (MetaCursorRenderer *renderer,
                              MetaCursorSprite   *cursor_sprite);
};

MetaCursorRenderer * meta_cursor_renderer_new (MetaBackend   *backend,
                                               ClutterSprite *sprite);

void meta_cursor_renderer_set_cursor (MetaCursorRenderer *renderer,
                                      MetaCursorSprite   *cursor_sprite);

void meta_cursor_renderer_update_position (MetaCursorRenderer *renderer);
void meta_cursor_renderer_force_update (MetaCursorRenderer *renderer);

META_EXPORT_TEST
MetaCursorSprite * meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer);

graphene_rect_t meta_cursor_renderer_calculate_rect (MetaCursorRenderer *renderer,
                                                     MetaCursorSprite   *cursor_sprite);

void meta_cursor_renderer_emit_painted (MetaCursorRenderer *renderer,
                                        MetaCursorSprite   *cursor_sprite,
                                        ClutterStageView   *stage_view,
                                        int64_t             view_frame_counter);

ClutterSprite * meta_cursor_renderer_get_sprite (MetaCursorRenderer *renderer);

void meta_cursor_renderer_update_stage_overlay (MetaCursorRenderer *renderer,
                                                MetaCursorSprite   *cursor_sprite);

MetaBackend * meta_cursor_renderer_get_backend (MetaCursorRenderer *renderer);
