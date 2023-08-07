/*
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "cogl-config.h"

#include "cogl/cogl-scanout.h"

enum
{
  SCANOUT_FAILED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_INTERFACE (CoglScanout, cogl_scanout, G_TYPE_OBJECT)

static void
cogl_scanout_default_init (CoglScanoutInterface *iface)
{
  signals[SCANOUT_FAILED] =
    g_signal_new ("scanout-failed",
                  G_TYPE_FROM_INTERFACE (iface),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  COGL_TYPE_ONSCREEN);
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

void
cogl_scanout_notify_failed (CoglScanout  *scanout,
                            CoglOnscreen *onscreen)
{
  g_signal_emit (scanout, signals[SCANOUT_FAILED], 0, onscreen);
}
