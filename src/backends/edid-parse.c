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

static void
decode_check_sum (const uint8_t *edid,
                  MetaEdidInfo  *info)
{
  int i;
  uint8_t check = 0;

  for (i = 0; i < 128; ++i)
    check += edid[i];

  info->checksum = check;
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
  int is_model_year;

  /* Manufacturer Code */
  info->manufacturer_code[0]  = get_bits (edid[0x08], 2, 6);
  info->manufacturer_code[1]  = get_bits (edid[0x08], 0, 1) << 3;
  info->manufacturer_code[1] |= get_bits (edid[0x09], 5, 7);
  info->manufacturer_code[2]  = get_bits (edid[0x09], 0, 4);
  info->manufacturer_code[3]  = '\0';

  info->manufacturer_code[0] += 'A' - 1;
  info->manufacturer_code[1] += 'A' - 1;
  info->manufacturer_code[2] += 'A' - 1;

  /* Product Code */
  info->product_code = edid[0x0b] << 8 | edid[0x0a];

  /* Serial Number */
  info->serial_number =
    edid[0x0c] | edid[0x0d] << 8 | edid[0x0e] << 16 | edid[0x0f] << 24;

  /* Week and Year */
  is_model_year = FALSE;
  switch (edid[0x10])
    {
    case 0x00:
      info->production_week = -1;
      break;

    case 0xff:
      info->production_week = -1;
      is_model_year = TRUE;
      break;

    default:
      info->production_week = edid[0x10];
      break;
    }

  if (is_model_year)
    {
      info->production_year = -1;
      info->model_year = 1990 + edid[0x11];
    }
  else
    {
      info->production_year = 1990 + edid[0x11];
      info->model_year = -1;
    }

  return TRUE;
}

static gboolean
decode_edid_version (const uint8_t *edid,
                     MetaEdidInfo  *info)
{
  info->major_version = edid[0x12];
  info->minor_version = edid[0x13];

  return TRUE;
}

