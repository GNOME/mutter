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

static void
decode_edid_descriptors (const struct di_edid                    *di_edid,
                         const struct di_edid_display_descriptor *desc,
                         MetaEdidInfo                            *info)
{
  enum di_edid_display_descriptor_tag desc_tag;
  const struct di_edid_display_range_limits *range_limits;

  desc_tag = di_edid_display_descriptor_get_tag (desc);

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
      g_assert (range_limits != NULL);
      info->min_vert_rate_hz = range_limits->min_vert_rate_hz;
      break;
    default:
        break;
    }
}

static void
decode_edid_colorimetry (const struct di_cta_colorimetry_block *colorimetry,
                         MetaEdidInfo                          *info)
{
  /* Colorimetry Data Block */
  if (colorimetry->xvycc_601)
    info->colorimetry |= META_EDID_COLORIMETRY_XVYCC601;
  if (colorimetry->xvycc_709)
    info->colorimetry |= META_EDID_COLORIMETRY_XVYCC709;
  if (colorimetry->sycc_601)
    info->colorimetry |= META_EDID_COLORIMETRY_SYCC601;
  if (colorimetry->opycc_601)
    info->colorimetry |= META_EDID_COLORIMETRY_OPYCC601;
  if (colorimetry->oprgb)
    info->colorimetry |= META_EDID_COLORIMETRY_OPRGB;
  if (colorimetry->bt2020_cycc)
    info->colorimetry |= META_EDID_COLORIMETRY_BT2020CYCC;
  if (colorimetry->bt2020_ycc)
    info->colorimetry |= META_EDID_COLORIMETRY_BT2020YCC;
  if (colorimetry->bt2020_rgb)
    info->colorimetry |= META_EDID_COLORIMETRY_BT2020RGB;
  if (colorimetry->st2113_rgb)
    info->colorimetry |= META_EDID_COLORIMETRY_ST2113RGB;
  if (colorimetry->ictcp)
    info->colorimetry |= META_EDID_COLORIMETRY_ICTCP;
}

static void
decode_edid_hdr_static_metadata (const struct di_cta_hdr_static_metadata_block *hdr,
                                 MetaEdidInfo                                  *info)
{
  /* HDR Static Metadata Block */
  if (hdr->descriptors->type1)
    info->hdr_static_metadata.sm |= META_EDID_STATIC_METADATA_TYPE1;

  if (hdr->eotfs->traditional_sdr)
    info->hdr_static_metadata.tf |= META_EDID_TF_TRADITIONAL_GAMMA_SDR;
  if (hdr->eotfs->traditional_hdr)
    info->hdr_static_metadata.tf |= META_EDID_TF_TRADITIONAL_GAMMA_HDR;
  if (hdr->eotfs->pq)
    info->hdr_static_metadata.tf |= META_EDID_TF_PQ;
  if (hdr->eotfs->hlg)
    info->hdr_static_metadata.tf |= META_EDID_TF_HLG;

  info->hdr_static_metadata.max_luminance =
    hdr->desired_content_max_luminance;
  info->hdr_static_metadata.max_fal =
    hdr->desired_content_max_frame_avg_luminance;
  info->hdr_static_metadata.min_luminance =
    hdr->desired_content_min_luminance;
}

static void
decode_edid_cta_ext (const struct di_edid_cta *cta,
                     MetaEdidInfo             *info)
{
  const struct di_cta_data_block *const *data_blks;
  const struct di_cta_data_block *data_blk;
  enum di_cta_data_block_tag data_blk_tag;
  const struct di_cta_colorimetry_block *colorimetry;
  const struct di_cta_hdr_static_metadata_block *hdr_static_metadata;
  size_t data_index;

  data_blks = di_edid_cta_get_data_blocks (cta);
  for (data_index = 0; data_blks[data_index] != NULL; data_index++)
    {
      data_blk = data_blks[data_index];
      data_blk_tag = di_cta_data_block_get_tag (data_blk);

      switch (data_blk_tag)
        {
        case DI_CTA_DATA_BLOCK_COLORIMETRY:
          colorimetry = di_cta_data_block_get_colorimetry (data_blk);
          g_assert (colorimetry);
          decode_edid_colorimetry (colorimetry, info);
          break;
        case DI_CTA_DATA_BLOCK_HDR_STATIC_METADATA:
          hdr_static_metadata =
            di_cta_data_block_get_hdr_static_metadata (data_blk);
          g_assert (hdr_static_metadata);
          decode_edid_hdr_static_metadata (hdr_static_metadata, info);
          break;
        default:
          break;
        }
    }
}

