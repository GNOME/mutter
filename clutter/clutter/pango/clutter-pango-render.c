/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008 OpenedHand
 * Copyright (C) 2012 Intel Corporation.
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
 *
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 *   Matthew Allum  <mallum@openedhand.com>
 */

#include "config.h"

#include <pango/pango-fontmap.h>
#include <pango/pangocairo.h>
#include <pango/pango-renderer.h>
#include <cairo.h>
#include <cairo-ft.h>

#include "clutter/clutter-debug.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-private.h"
#include "clutter/pango/clutter-pango-private.h"
#include "clutter/pango/clutter-pango-glyph-cache.h"
#include "clutter/pango/clutter-pango-display-list.h"
#include "cogl/cogl.h"

#define PANGO_UNKNOWN_GLYPH_WIDTH 10
#define PANGO_UNKNOWN_GLYPH_HEIGHT 14

enum
{
  PROP_0,

  PROP_COGL_CONTEXT,
  PROP_LAST
};

struct _ClutterPangoRenderer
{
  PangoRenderer parent_instance;

  CoglContext *ctx;

  ClutterPangoGlyphCache *glyph_cache;
  ClutterPangoPipelineCache *pipeline_cache;

  /* The current display list that is being built */
  ClutterPangoDisplayList *display_list;
};

G_DECLARE_FINAL_TYPE (ClutterPangoRenderer,
                      clutter_pango_renderer,
                      CLUTTER_PANGO,
                      RENDERER,
                      PangoRenderer)

G_DEFINE_FINAL_TYPE (ClutterPangoRenderer, clutter_pango_renderer, PANGO_TYPE_RENDERER);

/* An instance of this struct gets attached to each PangoLayout to
   cache the VBO and to detect changes to the layout */
typedef struct _PangoLayoutQdata
{
  ClutterPangoRenderer *renderer;
  /* The cache of the geometry for the layout */
  ClutterPangoDisplayList *display_list;
  /* A reference to the first line of the layout. This is just used to
     detect changes */
  PangoLayoutLine *first_line;
} PangoLayoutQdata;

typedef struct
{
  ClutterPangoDisplayList *display_list;
  float x1, y1, x2, y2;
} PangoRendererSliceCbData;

PangoRenderer *
clutter_pango_renderer_new (CoglContext *context)
{
  return PANGO_RENDERER (g_object_new (clutter_pango_renderer_get_type (),
                                       "context", context, NULL));
}

static void
clutter_pango_renderer_slice_cb (CoglTexture *texture,
                                 const float *slice_coords,
                                 const float *virtual_coords,
                                 void        *user_data)
{
  PangoRendererSliceCbData *data = user_data;

  /* Note: this assumes that there is only one slice containing the
     whole texture and it doesn't attempt to split up the vertex
     coordinates based on the virtual_coords */

  clutter_pango_display_list_add_texture (data->display_list,
                                          texture,
                                          data->x1,
                                          data->y1,
                                          data->x2,
                                          data->y2,
                                          slice_coords[0],
                                          slice_coords[1],
                                          slice_coords[2],
                                          slice_coords[3]);
}

static void
clutter_pango_renderer_draw_glyph (ClutterPangoRenderer *renderer,
                                   PangoGlyphCacheValue *cache_value,
                                   float                 x1,
                                   float                 y1)
{
  PangoRendererSliceCbData data;

  g_return_if_fail (renderer->display_list != NULL);

  data.display_list = renderer->display_list;
  data.x1 = x1;
  data.y1 = y1;
  data.x2 = x1 + (float) cache_value->draw_width;
  data.y2 = y1 + (float) cache_value->draw_height;

  /* We iterate the internal sub textures of the texture so that we
     can get a pointer to the base texture even if the texture is in
     the global atlas. That way the display list can recognise that
     the neighbouring glyphs are coming from the same atlas and bundle
     them together into a single VBO */

  cogl_texture_foreach_in_region (cache_value->texture,
                                  cache_value->tx1,
                                  cache_value->ty1,
                                  cache_value->tx2,
                                  cache_value->ty2,
                                  COGL_PIPELINE_WRAP_MODE_REPEAT,
                                  COGL_PIPELINE_WRAP_MODE_REPEAT,
                                  clutter_pango_renderer_slice_cb,
                                  &data);
}

static void
clutter_pango_renderer_init (ClutterPangoRenderer *renderer)
{
}

static void
clutter_pango_renderer_constructed (GObject *gobject)
{
  ClutterPangoRenderer *renderer = CLUTTER_PANGO_RENDERER (gobject);
  CoglContext *ctx = renderer->ctx;

  renderer->pipeline_cache = clutter_pango_pipeline_cache_new (ctx);
  renderer->glyph_cache = clutter_pango_glyph_cache_new (ctx);

  G_OBJECT_CLASS (clutter_pango_renderer_parent_class)->constructed (gobject);
}

