/*
 * Copyright (C) 2021 Red Hat
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

#include "backends/native/meta-crtc-virtual.h"

struct _MetaCrtcVirtual
{
  MetaCrtcNative parent;
};

#define META_CRTC_VIRTUAL_ID_BIT (((uint64_t) 1) << 63)

G_DEFINE_TYPE (MetaCrtcVirtual, meta_crtc_virtual, META_TYPE_CRTC_NATIVE)

MetaCrtcVirtual *
meta_crtc_virtual_new (uint64_t id)
{
  return g_object_new (META_TYPE_CRTC_VIRTUAL,
                       "id", META_CRTC_VIRTUAL_ID_BIT | id,
                       NULL);
}

static gboolean
meta_crtc_virtual_is_transform_handled (MetaCrtcNative       *crtc_native,
                                        MetaMonitorTransform  transform)
{
  return transform == META_MONITOR_TRANSFORM_NORMAL;
}

static void
meta_crtc_virtual_init (MetaCrtcVirtual *crtc_virtual)
{
}

static void
meta_crtc_virtual_class_init (MetaCrtcVirtualClass *klass)
{
  MetaCrtcNativeClass *crtc_native_class = META_CRTC_NATIVE_CLASS (klass);

  crtc_native_class->is_transform_handled =
    meta_crtc_virtual_is_transform_handled;
}
