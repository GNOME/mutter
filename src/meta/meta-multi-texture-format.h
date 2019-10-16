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

#ifndef META_MULTI_TEXTURE_FORMAT_H
#define META_MULTI_TEXTURE_FORMAT_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * MetaMultiTextureFormat:
 * @META_MULTI_TEXTURE_FORMAT_INVALID: Invalid value
 * @META_MULTI_TEXTURE_FORMAT_SIMPLE: Any format supported by Cogl (see #CoglPixelFormat)
 * @META_MULTI_TEXTURE_FORMAT_YUYV: YUYV, 32 bits, 16 bpc (Y), 8 bpc (U & V)
 * @META_MULTI_TEXTURE_FORMAT_NV12: 2 planes: 1 Y-plane, 1 UV-plane (2x2 subsampled)
 * @META_MULTI_TEXTURE_FORMAT_YUV420: 3 planes: 1 Y-plane, 1 U-plane (2x2 subsampled), 1 V-plane (2x2 subsampled)
 */
typedef enum _MetaMultiTextureFormat
{
  META_MULTI_TEXTURE_FORMAT_INVALID,
  META_MULTI_TEXTURE_FORMAT_SIMPLE,
  META_MULTI_TEXTURE_FORMAT_YUYV,
  META_MULTI_TEXTURE_FORMAT_NV12,
  META_MULTI_TEXTURE_FORMAT_YUV420,
} MetaMultiTextureFormat;

G_END_DECLS

#endif /* META_MULTI_TEXTURE_FORMAT_H */