static void
clutter_pango_renderer_set_property (GObject      *object,
                                     unsigned int  prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  ClutterPangoRenderer *renderer = CLUTTER_PANGO_RENDERER (object);

  switch (prop_id)
    {
    case PROP_COGL_CONTEXT:
      renderer->ctx = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_pango_renderer_dispose (GObject *object)
{
  ClutterPangoRenderer *renderer = CLUTTER_PANGO_RENDERER (object);

  g_clear_object (&renderer->ctx);

  G_OBJECT_CLASS (clutter_pango_renderer_parent_class)->dispose (object);
}

static void
clutter_pango_renderer_finalize (GObject *object)
{
  ClutterPangoRenderer *renderer = CLUTTER_PANGO_RENDERER (object);

  clutter_pango_glyph_cache_free (renderer->glyph_cache);
  clutter_pango_pipeline_cache_free (renderer->pipeline_cache);

  G_OBJECT_CLASS (clutter_pango_renderer_parent_class)->finalize (object);
}

static GQuark
clutter_pango_layout_get_qdata_key (void)
{
  static GQuark key = 0;

  if (G_UNLIKELY (key == 0))
    key = g_quark_from_static_string ("ClutterPangoDisplayList");

  return key;
}

static void
clutter_pango_layout_qdata_forget_display_list (PangoLayoutQdata *qdata)
{
  if (qdata->display_list)
    {
      clutter_pango_glyph_cache_remove_reorganize_callback
        (qdata->renderer->glyph_cache,
        (GHookFunc) clutter_pango_layout_qdata_forget_display_list,
        qdata);

      clutter_pango_display_list_free (qdata->display_list);

      qdata->display_list = NULL;
    }
}

static void
clutter_pango_render_qdata_destroy (PangoLayoutQdata *qdata)
{
  clutter_pango_layout_qdata_forget_display_list (qdata);
  if (qdata->first_line)
    pango_layout_line_unref (qdata->first_line);
  g_free (qdata);
}

void
clutter_show_layout (ClutterContext    *context,
                     CoglFramebuffer   *fb,
                     PangoLayout       *layout,
                     float              x,
                     float              y,
                     const CoglColor   *color,
                     ClutterColorState *color_state,
                     ClutterColorState *target_color_state)
{
  ClutterPangoRenderer *renderer;
  PangoLayoutQdata *qdata;

  renderer = CLUTTER_PANGO_RENDERER (clutter_context_get_font_renderer (context));
  if (G_UNLIKELY (!renderer))
    return;

  qdata = g_object_get_qdata (G_OBJECT (layout),
                              clutter_pango_layout_get_qdata_key ());

  if (qdata == NULL)
    {
      qdata = g_new0 (PangoLayoutQdata, 1);
      qdata->renderer = renderer;
      g_object_set_qdata_full (G_OBJECT (layout),
                               clutter_pango_layout_get_qdata_key (),
                               qdata,
                               (GDestroyNotify)
                               clutter_pango_render_qdata_destroy);
    }

  /* Check if the layout has changed since the last build of the
     display list. This trick was suggested by Behdad Esfahbod here:
     http://mail.gnome.org/archives/gtk-i18n-list/2009-May/msg00019.html */
  if (qdata->display_list &&
      ((qdata->first_line &&
        qdata->first_line->layout != layout)))
    clutter_pango_layout_qdata_forget_display_list (qdata);

  if (qdata->display_list == NULL)
    {
      clutter_ensure_glyph_cache_for_layout (context, layout);

      qdata->display_list =
        clutter_pango_display_list_new (renderer->pipeline_cache);

      /* Register for notification of when the glyph cache changes so
         we can rebuild the display list */
      clutter_pango_glyph_cache_add_reorganize_callback
        (renderer->glyph_cache,
        (GHookFunc) clutter_pango_layout_qdata_forget_display_list,
        qdata);

      renderer->display_list = qdata->display_list;
      pango_renderer_draw_layout (PANGO_RENDERER (renderer), layout, 0, 0);
      renderer->display_list = NULL;
    }

  cogl_framebuffer_push_matrix (fb);
  cogl_framebuffer_translate (fb, x, y, 0);

  clutter_pango_display_list_render (fb,
                                     qdata->display_list,
                                     color_state, target_color_state,
                                     color);

  cogl_framebuffer_pop_matrix (fb);

  /* Keep a reference to the first line of the layout so we can detect
     changes */
  if (qdata->first_line)
    {
      pango_layout_line_unref (qdata->first_line);
      qdata->first_line = NULL;
    }
  if (pango_layout_get_line_count (layout) > 0)
    {
      qdata->first_line = pango_layout_get_line (layout, 0);
      pango_layout_line_ref (qdata->first_line);
    }
}

static PangoGlyphCacheValue *
clutter_pango_renderer_get_cached_glyph (PangoRenderer *renderer,
                                         gboolean       create,
                                         PangoFont     *font,
                                         PangoGlyph     glyph)
{
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);

  return clutter_pango_glyph_cache_lookup (priv->glyph_cache,
                                           priv->ctx,
                                           create, font, glyph);
}

static void
clutter_pango_ensure_glyph_cache_for_layout_line_internal (PangoRenderer   *renderer,
                                                           PangoLayoutLine *line)
{
  GSList *l;

  for (l = line->runs; l; l = l->next)
    {
      PangoLayoutRun *run = l->data;
      PangoGlyphString *glyphs = run->glyphs;
      int i;

      for (i = 0; i < glyphs->num_glyphs; i++)
        {
          PangoGlyphInfo *gi = &glyphs->glyphs[i];

          /* If the glyph isn't cached then this will reserve
             space for it now. We won't actually draw the glyph
             yet because reserving space could cause all of the
             other glyphs to be moved so we might as well redraw
             them all later once we know that the position is
             settled */
          clutter_pango_renderer_get_cached_glyph (renderer, TRUE,
                                                   run->item->analysis.font,
                                                   gi->glyph);
        }
    }
}

void
clutter_ensure_glyph_cache_for_layout (ClutterContext *context,
                                       PangoLayout    *layout)
{
  PangoRenderer *renderer;
  PangoLayoutIter *iter;

  renderer = clutter_context_get_font_renderer (context);

  g_return_if_fail (PANGO_IS_LAYOUT (layout));

  if ((iter = pango_layout_get_iter (layout)) == NULL)
    return;

  do
    {
      PangoLayoutLine *line;

      line = pango_layout_iter_get_line_readonly (iter);

      clutter_pango_ensure_glyph_cache_for_layout_line_internal (renderer, line);
    }
  while (pango_layout_iter_next_line (iter));

  pango_layout_iter_free (iter);

  /* Now that we know all of the positions are settled we'll fill in
     any dirty glyphs */
  clutter_pango_glyph_cache_set_dirty_glyphs (CLUTTER_PANGO_RENDERER (renderer)->glyph_cache);
}

static void
clutter_pango_renderer_set_color_for_part (PangoRenderer   *renderer,
                                           PangoRenderPart  part)
{
  PangoColor *pango_color = pango_renderer_get_color (renderer, part);
  uint16_t alpha = pango_renderer_get_alpha (renderer, part);
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);

  if (pango_color)
    {
      CoglColor color;

      cogl_color_init_from_4f (&color,
                               pango_color->red / 65535.0f,
                               pango_color->green / 65535.0f,
                               pango_color->blue / 65535.0f,
                               alpha ? alpha / 65535.0f : 1.0f);

      clutter_pango_display_list_set_color_override (priv->display_list, &color);
    }
  else
    clutter_pango_display_list_remove_color_override (priv->display_list);
}

