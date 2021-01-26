/*
 * Copyright (C) 2017 Red Hat
 * Copyright (C) 2020 NVIDIA CORPORATION
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

#ifndef META_OUTPUT_H
#define META_OUTPUT_H

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-gpu.h"
#include "core/util-private.h"

struct _MetaTileInfo
{
  uint32_t group_id;
  uint32_t flags;
  uint32_t max_h_tiles;
  uint32_t max_v_tiles;
  uint32_t loc_h_tile;
  uint32_t loc_v_tile;
  uint32_t tile_w;
  uint32_t tile_h;
};

/* The first 17 matches the values in drm_mode.h, the ones starting with
 * 1000 do not. */
typedef enum
{
  META_CONNECTOR_TYPE_Unknown = 0,
  META_CONNECTOR_TYPE_VGA = 1,
  META_CONNECTOR_TYPE_DVII = 2,
  META_CONNECTOR_TYPE_DVID = 3,
  META_CONNECTOR_TYPE_DVIA = 4,
  META_CONNECTOR_TYPE_Composite = 5,
  META_CONNECTOR_TYPE_SVIDEO = 6,
  META_CONNECTOR_TYPE_LVDS = 7,
  META_CONNECTOR_TYPE_Component = 8,
  META_CONNECTOR_TYPE_9PinDIN = 9,
  META_CONNECTOR_TYPE_DisplayPort = 10,
  META_CONNECTOR_TYPE_HDMIA = 11,
  META_CONNECTOR_TYPE_HDMIB = 12,
  META_CONNECTOR_TYPE_TV = 13,
  META_CONNECTOR_TYPE_eDP = 14,
  META_CONNECTOR_TYPE_VIRTUAL = 15,
  META_CONNECTOR_TYPE_DSI = 16,

  META_CONNECTOR_TYPE_META = 1000,
} MetaConnectorType;

typedef struct _MetaOutputInfo
{
  grefcount ref_count;

  char *name;
  char *vendor;
  char *product;
  char *serial;
  int width_mm;
  int height_mm;
  CoglSubpixelOrder subpixel_order;

  MetaConnectorType connector_type;
  MetaMonitorTransform panel_orientation_transform;

  MetaCrtcMode *preferred_mode;
  MetaCrtcMode **modes;
  unsigned int n_modes;

  MetaCrtc **possible_crtcs;
  unsigned int n_possible_crtcs;

  MetaOutput **possible_clones;
  unsigned int n_possible_clones;

  int backlight_min;
  int backlight_max;

  gboolean supports_underscanning;
  gboolean supports_color_transform;

  /*
   * Get a new preferred mode on hotplug events, to handle dynamic guest
   * resizing.
   */
  gboolean hotplug_mode_update;
  int suggested_x;
  int suggested_y;

  MetaTileInfo tile_info;
} MetaOutputInfo;

#define META_TYPE_OUTPUT_INFO (meta_output_info_get_type ())
META_EXPORT_TEST
GType meta_output_info_get_type (void);

META_EXPORT_TEST
MetaOutputInfo * meta_output_info_new (void);

META_EXPORT_TEST
MetaOutputInfo * meta_output_info_ref (MetaOutputInfo *output_info);

META_EXPORT_TEST
void meta_output_info_unref (MetaOutputInfo *output_info);

META_EXPORT_TEST
void meta_output_info_parse_edid (MetaOutputInfo *output_info,
                                  GBytes         *edid);

gboolean meta_output_is_laptop  (MetaOutput *output);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaOutputInfo, meta_output_info_unref)

#define META_TYPE_OUTPUT (meta_output_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaOutput, meta_output, META, OUTPUT, GObject)

struct _MetaOutputClass
{
  GObjectClass parent_class;
};

META_EXPORT_TEST
uint64_t meta_output_get_id (MetaOutput *output);

META_EXPORT_TEST
MetaGpu * meta_output_get_gpu (MetaOutput *output);

META_EXPORT_TEST
MetaMonitor * meta_output_get_monitor (MetaOutput *output);

void meta_output_set_monitor (MetaOutput  *output,
                              MetaMonitor *monitor);

void meta_output_unset_monitor (MetaOutput *output);

const char * meta_output_get_name (MetaOutput *output);

META_EXPORT_TEST
gboolean meta_output_is_primary (MetaOutput *output);

META_EXPORT_TEST
gboolean meta_output_is_presentation (MetaOutput *output);

META_EXPORT_TEST
gboolean meta_output_is_underscanning (MetaOutput *output);

void meta_output_set_backlight (MetaOutput *output,
                                int         backlight);

int meta_output_get_backlight (MetaOutput *output);

void meta_output_add_possible_clone (MetaOutput *output,
                                     MetaOutput *possible_clone);

META_EXPORT_TEST
const MetaOutputInfo * meta_output_get_info (MetaOutput *output);

META_EXPORT_TEST
void meta_output_assign_crtc (MetaOutput                 *output,
                              MetaCrtc                   *crtc,
                              const MetaOutputAssignment *output_assignment);

META_EXPORT_TEST
void meta_output_unassign_crtc (MetaOutput *output);

META_EXPORT_TEST
MetaCrtc * meta_output_get_assigned_crtc (MetaOutput *output);

MetaMonitorTransform meta_output_logical_to_crtc_transform (MetaOutput           *output,
                                                            MetaMonitorTransform  transform);

MetaMonitorTransform meta_output_crtc_to_logical_transform (MetaOutput           *output,
                                                            MetaMonitorTransform  transform);

#endif /* META_OUTPUT_H */
