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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-frame-native.h"

#include "backends/native/meta-kms-update.h"
#include "clutter/clutter-mutter.h"
#include "core/util-private.h"

#include <glib/gstdio.h>

struct _MetaFrameNative
{
  ClutterFrame base;

  MetaDrmBuffer *buffer;
  CoglScanout *scanout;

  MetaKmsUpdate *kms_update;

  MtkRegion *damage;
  int sync_fd;
};

static void
meta_frame_native_release (ClutterFrame *frame)
{
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);

  g_clear_fd (&frame_native->sync_fd, NULL);
  g_clear_pointer (&frame_native->damage, mtk_region_unref);
  g_clear_object (&frame_native->buffer);
  g_clear_object (&frame_native->scanout);

  g_return_if_fail (!frame_native->kms_update);
}

MetaFrameNative *
meta_frame_native_new (void)
{
  MetaFrameNative *frame_native =
    clutter_frame_new (MetaFrameNative, meta_frame_native_release);

  frame_native->sync_fd = -1;

  return frame_native;
}

MetaFrameNative *
meta_frame_native_from_frame (ClutterFrame *frame)
{
  return META_CONTAINER_OF (frame, MetaFrameNative, base);
}

MetaKmsUpdate *
meta_frame_native_ensure_kms_update (MetaFrameNative *frame_native,
                                     MetaKmsDevice   *kms_device)
{
  if (frame_native->kms_update)
    {
      g_warn_if_fail (meta_kms_update_get_device (frame_native->kms_update) ==
                      kms_device);
      return frame_native->kms_update;
    }

  frame_native->kms_update = meta_kms_update_new (kms_device);
  return frame_native->kms_update;
}

MetaKmsUpdate *
meta_frame_native_steal_kms_update (MetaFrameNative *frame_native)
{
  return g_steal_pointer (&frame_native->kms_update);
}

gboolean
meta_frame_native_has_kms_update (MetaFrameNative *frame_native)
{
  return !!frame_native->kms_update;
}

void
meta_frame_native_set_buffer (MetaFrameNative *frame_native,
                              MetaDrmBuffer   *buffer)
{
  g_set_object (&frame_native->buffer, buffer);
}

MetaDrmBuffer *
meta_frame_native_get_buffer (MetaFrameNative *frame_native)
{
  return frame_native->buffer;
}

void
meta_frame_native_set_scanout (MetaFrameNative *frame_native,
                               CoglScanout     *scanout)
{
  g_set_object (&frame_native->scanout, scanout);
}

CoglScanout *
meta_frame_native_get_scanout (MetaFrameNative *frame_native)
{
  return frame_native->scanout;
}

void
meta_frame_native_set_damage (MetaFrameNative *frame_native,
                              const MtkRegion *damage)
{
  g_clear_pointer (&frame_native->damage, mtk_region_unref);
  frame_native->damage = mtk_region_copy (damage);
}

MtkRegion *
meta_frame_native_get_damage (MetaFrameNative *frame_native)
{
  return frame_native->damage;
}

void
meta_frame_native_set_sync_fd (MetaFrameNative *frame_native,
                               int              sync_fd)
{
  g_clear_fd (&frame_native->sync_fd, NULL);
  frame_native->sync_fd = sync_fd;
}

int
meta_frame_native_steal_sync_fd (MetaFrameNative *frame_native)
{
  return g_steal_fd (&frame_native->sync_fd);
}