static void
clutter_pango_renderer_draw_box (PangoRenderer *renderer,
                                 int            x,
                                 int            y,
                                 int            width,
                                 int            height)
{
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);

  g_return_if_fail (priv->display_list != NULL);

  clutter_pango_display_list_add_rectangle (priv->display_list,
                                            x,
                                            y - height,
                                            x + width,
                                            y);
}

static void
clutter_pango_renderer_get_device_units (PangoRenderer *renderer,
                                         int            xin,
                                         int            yin,
                                         float         *xout,
                                         float         *yout)
{
  const PangoMatrix *matrix;

  if ((matrix = pango_renderer_get_matrix (renderer)))
    {
      /* Convert user-space coords to device coords */
      *xout = (float) ((xin * matrix->xx + yin * matrix->xy) /
                       PANGO_SCALE + matrix->x0);
      *yout = (float) ((yin * matrix->yy + xin * matrix->yx) /
                       PANGO_SCALE + matrix->y0);
    }
  else
    {
      *xout = PANGO_PIXELS (xin);
      *yout = PANGO_PIXELS (yin);
    }
}

static void
clutter_pango_renderer_draw_rectangle (PangoRenderer   *renderer,
                                       PangoRenderPart  part,
                                       int              x,
                                       int              y,
                                       int              width,
                                       int              height)
{
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);
  float x1, x2, y1, y2;

  g_return_if_fail (priv->display_list != NULL);

  clutter_pango_renderer_set_color_for_part (renderer, part);

  clutter_pango_renderer_get_device_units (renderer,
                                           x, y,
                                           &x1, &y1);
  clutter_pango_renderer_get_device_units (renderer,
                                           x + width, y + height,
                                           &x2, &y2);

  clutter_pango_display_list_add_rectangle (priv->display_list,
                                            x1, y1, x2, y2);
}

