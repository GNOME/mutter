/* edid.h
 *
 * Copyright 2007, 2008, Red Hat, Inc.
 *
 * This file is part of the Gnome Library.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Soren Sandmann <sandmann@redhat.com>
 */

#ifndef EDID_H
#define EDID_H

#include <stdint.h>

#include "core/util-private.h"

typedef struct _MetaEdidInfo MetaEdidInfo;
typedef struct _MetaEdidTiming MetaEdidTiming;
typedef struct _MetaEdidDetailedTiming MetaEdidDetailedTiming;
typedef struct _MetaEdidHdrStaticMetadata MetaEdidHdrStaticMetadata;

typedef enum
{
  META_EDID_INTERFACE_UNDEFINED,
  META_EDID_INTERFACE_DVI,
  META_EDID_INTERFACE_HDMI_A,
  META_EDID_INTERFACE_HDMI_B,
  META_EDID_INTERFACE_MDDI,
  META_EDID_INTERFACE_DISPLAY_PORT
} MetaEdidInterface;

typedef enum
{
  META_EDID_COLOR_TYPE_UNDEFINED,
  META_EDID_COLOR_TYPE_MONOCHROME,
  META_EDID_COLOR_TYPE_RGB,
  META_EDID_COLOR_TYPE_OTHER_COLOR
} MetaEdidColorType;

typedef enum
{
  META_EDID_STEREO_TYPE_NO_STEREO,
  META_EDID_STEREO_TYPE_FIELD_RIGHT,
  META_EDID_STEREO_TYPE_FIELD_LEFT,
  META_EDID_STEREO_TYPE_TWO_WAY_RIGHT_ON_EVEN,
  META_EDID_STEREO_TYPE_TWO_WAY_LEFT_ON_EVEN,
  META_EDID_STEREO_TYPE_FOUR_WAY_INTERLEAVED,
  META_EDID_STEREO_TYPE_SIDE_BY_SIDE
} MetaEdidStereoType;

typedef enum
{
  META_EDID_COLORIMETRY_XVYCC601    = (1 << 0),
  META_EDID_COLORIMETRY_XVYCC709    = (1 << 1),
  META_EDID_COLORIMETRY_SYCC601     = (1 << 2),
  META_EDID_COLORIMETRY_OPYCC601    = (1 << 3),
  META_EDID_COLORIMETRY_OPRGB       = (1 << 4),
  META_EDID_COLORIMETRY_BT2020CYCC  = (1 << 5),
  META_EDID_COLORIMETRY_BT2020YCC   = (1 << 6),
  META_EDID_COLORIMETRY_BT2020RGB   = (1 << 7),
  META_EDID_COLORIMETRY_ST2113RGB   = (1 << 14),
  META_EDID_COLORIMETRY_ICTCP       = (1 << 15),
} MetaEdidColorimetry;

typedef enum
{
  META_EDID_TF_TRADITIONAL_GAMMA_SDR = (1 << 0),
  META_EDID_TF_TRADITIONAL_GAMMA_HDR = (1 << 1),
  META_EDID_TF_PQ                    = (1 << 2),
  META_EDID_TF_HLG                   = (1 << 3),
} MetaEdidTransferFunction;

typedef enum
{
  META_EDID_STATIC_METADATA_TYPE1 = 0,
} MetaEdidStaticMetadataType;

struct _MetaEdidTiming
{
  int width;
  int height;
  int frequency;
};

struct _MetaEdidDetailedTiming
{
  int		pixel_clock;
  int		h_addr;
  int		h_blank;
  int		h_sync;
  int		h_front_porch;
  int		v_addr;
  int		v_blank;
  int		v_sync;
  int		v_front_porch;
  int		width_mm;
  int		height_mm;
  int		right_border;
  int		top_border;
  int		interlaced;
  MetaEdidStereoType stereo;

  int		digital_sync;
  union
  {
    struct
    {
      int bipolar;
      int serrations;
      int sync_on_green;
    } analog;

    struct
    {
      int composite;
      int serrations;
      int negative_vsync;
      int negative_hsync;
    } digital;
  } connector;
};

struct _MetaEdidHdrStaticMetadata
{
  int available;
  int max_luminance;
  int min_luminance;
  int max_fal;
  MetaEdidTransferFunction tf;
  MetaEdidStaticMetadataType sm;
};

struct _MetaEdidInfo
{
  int		checksum;
  char		manufacturer_code[4];
  int		product_code;
  unsigned int	serial_number;

  int		production_week;	/* -1 if not specified */
  int		production_year;	/* -1 if not specified */
  int		model_year;		/* -1 if not specified */

  int		major_version;
  int		minor_version;

  int		is_digital;

  union
  {
    struct
    {
      int	bits_per_primary;
      MetaEdidInterface interface;
      int	rgb444;
      int	ycrcb444;
      int	ycrcb422;
    } digital;

    struct
    {
      double	video_signal_level;
      double	sync_signal_level;
      double	total_signal_level;

      int	blank_to_black;

      int	separate_hv_sync;
      int	composite_sync_on_h;
      int	composite_sync_on_green;
      int	serration_on_vsync;
      MetaEdidColorType color_type;
    } analog;
  } connector;

  int		width_mm;		/* -1 if not specified */
  int		height_mm;		/* -1 if not specified */
  double	aspect_ratio;		/* -1.0 if not specififed */

  double	gamma;			/* -1.0 if not specified */

  int		standby;
  int		suspend;
  int		active_off;

  int		srgb_is_standard;
  int		preferred_timing_includes_native;
  int		continuous_frequency;

  double	red_x;
  double	red_y;
  double	green_x;
  double	green_y;
  double	blue_x;
  double	blue_y;
  double	white_x;
  double	white_y;

  MetaEdidTiming established[24];	/* Terminated by 0x0x0 */
  MetaEdidTiming standard[8];

  int		n_detailed_timings;
  MetaEdidDetailedTiming detailed_timings[4];	/* If monitor has a preferred
                                                 * mode, it is the first one
                                                 * (whether it has, is
                                                 * determined by the
                                                 * preferred_timing_includes
                                                 * bit.
                                                 */

  /* Optional product description */
  char		dsc_serial_number[14];
  char		dsc_product_name[14];
  char		dsc_string[14];		/* Unspecified ASCII data */

  MetaEdidColorimetry colorimetry;
  MetaEdidHdrStaticMetadata hdr_static_metadata;
};

META_EXPORT_TEST
MetaEdidInfo *meta_edid_info_new_parse (const uint8_t *data);

#endif
