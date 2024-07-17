/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024  Red Hat
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
 *
 */

#pragma once

#include "clutter/clutter-color-state.h"

typedef struct _ClutterColorTransformKey
{
  struct {
    ClutterColorspace colorspace;
    ClutterTransferFunction transfer_function;
  } source;
  struct {
    ClutterColorspace colorspace;
    ClutterTransferFunction transfer_function;
  } target;
} ClutterColorTransformKey;

void clutter_color_transform_key_init (ClutterColorTransformKey *key,
                                       ClutterColorState        *color_state,
                                       ClutterColorState        *target_color_state);

guint clutter_color_transform_key_hash (gconstpointer data);

gboolean clutter_color_transform_key_equal (gconstpointer data1,
                                            gconstpointer data2);
