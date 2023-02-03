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

#include "backends/edid.h"

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
  /* Manufacturer Code */
  info->manufacturer_code[0] = get_bits (edid[0x08], 2, 6);
  info->manufacturer_code[1] = get_bits (edid[0x08], 0, 1) << 3;
  info->manufacturer_code[1] |= get_bits (edid[0x09], 5, 7);
  info->manufacturer_code[2] = get_bits (edid[0x09], 0, 4);
  info->manufacturer_code[3] = '\0';

  info->manufacturer_code[0] += 'A' - 1;
  info->manufacturer_code[1] += 'A' - 1;
  info->manufacturer_code[2] += 'A' - 1;

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
                  int            n_chars,
                  char          *result)
{
  int i;
  for (i = 0; i < n_chars; ++i)
    {
      if (s[i] == 0x0a)
	{
          *result++ = '\0';
          break;
	}
      else if (s[i] == 0x00)
	{
          /* Convert embedded 0's to spaces */
          *result++ = ' ';
	}
      else
	{
          *result++ = s[i];
	}
    }
}

static void
decode_display_descriptor (const uint8_t *desc,
                           MetaEdidInfo  *info)
{
  switch (desc[0x03])
    {
    case 0xFC:
      decode_lf_string (desc + 5, 13, info->dsc_product_name);
      break;
    case 0xFF:
      decode_lf_string (desc + 5, 13, info->dsc_serial_number);
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

static gboolean
decode_ext_cta_hdr_static_metadata (const uint8_t *data_block,
                                    MetaEdidInfo  *info)
{
  /* CTA-861-H: Table 92 - HDR Static Metadata Data Block (HDR SMDB) */
  int size;

  info->hdr_static_metadata.available = TRUE;
  info->hdr_static_metadata.tf = data_block[2];
  info->hdr_static_metadata.sm = data_block[3];

  size = get_bits (data_block[0], 0, 5);
  if (size > 3)
    info->hdr_static_metadata.max_luminance = data_block[4];
  if (size > 4)
    info->hdr_static_metadata.max_fal = data_block[5];
  if (size > 5)
    info->hdr_static_metadata.min_luminance = data_block[6];

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

MetaEdidInfo *
meta_edid_info_new_parse (const uint8_t *edid)
{
  MetaEdidInfo *info;

  info = g_new0 (MetaEdidInfo, 1);

  if (decode_header (edid)
      && decode_vendor_and_product_identification (edid, info)
      && decode_display_parameters (edid, info)
      && decode_color_characteristics (edid, info)
      && decode_descriptors (edid, info)
      && decode_extensions (edid, info))
    {
      return info;
    }
  else
    {
      g_free (info);
      return NULL;
    }
}
