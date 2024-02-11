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

#ifdef HAVE_LIBDISPLAY_INFO
#include <libdisplay-info/cta.h>
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
#endif

#include "backends/edid.h"

#ifdef HAVE_LIBDISPLAY_INFO
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

  return TRUE;
}

#else /* HAVE_LIBDISPLAY_INFO */

/* VESA E-EDID */
#define EDID_BLOCK_LENGTH   128
#define EDID_EXT_FLAG_ADDR  0x7E
#define EDID_EXT_TAG_ADDR   0x00

/* VESA reserved IDs for extension blocks */
#define EDID_EXT_ID_CTA     0x02

/* CTA-861 extension block */
#define EDID_EXT_CTA_REVISION_ADDR                        0x01
#define EDID_EXT_CTA_DESCRIPTOR_OFFSET_ADDR               0x02
#define EDID_EXT_CTA_DATA_BLOCK_OFFSET                    0x04
#define EDID_EXT_CTA_TAG_EXTENDED                         0x07
#define EDID_EXT_CTA_TAG_EXTENDED_COLORIMETRY             0x0705
#define EDID_EXT_CTA_TAG_EXTENDED_HDR_STATIC_METADATA     0x0706

static int
get_bit (int in, int bit)
{
  return (in & (1 << bit)) >> bit;
}

static int
get_bits (int in, int begin, int end)
{
  int mask = (1 << (end - begin + 1)) - 1;

  return (in >> begin) & mask;
}

static gboolean
decode_header (const uint8_t *edid)
{
  if (memcmp (edid, "\x00\xff\xff\xff\xff\xff\xff\x00", 8) == 0)
    return TRUE;
  return FALSE;
}

static gboolean
decode_vendor_and_product_identification (const uint8_t *edid,
                                          MetaEdidInfo  *info)
{
  char manufacturer_code[4];

  /* Manufacturer Code */
  manufacturer_code[0] = get_bits (edid[0x08], 2, 6);
  manufacturer_code[1] = get_bits (edid[0x08], 0, 1) << 3;
  manufacturer_code[1] |= get_bits (edid[0x09], 5, 7);
  manufacturer_code[2] = get_bits (edid[0x09], 0, 4);
  manufacturer_code[3] = '\0';

  manufacturer_code[0] += 'A' - 1;
  manufacturer_code[1] += 'A' - 1;
  manufacturer_code[2] += 'A' - 1;

  info->manufacturer_code = g_strdup (manufacturer_code);

  /* Product Code */
  info->product_code = edid[0x0b] << 8 | edid[0x0a];

  /* Serial Number */
  info->serial_number =
    edid[0x0c] | edid[0x0d] << 8 | edid[0x0e] << 16 | edid[0x0f] << 24;
  return TRUE;
}

static gboolean
decode_display_parameters (const uint8_t *edid,
                           MetaEdidInfo  *info)
{
  /* Gamma */
  if (edid[0x17] == 0xFF)
    info->gamma = -1.0;
  else
    info->gamma = (edid[0x17] + 100.0) / 100.0;

  return TRUE;
}

static double
decode_fraction (int high, int low)
{
  double result = 0.0;
  int i;

  high = (high << 2) | low;

  for (i = 0; i < 10; ++i)
    result += get_bit (high, i) * pow (2, i - 10);

  return result;
}

static gboolean
decode_color_characteristics (const uint8_t *edid,
                              MetaEdidInfo  *info)
{
  info->red_x = decode_fraction (edid[0x1b], get_bits (edid[0x19], 6, 7));
  info->red_y = decode_fraction (edid[0x1c], get_bits (edid[0x19], 5, 4));
  info->green_x = decode_fraction (edid[0x1d], get_bits (edid[0x19], 2, 3));
  info->green_y = decode_fraction (edid[0x1e], get_bits (edid[0x19], 0, 1));
  info->blue_x = decode_fraction (edid[0x1f], get_bits (edid[0x1a], 6, 7));
  info->blue_y = decode_fraction (edid[0x20], get_bits (edid[0x1a], 4, 5));
  info->white_x = decode_fraction (edid[0x21], get_bits (edid[0x1a], 2, 3));
  info->white_y = decode_fraction (edid[0x22], get_bits (edid[0x1a], 0, 1));

  return TRUE;
}

static void
decode_lf_string (const uint8_t *s,
                  char          **result)
{
  int i;
  char decoded[14] = { 0 };

  for (i = 0; i < 13; ++i)
    {
      if (s[i] == 0x0a)
        {
          decoded[i] = '\0';
          break;
        }
      else if (s[i] == 0x00)
        {
          /* Convert embedded 0's to spaces */
          decoded[i] = ' ';
        }
      else
        {
          decoded[i] = s[i];
        }
    }

  *result = g_strdup (decoded);
}

static void
decode_display_descriptor (const uint8_t *desc,
                           MetaEdidInfo  *info)
{
  switch (desc[0x03])
    {
    case 0xFC:
      decode_lf_string (desc + 5, &info->dsc_product_name);
      break;
    case 0xFF:
      decode_lf_string (desc + 5, &info->dsc_serial_number);
      break;
    }
}


