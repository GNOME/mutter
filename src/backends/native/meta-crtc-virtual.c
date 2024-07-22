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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
meta_crtc_virtual_new (MetaBackend *backend,
                       uint64_t     id)
{
  return g_object_new (META_TYPE_CRTC_VIRTUAL,
                       "backend", backend,
                       "id", META_CRTC_VIRTUAL_ID_BIT | id,
                       NULL);
}

static size_t
meta_crtc_virtual_get_gamma_lut_size (MetaCrtc *crtc)
{
  return 0;
}

static MetaGammaLut *
meta_crtc_virtual_get_gamma_lut (MetaCrtc *crtc)
{
  return NULL;
}

static void
meta_crtc_virtual_set_gamma_lut (MetaCrtc           *crtc,
                                 const MetaGammaLut *lut)
{
  g_warn_if_reached ();
}

static gboolean
meta_crtc_virtual_is_transform_handled (MetaCrtcNative      *crtc_native,
                                        MtkMonitorTransform  transform)
{
  return transform == MTK_MONITOR_TRANSFORM_NORMAL;
}

static gboolean
meta_crtc_virtual_is_hw_cursor_supported (MetaCrtcNative *crtc_native)
{
  return TRUE;
}

static int64_t
meta_crtc_virtual_get_deadline_evasion (MetaCrtcNative *crtc_native)
{
  return 0;
}

static void
meta_crtc_virtual_init (MetaCrtcVirtual *crtc_virtual)
{
}

static void
meta_crtc_virtual_class_init (MetaCrtcVirtualClass *klass)
{
  MetaCrtcClass *crtc_class = META_CRTC_CLASS (klass);
  MetaCrtcNativeClass *crtc_native_class = META_CRTC_NATIVE_CLASS (klass);

  crtc_class->get_gamma_lut_size = meta_crtc_virtual_get_gamma_lut_size;
  crtc_class->get_gamma_lut = meta_crtc_virtual_get_gamma_lut;
  crtc_class->set_gamma_lut = meta_crtc_virtual_set_gamma_lut;

  crtc_native_class->is_transform_handled =
    meta_crtc_virtual_is_transform_handled;
  crtc_native_class->is_hw_cursor_supported =
    meta_crtc_virtual_is_hw_cursor_supported;
  crtc_native_class->get_deadline_evasion =
    meta_crtc_virtual_get_deadline_evasion;
}
