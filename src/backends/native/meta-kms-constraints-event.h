/*
 * Copyright (C) 2026 Red Hat
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

#include <glib.h>
#include <stdint.h>
#include <xf86drm.h>

typedef struct _MetaKmsConstraintsListChange
{
  uint32_t crtc_id;
  uint64_t generation;
  gboolean is_closed;
} MetaKmsConstraintsListChange;

gboolean meta_kms_constraints_event_decode_list_change (
  const struct drm_event        *event,
  MetaKmsConstraintsListChange *change);
