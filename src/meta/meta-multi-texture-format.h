/*
 * Authored By Niels De Graef <niels.degraef@barco.com>
 *
 * Copyright (C) 2018 Barco NV
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

#ifndef __META_MULTI_TEXTURE_FORMAT_H__
#define __META_MULTI_TEXTURE_FORMAT_H__

#include <glib.h>
#include <cogl/cogl.h>
#include <meta/common.h>

G_BEGIN_DECLS

/**
 * MetaMultiTextureFormat:
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_SIMPLE: Any format supported by Cogl (see #CoglPixelFormat)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUYV: YUYV, 32 bits, 16 bpc (Y), 8 bpc (U & V)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVYU: YVYU, 32 bits, 16 bpc (Y), 8 bpc (V & U)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_UYVY: UYVY, 32 bits, 16 bpc (Y), 8 bpc (V & U)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_VYUY: VYUV, 32 bits, 16 bpc (Y), 8 bpc (V & U)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_AYUV: AYUV, 32 bits, 8 bpc
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_XRGB8888_A8: 2 planes: 1 RGB-plane (64-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_XBGR8888_A8: 2 planes: 1 BGR-plane (64-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_RGBX8888_A8: 2 planes: 1 RGB-plane (64-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_BGRX8888_A8: 2 planes: 1 BGR-plane (64-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_RGB888_A8: 2 planes: 1 RGB-plane (32-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_BGR888_A8: 2 planes: 1 BGR-plane (32-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_RGB565_A8: 2 planes: 1 RGB-plane (16-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_BGR565_A8: 2 planes: 1 BGR-plane (16-bit), 1 alpha-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV12: 2 planes: 1 Y-plane, 1 UV-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV21: 2 planes: 1 Y-plane, 1 VU-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV16: 2 planes: 1 Y-plane, 1 UV-plane (2x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV61: 2 planes: 1 Y-plane, 1 VU-plane (2x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV24: 2 planes: 1 Y-plane, 1 UV-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_NV42: 2 planes: 1 Y-plane, 1 VU-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUV410: 3 planes: 1 Y-plane, 1 U-plane (4x4 subsampled), 1 V-plane (4x4 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVU410: 3 planes: 1 Y-plane, 1 V-plane (4x4 subsampled), 1 U-plane (4x4 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUV411: 3 planes: 1 Y-plane, 1 U-plane (4x1 subsampled), 1 V-plane (4x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVU411: 3 planes: 1 Y-plane, 1 V-plane (4x1 subsampled), 1 U-plane (4x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUV420: 3 planes: 1 Y-plane, 1 U-plane (2x2 subsampled), 1 V-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVU420: 3 planes: 1 Y-plane, 1 V-plane (2x2 subsampled), 1 U-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUV422: 3 planes: 1 Y-plane, 1 U-plane (2x1 subsampled), 1 V-plane (2x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVU422: 3 planes: 1 Y-plane, 1 V-plane (2x1 subsampled), 1 U-plane (2x1 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YUV444: 3 planes: 1 Y-plane, 1 U-plane, 1 V-plane
 * @META_MULTI_TEXTURE_FORMAT_FORMAT_YVU444: 3 planes: 1 Y-plane, 1 V-plane, 1 U-plane
 *
 * XXX write docs
 */
typedef enum _MetaMultiTextureFormat
{
  META_MULTI_TEXTURE_FORMAT_SIMPLE,

  /* The following list is somewhat synced with Linux's <drm_fourcc.h> */

  /* Packed YUV */
  META_MULTI_TEXTURE_FORMAT_YUYV,
  META_MULTI_TEXTURE_FORMAT_YVYU,
  META_MULTI_TEXTURE_FORMAT_UYVY,
  META_MULTI_TEXTURE_FORMAT_VYUY,

  META_MULTI_TEXTURE_FORMAT_AYUV,

  /* 2 plane RGB + A */
  META_MULTI_TEXTURE_FORMAT_XRGB8888_A8,
  META_MULTI_TEXTURE_FORMAT_XBGR8888_A8,
  META_MULTI_TEXTURE_FORMAT_RGBX8888_A8,
  META_MULTI_TEXTURE_FORMAT_BGRX8888_A8,
  META_MULTI_TEXTURE_FORMAT_RGB888_A8,
  META_MULTI_TEXTURE_FORMAT_BGR888_A8,
  META_MULTI_TEXTURE_FORMAT_RGB565_A8,
  META_MULTI_TEXTURE_FORMAT_BGR565_A8,

  /* 2 plane YUV */
  META_MULTI_TEXTURE_FORMAT_NV12,
  META_MULTI_TEXTURE_FORMAT_NV21,
  META_MULTI_TEXTURE_FORMAT_NV16,
  META_MULTI_TEXTURE_FORMAT_NV61,
  META_MULTI_TEXTURE_FORMAT_NV24,
  META_MULTI_TEXTURE_FORMAT_NV42,

  /* 3 plane YUV */
  META_MULTI_TEXTURE_FORMAT_YUV410,
  META_MULTI_TEXTURE_FORMAT_YVU410,
  META_MULTI_TEXTURE_FORMAT_YUV411,
  META_MULTI_TEXTURE_FORMAT_YVU411,
  META_MULTI_TEXTURE_FORMAT_YUV420,
  META_MULTI_TEXTURE_FORMAT_YVU420,
  META_MULTI_TEXTURE_FORMAT_YUV422,
  META_MULTI_TEXTURE_FORMAT_YVU422,
  META_MULTI_TEXTURE_FORMAT_YUV444,
  META_MULTI_TEXTURE_FORMAT_YVU444
} MetaMultiTextureFormat;

META_EXPORT
const char * meta_multi_texture_format_to_string        (MetaMultiTextureFormat format);

META_EXPORT
void  meta_multi_texture_format_get_bytes_per_pixel     (MetaMultiTextureFormat format,
                                                         uint8_t               *bpp_out);

META_EXPORT
int   meta_multi_texture_format_get_n_planes            (MetaMultiTextureFormat format);

META_EXPORT
void  meta_multi_texture_format_get_subsampling_factors (MetaMultiTextureFormat format,
                                                         uint8_t               *horizontal_factors,
                                                         uint8_t               *vertical_factors);

META_EXPORT
void  meta_multi_texture_format_get_subformats          (MetaMultiTextureFormat format,
                                                         CoglPixelFormat       *formats_out);

META_EXPORT
gboolean meta_multi_texture_format_needs_shaders        (MetaMultiTextureFormat format);

META_EXPORT
gboolean meta_multi_texture_format_get_snippets         (MetaMultiTextureFormat format,
                                                        CoglSnippet          **vertex_snippet,
                                                        CoglSnippet          **fragment_snippet,
                                                        CoglSnippet          **layer_snippet);

G_END_DECLS

#endif
