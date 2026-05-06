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
  ClutterFrame parent;

  MetaDrmBuffer *buffer;
  CoglScanout *scanout;

  MetaKmsUpdate *kms_update;

  MtkRegion *damage;

  GPollFD sync;
};

static void
meta_frame_native_release (ClutterFrame *frame)
{
  MetaFrameNative *frame_native = meta_frame_native_from_frame (frame);

  g_clear_fd (&frame_native->sync.fd, NULL);
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

  frame_native->sync.fd = -1;
  frame_native->sync.events = 0;

  return frame_native;
}

MetaFrameNative *
meta_frame_native_from_frame (ClutterFrame *frame)
{
  return META_CONTAINER_OF (frame, MetaFrameNative, parent);
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
  g_clear_fd (&frame_native->sync.fd, NULL);
  frame_native->sync.fd = sync_fd;
}

int
meta_frame_native_steal_sync_fd (MetaFrameNative *frame_native)
{
  g_warn_if_fail (frame_native->sync.events == 0);

  return g_steal_fd (&frame_native->sync.fd);
}

void
meta_frame_native_add_source (MetaFrameNative *frame_native,
                              GSource         *source)
{
  g_return_if_fail (frame_native->sync.fd >= 0);
  g_return_if_fail (frame_native->sync.events == 0);

  frame_native->sync.events = G_IO_IN;
  g_source_add_poll (source, &frame_native->sync);
}

void
meta_frame_native_remove_source (MetaFrameNative *frame_native,
                                 GSource         *source)
{
  g_return_if_fail (frame_native->sync.fd >= 0);
  g_return_if_fail (frame_native->sync.events != 0);

  g_source_remove_poll (source, &frame_native->sync);
  frame_native->sync.events = 0;
}

gboolean
meta_frame_native_is_ready (MetaFrameNative *frame_native)
{
  g_return_val_if_fail (frame_native->sync.fd >= 0, FALSE);
  g_return_val_if_fail (frame_native->sync.events != 0, FALSE);

  return !!(frame_native->sync.revents & G_IO_IN);
}
