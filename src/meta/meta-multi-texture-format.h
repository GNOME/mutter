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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * MetaMultiTextureFormat:
 * @META_MULTI_TEXTURE_FORMAT_INVALID: Invalid value
 * @META_MULTI_TEXTURE_FORMAT_SIMPLE: Any format supported by Cogl (see #CoglPixelFormat)
 * @META_MULTI_TEXTURE_FORMAT_YUYV: YUYV, 32 bits, 16 bpc (Y), 8 bpc (U & V)
 * @META_MULTI_TEXTURE_FORMAT_NV12: 2 planes: 1 Y-plane, 1 UV-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_YUV420: 3 planes: 1 Y-plane, 1 U-plane (2x2 subsampled), 1 V-plane (2x2 subsampled)
 *
 * A representation for complex pixel formats
 *
 * Some pixel formats that are used in the wild are a bit more complex than
 * just ARGB and all its variants. For example: a component might be put in a
 * different plane (i.e. at a different place in memory). Another example are
 * formats that use Y, U, and V components rather than RGB; if we composite them
 * onto an RGBA framebuffer, we have to make sure for example that these get
 * converted to the right color format first (using e.g. a shader).
 */
typedef enum _MetaMultiTextureFormat
{
  META_MULTI_TEXTURE_FORMAT_INVALID,
  META_MULTI_TEXTURE_FORMAT_SIMPLE,
  META_MULTI_TEXTURE_FORMAT_YUYV,
  META_MULTI_TEXTURE_FORMAT_YVYU,
  META_MULTI_TEXTURE_FORMAT_UYVY,
  META_MULTI_TEXTURE_FORMAT_VYUY,
  META_MULTI_TEXTURE_FORMAT_NV12,
  META_MULTI_TEXTURE_FORMAT_NV21,
  META_MULTI_TEXTURE_FORMAT_NV16,
  META_MULTI_TEXTURE_FORMAT_NV61,
  META_MULTI_TEXTURE_FORMAT_NV24,
  META_MULTI_TEXTURE_FORMAT_NV42,
  META_MULTI_TEXTURE_FORMAT_P010,
  META_MULTI_TEXTURE_FORMAT_P012,
  META_MULTI_TEXTURE_FORMAT_P016,
  META_MULTI_TEXTURE_FORMAT_YUV420,
  META_MULTI_TEXTURE_FORMAT_YVU420,
  META_MULTI_TEXTURE_FORMAT_YUV422,
  META_MULTI_TEXTURE_FORMAT_YVU422,
  META_MULTI_TEXTURE_FORMAT_YUV444,
  META_MULTI_TEXTURE_FORMAT_YVU444,
  META_MULTI_TEXTURE_FORMAT_S010,
  META_MULTI_TEXTURE_FORMAT_S210,
  META_MULTI_TEXTURE_FORMAT_S410,
  META_MULTI_TEXTURE_FORMAT_S012,
  META_MULTI_TEXTURE_FORMAT_S212,
  META_MULTI_TEXTURE_FORMAT_S412,
  META_MULTI_TEXTURE_FORMAT_S016,
  META_MULTI_TEXTURE_FORMAT_S216,
  META_MULTI_TEXTURE_FORMAT_S416,
  N_META_MULTI_TEXTURE_FORMATS
} MetaMultiTextureFormat;

typedef enum _MetaMultiTextureAlphaMode
{
  META_MULTI_TEXTURE_ALPHA_MODE_NONE = 0,
  META_MULTI_TEXTURE_ALPHA_MODE_PREMULT_ELECTRICAL,
  META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT,
  N_META_MULTI_TEXTURE_ALPHA_MODES
} MetaMultiTextureAlphaMode;

typedef enum _MetaMultiTextureCoefficients
{
  META_MULTI_TEXTURE_COEFFICIENTS_NONE = 0,
  META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_FULL,
  META_MULTI_TEXTURE_COEFFICIENTS_IDENTITY_LIMITED,
  META_MULTI_TEXTURE_COEFFICIENTS_BT709_FULL,
  META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED,
  META_MULTI_TEXTURE_COEFFICIENTS_BT601_FULL,
  META_MULTI_TEXTURE_COEFFICIENTS_BT601_LIMITED,
  META_MULTI_TEXTURE_COEFFICIENTS_BT2020_FULL,
  META_MULTI_TEXTURE_COEFFICIENTS_BT2020_LIMITED,
  N_META_MULTI_TEXTURE_COEFFICIENTS
} MetaMultiTextureCoefficients;

typedef enum _MetaMultiTextureChromaLoc
{
  META_MULTI_TEXTURE_CHROMA_LOC_NONE = 0,
  META_MULTI_TEXTURE_CHROMA_LOC_DEFINED,
} MetaMultiTextureChromaLoc;

G_END_DECLS
