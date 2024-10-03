/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008 OpenedHand
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
 */

#pragma once

#include <glib.h>
#include <pango/pango-font.h>

#include "cogl/cogl.h"

G_BEGIN_DECLS

typedef struct _ClutterPangoGlyphCache ClutterPangoGlyphCache;

typedef struct
{
  CoglTexture *texture;

  float tx1;
  float ty1;
  float tx2;
  float ty2;

  int tx_pixel;
  int ty_pixel;

  int draw_x;
  int draw_y;
  int draw_width;
  int draw_height;

  /* This will be set to TRUE when the glyph atlas is reorganized
     which means the glyph will need to be redrawn */
  guint dirty : 1;
  /* Set to TRUE if the glyph has colors (eg. emoji) */
  guint has_color : 1;
} PangoGlyphCacheValue;

ClutterPangoGlyphCache * clutter_pango_glyph_cache_new (CoglContext *ctx);

void clutter_pango_glyph_cache_free (ClutterPangoGlyphCache *cache);

PangoGlyphCacheValue * clutter_pango_glyph_cache_lookup (ClutterPangoGlyphCache *cache,
                                                         CoglContext            *context,
                                                         gboolean                create,
                                                         PangoFont              *font,
                                                         PangoGlyph              glyph);

void clutter_pango_glyph_cache_add_reorganize_callback (ClutterPangoGlyphCache *cache,
                                                        GHookFunc               func,
                                                        void                   *user_data);

void clutter_pango_glyph_cache_remove_reorganize_callback (ClutterPangoGlyphCache *cache,
                                                           GHookFunc               func,
                                                           void                   *user_data);

void clutter_pango_glyph_cache_set_dirty_glyphs (ClutterPangoGlyphCache *cache);

G_END_DECLS