static void
clutter_pango_renderer_draw_trapezoid (PangoRenderer   *renderer,
                                       PangoRenderPart  part,
                                       double           y1,
                                       double           x11,
                                       double           x21,
                                       double           y2,
                                       double           x12,
                                       double           x22)
{
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);

  g_return_if_fail (priv->display_list != NULL);

  clutter_pango_renderer_set_color_for_part (renderer, part);

  clutter_pango_display_list_add_trapezoid (priv->display_list,
                                            (float) y1,
                                            (float) x11,
                                            (float) x21,
                                            (float) y2,
                                            (float) x12,
                                            (float) x22);
}

static void
clutter_pango_renderer_draw_glyphs (PangoRenderer    *renderer,
                                    PangoFont        *font,
                                    PangoGlyphString *glyphs,
                                    int               xi,
                                    int               yi)
{
  ClutterPangoRenderer *priv = CLUTTER_PANGO_RENDERER (renderer);
  PangoGlyphCacheValue *cache_value;
  int i;

  for (i = 0; i < glyphs->num_glyphs; i++)
    {
      PangoGlyphInfo *gi = glyphs->glyphs + i;
      float x, y;

      clutter_pango_renderer_set_color_for_part (renderer,
                                                 PANGO_RENDER_PART_FOREGROUND);
      clutter_pango_renderer_get_device_units (renderer,
                                               xi + gi->geometry.x_offset,
                                               yi + gi->geometry.y_offset,
                                               &x, &y);

      if ((gi->glyph & PANGO_GLYPH_UNKNOWN_FLAG))
        {
          if (font == NULL)
            {
              clutter_pango_renderer_draw_box (renderer,
                                               (int) x,
                                               (int) y,
                                               PANGO_UNKNOWN_GLYPH_WIDTH,
                                               PANGO_UNKNOWN_GLYPH_HEIGHT);
            }
          else
            {
              PangoRectangle ink_rect;

              pango_font_get_glyph_extents (font, gi->glyph, &ink_rect, NULL);
              pango_extents_to_pixels (&ink_rect, NULL);

              clutter_pango_renderer_draw_box (renderer,
                                               (int) (x + ink_rect.x),
                                               (int) (y + ink_rect.y + ink_rect.height),
                                               ink_rect.width,
                                               ink_rect.height);
            }
        }
      else
        {
          /* Get the texture containing the glyph */
          cache_value =
            clutter_pango_renderer_get_cached_glyph (renderer,
                                                     FALSE,
                                                     font,
                                                     gi->glyph);

          /* clutter_ensure_glyph_cache_for_layout should always be
             called before rendering a layout so we should never have
             a dirty glyph here */
          g_assert (cache_value == NULL || !cache_value->dirty);

          if (cache_value == NULL)
            {
              clutter_pango_renderer_draw_box (renderer,
                                               (int) x,
                                               (int) y,
                                               PANGO_UNKNOWN_GLYPH_WIDTH,
                                               PANGO_UNKNOWN_GLYPH_HEIGHT);
            }
          else if (cache_value->texture)
            {
              x += (float)(cache_value->draw_x);
              y += (float)(cache_value->draw_y);

              /* Do not override color if the glyph/font provide its own */
              if (cache_value->has_color)
                {
                  CoglColor color;
                  uint16_t alpha;

                  alpha = pango_renderer_get_alpha (renderer,
                                                    PANGO_RENDER_PART_FOREGROUND);
                  cogl_color_init_from_4f (&color, 1.0f, 1.0f, 1.0f,
                                           alpha ? (alpha >> 8) / 255.0f : 1.0f);
                  clutter_pango_display_list_set_color_override (priv->display_list, &color);
                }

              clutter_pango_renderer_draw_glyph (priv, cache_value, x, y);
            }
        }

      xi += gi->geometry.width;
    }
}

static void
clutter_pango_renderer_class_init (ClutterPangoRendererClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PangoRendererClass *renderer_class = PANGO_RENDERER_CLASS (klass);
  GParamSpec *pspec;

  object_class->set_property = clutter_pango_renderer_set_property;
  object_class->constructed = clutter_pango_renderer_constructed;
  object_class->dispose = clutter_pango_renderer_dispose;
  object_class->finalize = clutter_pango_renderer_finalize;

  pspec = g_param_spec_object ("context", NULL, NULL,
                               COGL_TYPE_CONTEXT,
                               G_PARAM_WRITABLE |
                               G_PARAM_STATIC_STRINGS |
                               G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class, PROP_COGL_CONTEXT, pspec);

  renderer_class->draw_glyphs = clutter_pango_renderer_draw_glyphs;
  renderer_class->draw_rectangle = clutter_pango_renderer_draw_rectangle;
  renderer_class->draw_trapezoid = clutter_pango_renderer_draw_trapezoid;
}
