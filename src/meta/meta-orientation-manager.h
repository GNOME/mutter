/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2024 Red Hat
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

#include <glib-object.h>

#include "meta/common.h"
#include "mtk/mtk.h"

typedef enum
{
  META_ORIENTATION_UNDEFINED,
  META_ORIENTATION_NORMAL,
  META_ORIENTATION_BOTTOM_UP,
  META_ORIENTATION_LEFT_UP,
  META_ORIENTATION_RIGHT_UP
} MetaOrientation;
#define META_N_ORIENTATIONS (META_ORIENTATION_RIGHT_UP + 1)

#define META_TYPE_ORIENTATION_MANAGER (meta_orientation_manager_get_type ())

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaOrientationManager,
                      meta_orientation_manager,
                      META,
                      ORIENTATION_MANAGER,
                      GObject)

META_EXPORT
MetaOrientation meta_orientation_manager_get_orientation (MetaOrientationManager *self);

META_EXPORT
gboolean meta_orientation_manager_has_accelerometer (MetaOrientationManager *self);

META_EXPORT
MtkMonitorTransform meta_orientation_to_transform (MetaOrientation orientation);

void meta_orientation_manager_inhibit_tracking (MetaOrientationManager *self);

void meta_orientation_manager_uninhibit_tracking (MetaOrientationManager *self);
