/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#pragma once

#include "backends/meta-backend-types.h"
#include "core/util-private.h"
#include "meta/common.h"
#include "meta/boxes.h"
#include "mtk/mtk.h"

#define META_TYPE_CURSOR_SPRITE (meta_cursor_sprite_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCursorSprite,
                          meta_cursor_sprite,
                          META, CURSOR_SPRITE,
                          GObject)

struct _MetaCursorSpriteClass
{
  GObjectClass parent_class;

  void (* invalidate) (MetaCursorSprite *sprite);
  gboolean (* realize_texture) (MetaCursorSprite *sprite);
  gboolean (* is_animated) (MetaCursorSprite *sprite);
  void (* tick_frame) (MetaCursorSprite *sprite);
  unsigned int (* get_current_frame_time) (MetaCursorSprite *sprite);
  void (* prepare_at) (MetaCursorSprite *sprite,
                       float             best_scale,
                       int               x,
                       int               y);
};

void meta_cursor_sprite_prepare_at (MetaCursorSprite *sprite,
                                    float             best_scale,
                                    int               x,
                                    int               y);

void meta_cursor_sprite_invalidate (MetaCursorSprite *sprite);
gboolean meta_cursor_sprite_realize_texture (MetaCursorSprite *sprite);

void meta_cursor_sprite_clear_texture (MetaCursorSprite *sprite);

void meta_cursor_sprite_set_texture (MetaCursorSprite *sprite,
                                     CoglTexture      *texture,
                                     int               hot_x,
                                     int               hot_y);

void meta_cursor_sprite_set_texture_scale (MetaCursorSprite *sprite,
                                           float             scale);

void meta_cursor_sprite_set_texture_transform (MetaCursorSprite    *sprite,
                                               MtkMonitorTransform  transform);

void meta_cursor_sprite_set_viewport_src_rect (MetaCursorSprite      *sprite,
                                               const graphene_rect_t *src_rect);

void meta_cursor_sprite_reset_viewport_src_rect (MetaCursorSprite *sprite);

void meta_cursor_sprite_set_viewport_dst_size (MetaCursorSprite *sprite,
                                               int               dst_width,
                                               int               dst_height);

void meta_cursor_sprite_reset_viewport_dst_size (MetaCursorSprite *sprite);

CoglTexture *meta_cursor_sprite_get_cogl_texture (MetaCursorSprite *sprite);

void meta_cursor_sprite_get_hotspot (MetaCursorSprite *sprite,
                                     int              *hot_x,
                                     int              *hot_y);

int meta_cursor_sprite_get_width (MetaCursorSprite *sprite);

int meta_cursor_sprite_get_height (MetaCursorSprite *sprite);

float meta_cursor_sprite_get_texture_scale (MetaCursorSprite *sprite);

MtkMonitorTransform meta_cursor_sprite_get_texture_transform (MetaCursorSprite *sprite);

const graphene_rect_t * meta_cursor_sprite_get_viewport_src_rect (MetaCursorSprite *sprite);

gboolean meta_cursor_sprite_get_viewport_dst_size (MetaCursorSprite *sprite,
                                                   int              *dst_width,
                                                   int              *dst_height);

gboolean meta_cursor_sprite_is_animated (MetaCursorSprite *sprite);

void meta_cursor_sprite_tick_frame (MetaCursorSprite *sprite);

unsigned int meta_cursor_sprite_get_current_frame_time (MetaCursorSprite *sprite);

ClutterColorState * meta_cursor_sprite_get_color_state (MetaCursorSprite *sprite);

MetaCursorTracker * meta_cursor_sprite_get_cursor_tracker (MetaCursorSprite *sprite);