static void
decode_edid_extensions (const struct di_edid_ext *ext,
                        MetaEdidInfo             *info)
{
  enum di_edid_ext_tag ext_tag;
  const struct di_edid_cta *cta;
  ext_tag = di_edid_ext_get_tag (ext);

  switch (ext_tag)
    {
    case DI_EDID_EXT_CEA:
      cta = di_edid_ext_get_cta (ext);
      decode_edid_cta_ext (cta, info);
      break;
    default:
      break;
    }
}

static gboolean
decode_edid_info (const uint8_t *edid,
                  MetaEdidInfo  *info,
                  size_t         size)
{
  const struct di_edid *di_edid;
  struct di_info *edid_info;
  const struct di_edid_vendor_product *vendor_product;
  const struct di_edid_chromaticity_coords *chromaticity_coords;
  float gamma;
  const struct di_edid_display_descriptor *const *edid_descriptors;
  const struct di_edid_ext *const *extensions;
  size_t desc_index;
  size_t ext_index;

  edid_info = di_info_parse_edid (edid, size);

  if (!edid_info)
    {
      return FALSE;
    }

  di_edid = di_info_get_edid (edid_info);

  /* Vendor and Product identification */
  vendor_product = di_edid_get_vendor_product (di_edid);

  /* Manufacturer Code */
  info->manufacturer_code = g_strndup (vendor_product->manufacturer, 3);

  /* Product Code */
  info->product_code = vendor_product->product;

  /* Serial Number */
  info->serial_number = vendor_product->serial;

  /* Color Characteristics */
  chromaticity_coords = di_edid_get_chromaticity_coords (di_edid);
  info->red_x = chromaticity_coords->red_x;
  info->red_y = chromaticity_coords->red_y;
  info->green_x = chromaticity_coords->green_x;
  info->green_y = chromaticity_coords->green_y;
  info->blue_x = chromaticity_coords->blue_x;
  info->blue_y = chromaticity_coords->blue_y;
  info->white_x = chromaticity_coords->white_x;
  info->white_y = chromaticity_coords->white_y;

  /* Gamma */
  gamma = di_edid_get_basic_gamma (di_edid);
  if (gamma != 0)
    info->gamma = gamma;
  else
    info->gamma = -1;

  /* Descriptors */
  edid_descriptors = di_edid_get_display_descriptors (di_edid);
  for (desc_index = 0; edid_descriptors[desc_index] != NULL; desc_index++)
    {
      decode_edid_descriptors (di_edid, edid_descriptors[desc_index], info);
    }

  /* Extension Blocks */
  extensions = di_edid_get_extensions (di_edid);

  for (ext_index = 0; extensions[ext_index] != NULL; ext_index++)
    {
      decode_edid_extensions (extensions[ext_index], info);
    }

  di_info_destroy (edid_info);

  return TRUE;
}

MetaEdidInfo *
meta_edid_info_new_parse (const uint8_t *edid,
                          size_t         size)
{
  MetaEdidInfo *info;

  info = g_new0 (MetaEdidInfo, 1);

  if (decode_edid_info (edid, info, size))
    {
      return info;
    }
  else
    {
      meta_edid_info_free (info);
      return NULL;
    }
}

void
meta_edid_info_free (MetaEdidInfo *info)
{
  g_clear_pointer (&info->manufacturer_code, g_free);
  g_clear_pointer (&info->dsc_serial_number, g_free);
  g_clear_pointer (&info->dsc_product_name, g_free);
  g_free (info);
}
