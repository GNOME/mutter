/*
 * Copyright 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Author: Soren Sandmann <sandmann@redhat.com> */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>

#include <libdisplay-info/cta.h>
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>

#include "backends/edid.h"

MetaEdidInfo *
meta_edid_info_new_parse (const uint8_t *edid,
                          size_t         size)
{
  g_autofree MetaEdidInfo *info = g_new0 (MetaEdidInfo, 1);
  struct di_info *di_info;
  const struct di_edid *di_edid;
  const struct di_edid_vendor_product *vendor_product;
  const struct di_edid_display_descriptor *const *edid_descriptors;
  const struct di_color_primaries *default_color_primaries;
  const struct di_supported_signal_colorimetry *signal_colorimetry;
  const struct di_hdr_static_metadata *hdr_static_metadata;

  di_info = di_info_parse_edid (edid, size);

  if (!di_info)
    return NULL;

  di_edid = di_info_get_edid (di_info);

  /* Vendor and Product identification */
  vendor_product = di_edid_get_vendor_product (di_edid);

  /* Manufacturer Code */
  info->manufacturer_code = g_strndup (vendor_product->manufacturer, 3);

  /* Product Code */
  info->product_code = vendor_product->product;

  /* Serial Number */
  info->serial_number = vendor_product->serial;

  /* Product Serial and Name */
  edid_descriptors = di_edid_get_display_descriptors (di_edid);
  for (; *edid_descriptors; edid_descriptors++)
    {
      const struct di_edid_display_descriptor *desc = *edid_descriptors;
      enum di_edid_display_descriptor_tag desc_tag =
        di_edid_display_descriptor_get_tag (desc);
      const struct di_edid_display_range_limits *range_limits;

      switch (desc_tag)
        {
        case DI_EDID_DISPLAY_DESCRIPTOR_PRODUCT_SERIAL:
          info->dsc_serial_number =
            g_strdup (di_edid_display_descriptor_get_string (desc));
          break;
        case DI_EDID_DISPLAY_DESCRIPTOR_PRODUCT_NAME:
          info->dsc_product_name =
            g_strdup (di_edid_display_descriptor_get_string (desc));
          break;
        case DI_EDID_DISPLAY_DESCRIPTOR_RANGE_LIMITS:
          range_limits = di_edid_display_descriptor_get_range_limits (desc);
          info->min_vert_rate_hz = range_limits->min_vert_rate_hz;
          break;
        default:
            break;
        }
    }

  /* Default Color Characteristics */
  default_color_primaries = di_info_get_default_color_primaries (di_info);
  memcpy (&info->default_color_primaries,
          default_color_primaries,
          sizeof (*default_color_primaries));

  /* Default Gamma */
  info->default_gamma = di_info_get_default_gamma (di_info);

  /* Supported Signal Colorimetry */
  signal_colorimetry = di_info_get_supported_signal_colorimetry (di_info);
  memcpy (&info->colorimetry,
          signal_colorimetry,
          sizeof (*signal_colorimetry));

  /* Supported HDR Static Metadata */
  hdr_static_metadata = di_info_get_hdr_static_metadata (di_info);
  memcpy (&info->hdr_static_metadata,
          hdr_static_metadata,
          sizeof (*hdr_static_metadata));

  di_info_destroy (di_info);
  return g_steal_pointer (&info);
}

void
meta_edid_info_free (MetaEdidInfo *info)
{
  g_clear_pointer (&info->manufacturer_code, g_free);
  g_clear_pointer (&info->dsc_serial_number, g_free);
  g_clear_pointer (&info->dsc_product_name, g_free);
  g_free (info);
}
