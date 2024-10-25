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
  /* 3 bits to define TransferFunction enums
   * + 1 bit to define Gamma TF */
  guint source_eotf_bits : 4;
  guint target_eotf_bits : 4;
  /* When there is a luminance mapping snippet */
  guint luminance_bit    : 1;
  /* When there is a color trans snippet */
  guint color_trans_bit  : 1;
} ClutterColorTransformKey;

void clutter_color_transform_key_init (ClutterColorTransformKey *key,
                                       ClutterColorState        *color_state,
                                       ClutterColorState        *target_color_state);

guint clutter_color_transform_key_hash (gconstpointer data);

gboolean clutter_color_transform_key_equal (gconstpointer data1,
                                            gconstpointer data2);
