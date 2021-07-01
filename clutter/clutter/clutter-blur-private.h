/*
 * Copyright (C) 2020 Endless OS Foundation, LLC
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
 */

#pragma once

#include <glib-object.h>

#include "cogl/cogl.h"

G_BEGIN_DECLS

typedef struct _ClutterBlur ClutterBlur;

ClutterBlur * clutter_blur_new (CoglTexture *texture,
                                float        radius);

void clutter_blur_apply (ClutterBlur *blur);

CoglTexture * clutter_blur_get_texture (ClutterBlur *blur);

void clutter_blur_free (ClutterBlur *blur);

G_END_DECLS