static gboolean
decode_display_parameters (const uint8_t *edid,
                           MetaEdidInfo  *info)
{
  /* Digital vs Analog */
  info->is_digital = get_bit (edid[0x14], 7);

  if (info->is_digital)
    {
      int bits;

      static const int bit_depth[8] =
        {
          -1, 6, 8, 10, 12, 14, 16, -1
        };

      static const MetaEdidInterface interfaces[6] =
        {
          META_EDID_INTERFACE_UNDEFINED,
          META_EDID_INTERFACE_DVI,
          META_EDID_INTERFACE_HDMI_A,
          META_EDID_INTERFACE_HDMI_B,
          META_EDID_INTERFACE_MDDI,
          META_EDID_INTERFACE_DISPLAY_PORT
        };

      bits = get_bits (edid[0x14], 4, 6);
      info->connector.digital.bits_per_primary = bit_depth[bits];

      bits = get_bits (edid[0x14], 0, 3);

      if (bits <= 5)
        info->connector.digital.interface = interfaces[bits];
      else
        info->connector.digital.interface = META_EDID_INTERFACE_UNDEFINED;
    }
  else
    {
      int bits = get_bits (edid[0x14], 5, 6);

      static const double levels[][3] =
        {
          { 0.7,   0.3,    1.0 },
          { 0.714, 0.286,  1.0 },
          { 1.0,   0.4,    1.4 },
          { 0.7,   0.0,    0.7 },
        };

      info->connector.analog.video_signal_level = levels[bits][0];
      info->connector.analog.sync_signal_level = levels[bits][1];
      info->connector.analog.total_signal_level = levels[bits][2];

      info->connector.analog.blank_to_black = get_bit (edid[0x14], 4);

      info->connector.analog.separate_hv_sync = get_bit (edid[0x14], 3);
      info->connector.analog.composite_sync_on_h = get_bit (edid[0x14], 2);
      info->connector.analog.composite_sync_on_green = get_bit (edid[0x14], 1);

      info->connector.analog.serration_on_vsync = get_bit (edid[0x14], 0);
    }

  /* Screen Size / Aspect Ratio */
  if (edid[0x15] == 0 && edid[0x16] == 0)
    {
      info->width_mm = -1;
      info->height_mm = -1;
      info->aspect_ratio = -1.0;
    }
  else if (edid[0x16] == 0)
    {
      info->width_mm = -1;
      info->height_mm = -1;
      info->aspect_ratio = 100.0 / (edid[0x15] + 99);
    }
  else if (edid[0x15] == 0)
    {
      info->width_mm = -1;
      info->height_mm = -1;
      info->aspect_ratio = 100.0 / (edid[0x16] + 99);
      info->aspect_ratio = 1/info->aspect_ratio; /* portrait */
    }
  else
    {
      info->width_mm = 10 * edid[0x15];
      info->height_mm = 10 * edid[0x16];
    }

  /* Gamma */
  if (edid[0x17] == 0xFF)
    info->gamma = -1.0;
  else
    info->gamma = (edid[0x17] + 100.0) / 100.0;

  /* Features */
  info->standby = get_bit (edid[0x18], 7);
  info->suspend = get_bit (edid[0x18], 6);
  info->active_off = get_bit (edid[0x18], 5);

  if (info->is_digital)
    {
      info->connector.digital.rgb444 = TRUE;
      if (get_bit (edid[0x18], 3))
        info->connector.digital.ycrcb444 = 1;
      if (get_bit (edid[0x18], 4))
        info->connector.digital.ycrcb422 = 1;
    }
  else
    {
      int bits = get_bits (edid[0x18], 3, 4);
      MetaEdidColorType color_type[4] =
        {
          META_EDID_COLOR_TYPE_MONOCHROME,
          META_EDID_COLOR_TYPE_RGB,
          META_EDID_COLOR_TYPE_OTHER_COLOR,
          META_EDID_COLOR_TYPE_UNDEFINED
        };

      info->connector.analog.color_type = color_type[bits];
    }

  info->srgb_is_standard = get_bit (edid[0x18], 2);

  /* In 1.3 this is called "has preferred timing" */
  info->preferred_timing_includes_native = get_bit (edid[0x18], 1);

  /* FIXME: In 1.3 this indicates whether the monitor accepts GTF */
  info->continuous_frequency = get_bit (edid[0x18], 0);
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

static int
decode_established_timings (const uint8_t *edid,
                            MetaEdidInfo  *info)
{
  static const MetaEdidTiming established[][8] =
    {
      {
        { 800, 600, 60 },
        { 800, 600, 56 },
        { 640, 480, 75 },
        { 640, 480, 72 },
        { 640, 480, 67 },
        { 640, 480, 60 },
        { 720, 400, 88 },
        { 720, 400, 70 }
      },
      {
        { 1280, 1024, 75 },
        { 1024, 768, 75 },
        { 1024, 768, 70 },
        { 1024, 768, 60 },
        { 1024, 768, 87 },
        { 832, 624, 75 },
        { 800, 600, 75 },
        { 800, 600, 72 }
	},
      {
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 1152, 870, 75 }
      },
    };

  int i, j, idx;

  idx = 0;
  for (i = 0; i < 3; ++i)
    {
      for (j = 0; j < 8; ++j)
	{
          int byte = edid[0x23 + i];

          if (get_bit (byte, j) && established[i][j].frequency != 0)
            info->established[idx++] = established[i][j];
	}
    }
  return TRUE;
}

static gboolean
decode_standard_timings (const uint8_t *edid,
                         MetaEdidInfo  *info)
{
  int i;

  for (i = 0; i < 8; i++)
    {
      int first = edid[0x26 + 2 * i];
      int second = edid[0x27 + 2 * i];

      if (first != 0x01 && second != 0x01)
	{
          int w = 8 * (first + 31);
          int h = 0;

          switch (get_bits (second, 6, 7))
            {
	    case 0x00: h = (w / 16) * 10; break;
	    case 0x01: h = (w / 4) * 3; break;
	    case 0x02: h = (w / 5) * 4; break;
	    case 0x03: h = (w / 16) * 9; break;
	    }

          info->standard[i].width = w;
          info->standard[i].height = h;
          info->standard[i].frequency = get_bits (second, 0, 5) + 60;
	}
    }

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
    case 0xFE:
      decode_lf_string (desc + 5, 13, info->dsc_string);
      break;
    case 0xFD:
      /* Range Limits */
      break;
    case 0xFB:
      /* Color Point */
      break;
    case 0xFA:
      /* Timing Identifications */
      break;
    case 0xF9:
      /* Color Management */
      break;
    case 0xF8:
      /* Timing Codes */
      break;
    case 0xF7:
      /* Established Timings */
      break;
    case 0x10:
      break;
    }
}

