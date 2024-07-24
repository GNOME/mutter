/*
 * Copyright (C) 2021-2024 Robert Mader <robert.mader@posteo.de>
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

#include <graphene.h>

#include "mtk/mtk-macros.h"
#include "mtk/mtk-monitor-transform.h"

MTK_EXPORT
void mtk_compute_viewport_matrix (graphene_matrix_t     *matrix,
                                  int                    width,
                                  int                    height,
                                  float                  scale,
                                  MtkMonitorTransform    transform,
                                  const graphene_rect_t *src_rect);
