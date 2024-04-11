/*
 * Copyright (C) 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 * Copyright (C) 2023 Collabora Ltd.
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
 *
 */

#include "config.h"

#include "common/meta-drm-format-helpers.h"

#include <drm_fourcc.h>
#include <glib.h>

/**
 * meta_drm_format_to_string:
 * @tmp: temporary buffer
 * @drm_format: DRM fourcc pixel format
 *
 * Returns a pointer to a string naming the given pixel format,
 * usually a pointer to the temporary buffer but not always.
 * Invalid formats may return nonsense names.
 *
 * When calling this, allocate one MetaDrmFormatBuf on the stack to
 * be used as the temporary buffer.
 */
const char *
meta_drm_format_to_string (MetaDrmFormatBuf *tmp,
                           uint32_t          drm_format)
{
  int i;

  if (drm_format == DRM_FORMAT_INVALID)
    return "INVALID";

  G_STATIC_ASSERT (sizeof (tmp->s) == 5);
  for (i = 0; i < 4; i++)
    {
      char c = (drm_format >> (i * 8)) & 0xff;
      tmp->s[i] = g_ascii_isgraph (c) ? c : '.';
    }

  tmp->s[i] = 0;

  return tmp->s;
}
