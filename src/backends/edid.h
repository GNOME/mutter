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
#include <libdisplay-info/info.h>

#include "core/util-private.h"

typedef struct _MetaEdidInfo
{
  char *manufacturer_code;
  int product_code;
  unsigned int serial_number;

  /* Optional product description */
  char *dsc_serial_number;
  char *dsc_product_name;

  struct di_color_primaries default_color_primaries;
  double default_gamma; /* -1.0 if not specified FIXME, now 0 */

  int32_t min_vert_rate_hz;

  struct di_supported_signal_colorimetry colorimetry;
  struct di_hdr_static_metadata hdr_static_metadata;
} MetaEdidInfo;

META_EXPORT_TEST
MetaEdidInfo *meta_edid_info_new_parse (const uint8_t *edid,
                                        size_t size);

META_EXPORT_TEST
void meta_edid_info_free (MetaEdidInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaEdidInfo, meta_edid_info_free)
