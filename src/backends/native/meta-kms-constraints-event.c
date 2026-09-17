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

#include "config.h"

#include "backends/native/meta-kms-constraints-event.h"

#include <string.h>

#include "backends/native/meta-drm-constraints.h"

gboolean
meta_kms_constraints_event_decode_list_change (
  const struct drm_event        *event,
  MetaKmsConstraintsListChange *change)
{
  struct drm_event base;
  struct drm_event_kms_constraints_list_changed wire_event;
  gboolean is_closed;

  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (change != NULL, FALSE);

  memcpy (&base, event, sizeof (base));
  if (base.type != DRM_EVENT_KMS_CONSTRAINTS_LIST_CHANGED ||
      base.length != sizeof (wire_event))
    return FALSE;

  memcpy (&wire_event, event, sizeof (wire_event));
  if (wire_event.crtc_id == 0 ||
      wire_event.reserved != 0 ||
      (wire_event.flags & ~DRM_KMS_CONSTRAINTS_LIST_CLOSED) != 0)
    return FALSE;

  is_closed = !!(wire_event.flags & DRM_KMS_CONSTRAINTS_LIST_CLOSED);
  if ((is_closed && wire_event.generation != 0) ||
      (!is_closed && wire_event.generation == 0))
    return FALSE;

  *change = (MetaKmsConstraintsListChange) {
    .crtc_id = wire_event.crtc_id,
    .generation = wire_event.generation,
    .is_closed = is_closed,
  };
  return TRUE;
}
