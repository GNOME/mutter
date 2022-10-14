/*
 * Copyright (C) 2022 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-frame-native.h"

#include "clutter/clutter-mutter.h"

struct _MetaFrameNative
{
  ClutterFrame base;
};

static void
meta_frame_native_release (ClutterFrame *frame)
{
}

MetaFrameNative *
meta_frame_native_new (void)
{
  return clutter_frame_new (MetaFrameNative, meta_frame_native_release);
}
