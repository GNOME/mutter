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

#include "backends/native/meta-output-virtual.h"

#include "backends/native/meta-crtc-mode-virtual.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/meta-virtual-monitor.h"

struct _MetaOutputVirtual
{
  MetaOutputNative parent;
};

#define META_OUTPUT_VIRTUAL_ID_BIT (((uint64_t) 1) << 63)

G_DEFINE_TYPE (MetaOutputVirtual, meta_output_virtual, META_TYPE_OUTPUT_NATIVE)

MetaOutputVirtual *
meta_output_virtual_new (uint64_t                      id,
                         const MetaVirtualMonitorInfo *info,
                         MetaCrtcVirtual              *crtc_virtual,
                         MetaCrtcModeVirtual          *crtc_mode_virtual)
{
  g_autoptr (MetaOutputInfo) output_info = NULL;

  output_info = meta_output_info_new ();
  output_info->name = g_strdup_printf ("Meta-%" G_GUINT64_FORMAT, id);

  output_info->n_possible_crtcs = 1;
  output_info->possible_crtcs = g_new0 (MetaCrtc *, 1);
  output_info->possible_crtcs[0] = META_CRTC (crtc_virtual);

  output_info->hotplug_mode_update = FALSE;
  output_info->suggested_x = -1;
  output_info->suggested_y = -1;

  output_info->connector_type = META_CONNECTOR_TYPE_META;
  output_info->vendor = g_strdup (info->vendor);
  output_info->product = g_strdup (info->product);
  output_info->serial = g_strdup (info->serial);

  output_info->n_modes = 1;
  output_info->modes = g_new0 (MetaCrtcMode *, 1);
  output_info->modes[0] = META_CRTC_MODE (crtc_mode_virtual);
  output_info->preferred_mode = output_info->modes[0];

  return g_object_new (META_TYPE_OUTPUT_VIRTUAL,
                       "id", META_OUTPUT_VIRTUAL_ID_BIT | id,
                       "info", output_info,
                       NULL);
}

static void
meta_output_virtual_init (MetaOutputVirtual *output_virtual)
{
}

static void
meta_output_virtual_class_init (MetaOutputVirtualClass *klass)
{
}
