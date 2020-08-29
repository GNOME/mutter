/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "cogl-config.h"

#include "cogl-scanout.h"

G_DEFINE_INTERFACE (CoglScanout, cogl_scanout, G_TYPE_OBJECT)

static void
cogl_scanout_default_init (CoglScanoutInterface *iface)
{
}

gboolean
cogl_scanout_blit_to_framebuffer (CoglScanout      *scanout,
                                  CoglFramebuffer  *framebuffer,
                                  int               x,
                                  int               y,
                                  GError          **error)
{
  CoglScanoutInterface *iface;

  g_return_val_if_fail (COGL_IS_SCANOUT (scanout), FALSE);

  iface = COGL_SCANOUT_GET_IFACE (scanout);

  if (iface->blit_to_framebuffer)
    return iface->blit_to_framebuffer (scanout, framebuffer, x, y, error);
  else
    return FALSE;
}
