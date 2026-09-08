/*
 * Copyright (C) 2026 Kristof Imeri
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
#include "mtk/mtk.h"

typedef struct _MetaBackgroundBlur MetaBackgroundBlur;

MtkRegion * meta_background_effect_create_blur_sample_region (const MtkRegion *blur_region,
                                                              float            radius);

MetaBackgroundBlur * meta_background_blur_new (ClutterActor    *actor,
                                               const MtkRegion *blur_region,
                                               float            radius);
void meta_background_blur_destroy (MetaBackgroundBlur *blur);

void meta_background_effect_paint_blur_region (ClutterPaintNode       *root_node,
                                               ClutterActor           *actor,
                                               ClutterPaintContext    *paint_context,
                                               const ClutterActorBox  *content_box,
                                               int                     content_width,
                                               int                     content_height,
                                               const MtkRegion        *blur_region,
                                               const MtkRegion        *clip_region,
                                               float                   radius,
                                               float                   saturation,
                                               float                   noise,
                                               uint8_t                 opacity);
