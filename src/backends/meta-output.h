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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "backends/edid.h"
#include "backends/meta-backend-types.h"
#include "backends/meta-backlight-private.h"
#include "backends/meta-connector.h"
#include "backends/meta-gpu.h"
#include "core/util-private.h"

typedef enum _MetaOutputColorspace
{
  META_OUTPUT_COLORSPACE_UNKNOWN = 0,
  META_OUTPUT_COLORSPACE_DEFAULT,
  META_OUTPUT_COLORSPACE_BT2020,
} MetaOutputColorspace;

typedef enum
{
  META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR,
  META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR,
  META_OUTPUT_HDR_METADATA_EOTF_PQ,
  META_OUTPUT_HDR_METADATA_EOTF_HLG,
} MetaOutputHdrMetadataEOTF;

enum _MetaColorMode
{
  META_COLOR_MODE_DEFAULT = 0,
  META_COLOR_MODE_BT2100 = 1,
};

typedef enum _MetaOutputRGBRange
{
  META_OUTPUT_RGB_RANGE_UNKNOWN = 0,
  META_OUTPUT_RGB_RANGE_AUTO,
  META_OUTPUT_RGB_RANGE_FULL,
  META_OUTPUT_RGB_RANGE_LIMITED,
} MetaOutputRGBRange;

typedef enum
{
  META_SUBPIXEL_ORDER_UNKNOWN,
  META_SUBPIXEL_ORDER_NONE,
  META_SUBPIXEL_ORDER_HORIZONTAL_RGB,
  META_SUBPIXEL_ORDER_HORIZONTAL_BGR,
  META_SUBPIXEL_ORDER_VERTICAL_RGB,
  META_SUBPIXEL_ORDER_VERTICAL_BGR,
} MetaSubpixelOrder;

typedef struct _MetaOutputHdrMetadata
{
  gboolean active;
  MetaOutputHdrMetadataEOTF eotf;
  struct {
    double x;
    double y;
  } mastering_display_primaries[3];
  struct {
    double x;
    double y;
  } mastering_display_white_point;
  double mastering_display_max_luminance;
  double mastering_display_min_luminance;
  double max_cll;
  double max_fall;
} MetaOutputHdrMetadata;

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

typedef enum
{
  META_PRIVACY_SCREEN_UNAVAILABLE = 0,
  META_PRIVACY_SCREEN_ENABLED = 1 << 0,
  META_PRIVACY_SCREEN_DISABLED = 1 << 1,
  META_PRIVACY_SCREEN_LOCKED = 1 << 2,
} MetaPrivacyScreenState;

typedef struct _MetaOutputInfo
{
  grefcount ref_count;

  gboolean is_virtual;

  char *name;
  char *vendor;
  char *product;
  char *serial;

  char *edid_checksum_md5;
  MetaEdidInfo *edid_info;

  int width_mm;
  int height_mm;
  MetaSubpixelOrder subpixel_order;

  MetaConnectorType connector_type;
  MtkMonitorTransform panel_orientation_transform;

  MetaCrtcMode *preferred_mode;
  MetaCrtcMode **modes;
  unsigned int n_modes;

  MetaCrtc **possible_crtcs;
  unsigned int n_possible_crtcs;

  MetaOutput **possible_clones;
  unsigned int n_possible_clones;

  gboolean supports_underscanning;
  gboolean supports_color_transform;
  gboolean supports_privacy_screen;

  unsigned int max_bpc_min;
  unsigned int max_bpc_max;

  /*
   * Get a new preferred mode on hotplug events, to handle dynamic guest
   * resizing.
   */
  gboolean hotplug_mode_update;
  int suggested_x;
  int suggested_y;

  MetaTileInfo tile_info;

  uint64_t supported_color_spaces;
  uint64_t supported_hdr_eotfs;

  uint64_t supported_rgb_ranges;

  gboolean supports_vrr;
} MetaOutputInfo;

gboolean meta_tile_info_equal (MetaTileInfo *a,
                               MetaTileInfo *b);

META_EXPORT_TEST
gboolean meta_output_hdr_metadata_equal (MetaOutputHdrMetadata *metadata,
                                         MetaOutputHdrMetadata *other_metadata);

const char * meta_output_colorspace_get_name (MetaOutputColorspace color_space);

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

gboolean meta_output_info_get_min_refresh_rate (const MetaOutputInfo *output_info,
                                                int                  *min_refresh_rate);

gboolean meta_output_info_is_builtin (const MetaOutputInfo *output_info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaOutputInfo, meta_output_info_unref)

#define META_TYPE_OUTPUT (meta_output_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaOutput, meta_output, META, OUTPUT, GObject)

struct _MetaOutputClass
{
  GObjectClass parent_class;

  MetaPrivacyScreenState (* get_privacy_screen_state) (MetaOutput *output);

  MetaBacklight * (* create_backlight) (MetaOutput  *output,
                                        GError     **error);
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

META_EXPORT_TEST
gboolean meta_output_get_max_bpc (MetaOutput   *output,
                                  unsigned int *max_bpc);

META_EXPORT_TEST
MetaBacklight * meta_output_create_backlight (MetaOutput  *output,
                                              GError     **error);

MetaPrivacyScreenState meta_output_get_privacy_screen_state (MetaOutput *output);

gboolean meta_output_is_privacy_screen_enabled (MetaOutput *output);

gboolean meta_output_set_privacy_screen_enabled (MetaOutput  *output,
                                                 gboolean     enabled,
                                                 GError     **error);

void meta_output_get_color_metadata (MetaOutput            *output,
                                     MetaOutputHdrMetadata *hdr_metadata,
                                     MetaOutputColorspace  *colorspace);

MetaColorMode meta_output_get_color_mode (MetaOutput *output);

META_EXPORT_TEST
MetaOutputRGBRange meta_output_peek_rgb_range (MetaOutput *output);

gboolean meta_output_is_vrr_enabled (MetaOutput *output);

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

MtkMonitorTransform meta_output_logical_to_crtc_transform (MetaOutput          *output,
                                                           MtkMonitorTransform  transform);

MtkMonitorTransform meta_output_crtc_to_logical_transform (MetaOutput          *output,
                                                           MtkMonitorTransform  transform);

void meta_output_update_modes (MetaOutput    *output,
                               MetaCrtcMode  *preferred_mode,
                               MetaCrtcMode **modes,
                               int            n_modes);

gboolean meta_output_matches (MetaOutput *output,
                              MetaOutput *other_output);
