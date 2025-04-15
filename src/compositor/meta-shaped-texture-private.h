/*
 * shaped texture
 *
 * An actor to draw a texture clipped to a list of rectangles
 *
 * Authored By Neil Roberts  <neil@linux.intel.com>
 *
 * Copyright (C) 2008 Intel Corporation
 *               2013 Red Hat, Inc.
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

#include "backends/meta-monitor-manager-private.h"
#include "meta/meta-multi-texture-format.h"

#include "meta/meta-shaped-texture.h"

MetaShapedTexture * meta_shaped_texture_new (ClutterContext    *clutter_context,
                                             ClutterColorState *color_state);

void meta_shaped_texture_set_texture (MetaShapedTexture *stex,
                                      MetaMultiTexture  *multi_texture);
void meta_shaped_texture_set_color_state (MetaShapedTexture *stex,
                                          ClutterColorState *color_state);
void meta_shaped_texture_set_is_y_inverted (MetaShapedTexture *stex,
                                            gboolean           is_y_inverted);
void meta_shaped_texture_set_snippet (MetaShapedTexture *stex,
                                      CoglSnippet       *snippet);
void meta_shaped_texture_set_fallback_size (MetaShapedTexture *stex,
                                            int                fallback_width,
                                            int                fallback_height);
MtkRegion * meta_shaped_texture_get_opaque_region (MetaShapedTexture *stex);
gboolean meta_shaped_texture_is_opaque (MetaShapedTexture *stex);
gboolean meta_shaped_texture_has_alpha (MetaShapedTexture *stex);
void meta_shaped_texture_set_transform (MetaShapedTexture    *stex,
                                        MtkMonitorTransform  transform);
void meta_shaped_texture_set_viewport_src_rect (MetaShapedTexture *stex,
                                                graphene_rect_t   *src_rect);
void meta_shaped_texture_reset_viewport_src_rect (MetaShapedTexture *stex);
void meta_shaped_texture_set_viewport_dst_size (MetaShapedTexture *stex,
                                                int                dst_width,
                                                int                dst_height);
void meta_shaped_texture_reset_viewport_dst_size (MetaShapedTexture *stex);
void meta_shaped_texture_set_buffer_scale (MetaShapedTexture *stex,
                                           int                buffer_scale);

gboolean meta_shaped_texture_update_area (MetaShapedTexture  *stex,
                                          const MtkRectangle *area,
                                          MtkRectangle       *clip);

int meta_shaped_texture_get_width (MetaShapedTexture *stex);
int meta_shaped_texture_get_height (MetaShapedTexture *stex);

float meta_shaped_texture_get_unscaled_width (MetaShapedTexture *stex);
float meta_shaped_texture_get_unscaled_height (MetaShapedTexture *stex);

void meta_shaped_texture_set_clip_region (MetaShapedTexture *stex,
                                          MtkRegion         *clip_region);
void meta_shaped_texture_set_opaque_region (MetaShapedTexture *stex,
                                            MtkRegion         *opaque_region);

void meta_shaped_texture_ensure_size_valid (MetaShapedTexture *stex);

gboolean meta_shaped_texture_should_get_via_offscreen (MetaShapedTexture *stex);

void meta_shaped_texture_set_color_repr (MetaShapedTexture            *stex,
                                         MetaMultiTextureAlphaMode     premult,
                                         MetaMultiTextureCoefficients  coeffs);
