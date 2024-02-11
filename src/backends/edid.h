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
 * see <http://www.gnu.org/licenses/>.
 *
 * Author: Soren Sandmann <sandmann@redhat.com>
 */

#pragma once

#include <stdint.h>

#include "core/util-private.h"

typedef struct _MetaEdidInfo MetaEdidInfo;
typedef struct _MetaEdidHdrStaticMetadata MetaEdidHdrStaticMetadata;

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
  META_EDID_STATIC_METADATA_TYPE1 = (1 << 0),
} MetaEdidStaticMetadataType;

struct _MetaEdidHdrStaticMetadata
{
  float max_luminance;
  float min_luminance;
  float max_fal;
  MetaEdidTransferFunction tf;
  MetaEdidStaticMetadataType sm;
};

struct _MetaEdidInfo
{
  char *manufacturer_code;
  int product_code;
  unsigned int serial_number;

  double gamma;                         /* -1.0 if not specified */

  double red_x;
  double red_y;
  double green_x;
  double green_y;
  double blue_x;
  double blue_y;
  double white_x;
  double white_y;

  /* Optional product description */
  char *dsc_serial_number;
  char *dsc_product_name;

  int32_t min_vert_rate_hz;

  MetaEdidColorimetry colorimetry;
  MetaEdidHdrStaticMetadata hdr_static_metadata;
};

META_EXPORT_TEST
MetaEdidInfo *meta_edid_info_new_parse (const uint8_t *edid,
                                        size_t size);
