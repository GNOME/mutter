/*
 * Copyright (C) 2015 Red Hat
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
 * Authored by:
 *      Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/native/meta-event-native.h"
#include "backends/native/meta-input-thread.h"
#include "clutter/clutter-mutter.h"

typedef struct _MetaEventNative MetaEventNative;

struct _MetaEventNative
{
  uint64_t time_usec;

  gboolean has_relative_motion;
  double dx;
  double dy;
  double dx_unaccel;
  double dy_unaccel;
};

MetaEventNative *
meta_event_native_copy (MetaEventNative *event_evdev)
{
  if (event_evdev != NULL)
    return g_slice_dup (MetaEventNative, event_evdev);

  return NULL;
}

void
meta_event_native_free (MetaEventNative *event_evdev)
{
  if (event_evdev != NULL)
    g_slice_free (MetaEventNative, event_evdev);
}
