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

#include "config.h"

#include "cogl/cogl-scanout.h"

enum
{
  SCANOUT_FAILED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_INTERFACE (CoglScanoutBuffer, cogl_scanout_buffer, G_TYPE_OBJECT)

struct _CoglScanout
{
  GObject parent;

  CoglScanoutBuffer *scanout_buffer;

  gboolean has_src_rect;
  graphene_rect_t src_rect;
  gboolean has_dst_rect;
  MtkRectangle dst_rect;
};

G_DEFINE_FINAL_TYPE (CoglScanout, cogl_scanout, G_TYPE_OBJECT);

static void
cogl_scanout_buffer_default_init (CoglScanoutBufferInterface *iface)
{
}

gboolean
cogl_scanout_blit_to_framebuffer (CoglScanout      *scanout,
                                  CoglFramebuffer  *framebuffer,
                                  int               x,
                                  int               y,
                                  GError          **error)
{
  CoglScanoutBufferInterface *iface =
    COGL_SCANOUT_BUFFER_GET_IFACE (scanout->scanout_buffer);

  return iface->blit_to_framebuffer (scanout, framebuffer, x, y, error);
}

int
cogl_scanout_buffer_get_width (CoglScanoutBuffer *scanout_buffer)
{
  CoglScanoutBufferInterface *iface =
    COGL_SCANOUT_BUFFER_GET_IFACE (scanout_buffer);

  return iface->get_width (scanout_buffer);
}

int
cogl_scanout_buffer_get_height (CoglScanoutBuffer *scanout_buffer)
{
  CoglScanoutBufferInterface *iface =
    COGL_SCANOUT_BUFFER_GET_IFACE (scanout_buffer);

  return iface->get_height (scanout_buffer);
}

CoglScanoutBuffer *
cogl_scanout_get_buffer (CoglScanout *scanout)
{
  return scanout->scanout_buffer;
}

void
cogl_scanout_notify_failed (CoglScanout  *scanout,
                            CoglOnscreen *onscreen)
{
  g_signal_emit (scanout, signals[SCANOUT_FAILED], 0, onscreen);
}

CoglScanout *
cogl_scanout_new (CoglScanoutBuffer *scanout_buffer)
{
  CoglScanout *scanout = g_object_new (COGL_TYPE_SCANOUT, NULL);

  scanout->scanout_buffer = scanout_buffer;

  return scanout;
}

void
cogl_scanout_get_src_rect (CoglScanout     *scanout,
                           graphene_rect_t *rect)
{
  if (scanout->has_src_rect)
    {
      *rect = scanout->src_rect;
      return;
    }

  rect->origin.x = 0;
  rect->origin.y = 0;
  rect->size.width = cogl_scanout_buffer_get_width (scanout->scanout_buffer);
  rect->size.height = cogl_scanout_buffer_get_height (scanout->scanout_buffer);
}

void
cogl_scanout_set_src_rect (CoglScanout           *scanout,
                           const graphene_rect_t *rect)
{
  if (rect != NULL)
    scanout->src_rect = *rect;

  scanout->has_src_rect = rect != NULL;
}

void
cogl_scanout_get_dst_rect (CoglScanout  *scanout,
                           MtkRectangle *rect)
{
  if (scanout->has_dst_rect)
    {
      *rect = scanout->dst_rect;
      return;
    }

  rect->x = 0;
  rect->y = 0;
  rect->width = cogl_scanout_buffer_get_width (scanout->scanout_buffer);
  rect->height = cogl_scanout_buffer_get_height (scanout->scanout_buffer);
}

void
cogl_scanout_set_dst_rect (CoglScanout        *scanout,
                           const MtkRectangle *rect)
{
  if (rect != NULL)
    scanout->dst_rect = *rect;

  scanout->has_dst_rect = rect != NULL;
}

static void
cogl_scanout_finalize (GObject *object)
{
  CoglScanout *scanout = COGL_SCANOUT (object);

  g_clear_object (&scanout->scanout_buffer);

  G_OBJECT_CLASS (cogl_scanout_parent_class)->finalize (object);
}

static void
cogl_scanout_class_init (CoglScanoutClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cogl_scanout_finalize;

  signals[SCANOUT_FAILED] =
    g_signal_new ("scanout-failed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  COGL_TYPE_ONSCREEN);
}

static void
cogl_scanout_init (CoglScanout *scanout)
{
}