static gboolean
decode_descriptors (const uint8_t *edid,
                    MetaEdidInfo  *info)
{
  int i;

  for (i = 0; i < 4; ++i)
    {
      int index = 0x36 + i * 18;

      if (edid[index + 0] == 0x00 && edid[index + 1] == 0x00)
        {
          decode_display_descriptor (edid + index, info);
        }
    }

  return TRUE;
}

static gboolean
decode_ext_cta_colorimetry (const uint8_t *data_block,
                            MetaEdidInfo  *info)
{
  /* CTA-861-H: Table 78 - Colorimetry Data Block (CDB) */
  info->colorimetry = (data_block[3] << 8) + data_block[2];
  return TRUE;
}

static float
decode_max_luminance (uint8_t raw)
{
  if (raw == 0)
    return 0.f;

  return 50 * powf (2, (float) raw / 32);
}

static float
decode_min_luminance (uint8_t raw,
                      float   max)
{
  if (raw == 0)
    return 0.f;

  return max * powf ((float) raw / 255, 2) / 100;
}

static gboolean
decode_ext_cta_hdr_static_metadata (const uint8_t *data_block,
                                    MetaEdidInfo  *info)
{
  /* CTA-861-H: Table 92 - HDR Static Metadata Data Block (HDR SMDB) */
  int size;

  info->hdr_static_metadata.tf = data_block[2];
  info->hdr_static_metadata.sm = data_block[3];

  size = get_bits (data_block[0], 0, 5);
  if (size > 3)
    {
      info->hdr_static_metadata.max_luminance =
        decode_max_luminance (data_block[4]);
    }
  if (size > 4)
    {
      info->hdr_static_metadata.max_fal = decode_max_luminance (data_block[5]);
    }
  if (size > 5)
    {
      info->hdr_static_metadata.min_luminance =
        decode_min_luminance (data_block[6],
                              info->hdr_static_metadata.max_luminance);
    }

  return TRUE;
}

static gboolean
decode_ext_cta (const uint8_t *cta_block,
                MetaEdidInfo  *info)
{
  const uint8_t *data_block;
  uint8_t data_block_end;
  uint8_t data_block_offset;
  int size;
  int tag;

  /* The CTA extension block is a number of data blocks followed by a number
   * of (timing) descriptors. We only parse the data blocks. */

  /* CTA-861-H Table 58: CTA Extension Version 3 */
  data_block_end = cta_block[EDID_EXT_CTA_DESCRIPTOR_OFFSET_ADDR];
  data_block_offset = EDID_EXT_CTA_DATA_BLOCK_OFFSET;

  /* Table 58:
   * If d=0, then no detailed timing descriptors are provided, and no data is
   * provided in the data block collection */
  if (data_block_end == 0)
    return TRUE;

  /* Table 58:
   * If no data is provided in the data block collection, then d=4 */
  if (data_block_end == 4)
    return TRUE;

  if (data_block_end < 4)
    return FALSE;

  while (data_block_offset < data_block_end)
    {
      /* CTA-861-H 7.4: CTA Data Block Collection */
      data_block = cta_block + data_block_offset;
      size = get_bits (data_block[0], 0, 4) + 1;
      tag = get_bits (data_block[0], 5, 7);

      data_block_offset += size;

      /* CTA Data Block extended tag type is the second byte */
      if (tag == EDID_EXT_CTA_TAG_EXTENDED)
        tag = (tag << 8) + data_block[1];

      switch (tag)
        {
        case EDID_EXT_CTA_TAG_EXTENDED_COLORIMETRY:
          if (!decode_ext_cta_colorimetry (data_block, info))
            return FALSE;
          break;
        case EDID_EXT_CTA_TAG_EXTENDED_HDR_STATIC_METADATA:
          if (!decode_ext_cta_hdr_static_metadata (data_block, info))
            return FALSE;
          break;
        }
    }

  return TRUE;
}

static gboolean
decode_extensions (const uint8_t *edid,
                   MetaEdidInfo  *info)
{
  int blocks;
  int i;
  const uint8_t *block = NULL;

  blocks = edid[EDID_EXT_FLAG_ADDR];

  for (i = 0; i < blocks; i++)
    {
      block = edid + EDID_BLOCK_LENGTH * (i + 1);

      switch (block[EDID_EXT_TAG_ADDR])
        {
        case EDID_EXT_ID_CTA:
          if (!decode_ext_cta (block, info))
            return FALSE;
          break;
        }
    }

  return TRUE;
}

static gboolean
decode_edid_info (const uint8_t *edid,
                  MetaEdidInfo  *info,
                  size_t         size)
{
  return decode_header (edid) &&
         decode_vendor_and_product_identification (edid, info) &&
         decode_display_parameters (edid, info) &&
         decode_color_characteristics (edid, info) &&
         decode_descriptors (edid, info) &&
         decode_extensions (edid, info);
}
#endif /* HAVE_LIBDISPLAY_INFO */

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
      g_free (info);
      return NULL;
    }
}