static void
decode_detailed_timing (const uint8_t          *timing,
			MetaEdidDetailedTiming *detailed)
{
  int bits;
  MetaEdidStereoType stereo[] =
    {
      META_EDID_STEREO_TYPE_NO_STEREO,
      META_EDID_STEREO_TYPE_NO_STEREO,
      META_EDID_STEREO_TYPE_FIELD_RIGHT,
      META_EDID_STEREO_TYPE_FIELD_LEFT,

      META_EDID_STEREO_TYPE_TWO_WAY_RIGHT_ON_EVEN,
      META_EDID_STEREO_TYPE_TWO_WAY_LEFT_ON_EVEN,

      META_EDID_STEREO_TYPE_FOUR_WAY_INTERLEAVED,
      META_EDID_STEREO_TYPE_SIDE_BY_SIDE
    };

  detailed->pixel_clock = (timing[0x00] | timing[0x01] << 8) * 10000;
  detailed->h_addr = timing[0x02] | ((timing[0x04] & 0xf0) << 4);
  detailed->h_blank = timing[0x03] | ((timing[0x04] & 0x0f) << 8);
  detailed->v_addr = timing[0x05] | ((timing[0x07] & 0xf0) << 4);
  detailed->v_blank = timing[0x06] | ((timing[0x07] & 0x0f) << 8);
  detailed->h_front_porch = timing[0x08] | get_bits (timing[0x0b], 6, 7) << 8;
  detailed->h_sync = timing[0x09] | get_bits (timing[0x0b], 4, 5) << 8;
  detailed->v_front_porch =
    get_bits (timing[0x0a], 4, 7) | get_bits (timing[0x0b], 2, 3) << 4;
  detailed->v_sync =
    get_bits (timing[0x0a], 0, 3) | get_bits (timing[0x0b], 0, 1) << 4;
  detailed->width_mm =  timing[0x0c] | get_bits (timing[0x0e], 4, 7) << 8;
  detailed->height_mm = timing[0x0d] | get_bits (timing[0x0e], 0, 3) << 8;
  detailed->right_border = timing[0x0f];
  detailed->top_border = timing[0x10];

  detailed->interlaced = get_bit (timing[0x11], 7);

  /* Stereo */
  bits = get_bits (timing[0x11], 5, 6) << 1 | get_bit (timing[0x11], 0);
  detailed->stereo = stereo[bits];

  /* Sync */
  bits = timing[0x11];

  detailed->digital_sync = get_bit (bits, 4);
  if (detailed->digital_sync)
    {
      detailed->connector.digital.composite = !get_bit (bits, 3);

      if (detailed->connector.digital.composite)
	{
          detailed->connector.digital.serrations = get_bit (bits, 2);
          detailed->connector.digital.negative_vsync = FALSE;
	}
      else
	{
          detailed->connector.digital.serrations = FALSE;
          detailed->connector.digital.negative_vsync = !get_bit (bits, 2);
	}

      detailed->connector.digital.negative_hsync = !get_bit (bits, 0);
    }
  else
    {
      detailed->connector.analog.bipolar = get_bit (bits, 3);
      detailed->connector.analog.serrations = get_bit (bits, 2);
      detailed->connector.analog.sync_on_green = !get_bit (bits, 1);
    }
}

static gboolean
decode_descriptors (const uint8_t *edid,
                    MetaEdidInfo  *info)
{
  int i;
  int timing_idx;

  timing_idx = 0;

  for (i = 0; i < 4; ++i)
    {
      int index = 0x36 + i * 18;

      if (edid[index + 0] == 0x00 && edid[index + 1] == 0x00)
	{
          decode_display_descriptor (edid + index, info);
	}
      else
	{
          decode_detailed_timing (edid + index, &(info->detailed_timings[timing_idx++]));
	}
    }

  info->n_detailed_timings = timing_idx;

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

  decode_check_sum (edid, info);

  if (decode_header (edid)
      && decode_vendor_and_product_identification (edid, info)
      && decode_edid_version (edid, info)
      && decode_display_parameters (edid, info)
      && decode_color_characteristics (edid, info)
      && decode_established_timings (edid, info)
      && decode_standard_timings (edid, info)
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
