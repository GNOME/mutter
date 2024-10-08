/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * MetaShadowFactory:
 *
 * Create and cache shadow textures for arbitrary window shapes
 *
 * Copyright (C) 2010 Red Hat, Inc.
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

#pragma once

#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "x11/meta-window-shape.h"

GType meta_shadow_get_type (void) G_GNUC_CONST;

/**
 * MetaShadowParams:
 * @radius: the radius (gaussian standard deviation) of the shadow
 * @top_fade: if >= 0, the shadow doesn't extend above the top
 *  of the shape, and fades out over the given number of pixels
 * @x_offset: horizontal offset of the shadow with respect to the
 *  shape being shadowed, in pixels
 * @y_offset: vertical offset of the shadow with respect to the
 *  shape being shadowed, in pixels
 * @opacity: opacity of the shadow, from 0 to 255
 *
 * Information about how to draw a particular style of shadow.
 */

typedef struct _MetaShadowParams MetaShadowParams;

struct _MetaShadowParams
{
  int radius;
  int top_fade;
  int x_offset;
  int y_offset;
  guint8 opacity;
};

#define META_TYPE_SHADOW_FACTORY (meta_shadow_factory_get_type ())

G_DECLARE_FINAL_TYPE (MetaShadowFactory,
                      meta_shadow_factory,
                      META, SHADOW_FACTORY,
                      GObject)

/**
 * MetaShadowFactory:
 *
 * Create window shadows.
 *
 * It caches shadows internally so that multiple shadows created for
 * the same shape with the same radius will share the same [struct@Meta.Shadow].
 */
MetaShadowFactory *meta_shadow_factory_get_default (void);

void meta_shadow_factory_get_params (MetaShadowFactory *factory,
                                     const char        *class_name,
                                     gboolean           focused,
                                     MetaShadowParams  *params);

/**
 * MetaShadow:
 *
 * Holds a shadow texture along with information about how to
 * apply that texture to draw a window texture.
 *
 * E.g., it knows how big the unscaled borders are on each
 * side of the shadow texture.
 */
typedef struct _MetaShadow MetaShadow;

MetaShadow *meta_shadow_ref         (MetaShadow            *shadow);

void        meta_shadow_unref       (MetaShadow            *shadow);

void        meta_shadow_paint       (MetaShadow      *shadow,
                                     CoglFramebuffer *framebuffer,
                                     int              window_x,
                                     int              window_y,
                                     int              window_width,
                                     int              window_height,
                                     guint8           opacity,
                                     MtkRegion       *clip,
                                     gboolean         clip_strictly);

void        meta_shadow_get_bounds  (MetaShadow   *shadow,
                                     int           window_x,
                                     int           window_y,
                                     int           window_width,
                                     int           window_height,
                                     MtkRectangle *bounds);

MetaShadow *meta_shadow_factory_get_shadow (MetaShadowFactory *factory,
                                            MetaWindowShape   *shape,
                                            int                width,
                                            int                height,
                                            const char        *class_name,
                                            gboolean           focused,
                                            CoglContext       *cogl_context);
