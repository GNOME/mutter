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

#include "config.h"

#include <cairo-ft.h>
#include <glib.h>
#include <pango/pangocairo.h>

#include "clutter/clutter-debug.h"
#include "clutter/pango/clutter-pango-glyph-cache.h"
#include "clutter/pango/clutter-pango-private.h"

struct _ClutterPangoGlyphCache
{
  CoglContext *ctx;

  /* Hash table to quickly check whether a particular glyph in a
     particular font is already cached */
  GHashTable       *hash_table;

  /* List of CoglAtlases */
  GSList           *atlases;

  /* List of callbacks to invoke when an atlas is reorganized */
  GHookList reorganize_callbacks;

  /* TRUE if we've ever stored a texture in the global atlas. This is
     used to make sure we only register one callback to listen for
     global atlas reorganizations */
  gboolean using_global_atlas;

  /* True if some of the glyphs are dirty. This is used as an
     optimization in clutter_pango_glyph_cache_set_dirty_glyphs to avoid
     iterating the hash table if we know none of them are dirty */
  gboolean has_dirty_glyphs;
};

typedef struct _PangoGlyphCacheKey
{
  PangoFont  *font;
  PangoGlyph glyph;
} PangoGlyphCacheKey;

static void
clutter_pango_glyph_cache_value_free (PangoGlyphCacheValue *value)
{
  g_clear_object (&value->texture);
  g_free (value);
}

static void
clutter_pango_glyph_cache_key_free (PangoGlyphCacheKey *key)
{
  g_clear_object (&key->font);
  g_free (key);
}

static unsigned int
clutter_pango_glyph_cache_hash_func (const void *key)
{
  const PangoGlyphCacheKey *cache_key = (const PangoGlyphCacheKey *) key;

  /* Generate a number affected by both the font and the glyph
     number. We can safely directly compare the pointers because the
     key holds a reference to the font so it is not possible that a
     different font will have the same memory address */
  return GPOINTER_TO_UINT (cache_key->font) ^ cache_key->glyph;
}

static gboolean
clutter_pango_glyph_cache_equal_func (const void *a,
                                      const void *b)
{
  const PangoGlyphCacheKey *key_a = (const PangoGlyphCacheKey *) a;
  const PangoGlyphCacheKey *key_b = (const PangoGlyphCacheKey *) b;

  /* We can safely directly compare the pointers for the fonts because
     the key holds a reference to the font so it is not possible that
     a different font will have the same memory address */
  return key_a->font == key_b->font
         && key_a->glyph == key_b->glyph;
}

ClutterPangoGlyphCache *
clutter_pango_glyph_cache_new (CoglContext *ctx)
{
  ClutterPangoGlyphCache *cache;

  cache = g_malloc (sizeof (ClutterPangoGlyphCache));

  /* Note: as a rule we don't take references to a CoglContext
   * internally since */
  cache->ctx = ctx;

  cache->hash_table = g_hash_table_new_full
    (clutter_pango_glyph_cache_hash_func,
     clutter_pango_glyph_cache_equal_func,
     (GDestroyNotify) clutter_pango_glyph_cache_key_free,
     (GDestroyNotify) clutter_pango_glyph_cache_value_free);

  cache->atlases = NULL;
  g_hook_list_init (&cache->reorganize_callbacks, sizeof (GHook));

  cache->has_dirty_glyphs = FALSE;

  cache->using_global_atlas = FALSE;

  return cache;
}

static void
clutter_pango_glyph_cache_reorganize_cb (void *user_data)
{
  ClutterPangoGlyphCache *cache = user_data;

  g_hook_list_invoke (&cache->reorganize_callbacks, FALSE);
}

void
clutter_pango_glyph_cache_free (ClutterPangoGlyphCache *cache)
{
  if (cache->using_global_atlas)
    {
      cogl_atlas_texture_remove_reorganize_callback (
                                  cache->ctx,
                                  clutter_pango_glyph_cache_reorganize_cb, cache);
    }

  g_slist_foreach (cache->atlases, (GFunc) g_object_unref, NULL);
  g_clear_pointer (&cache->atlases, g_slist_free);
  cache->has_dirty_glyphs = FALSE;

  g_hash_table_remove_all (cache->hash_table);
  g_clear_pointer (&cache->hash_table, g_hash_table_unref);

  g_hook_list_clear (&cache->reorganize_callbacks);

  g_free (cache);
}

static void
clutter_pango_glyph_cache_update_position_cb (void               *user_data,
                                              CoglTexture        *new_texture,
                                              const MtkRectangle *rect)
{
  PangoGlyphCacheValue *value = user_data;
  float tex_width, tex_height;

  g_clear_object (&value->texture);
  value->texture = g_object_ref (new_texture);

  tex_width = cogl_texture_get_width (new_texture);
  tex_height = cogl_texture_get_height (new_texture);

  value->tx1 = rect->x / tex_width;
  value->ty1 = rect->y / tex_height;
  value->tx2 = (rect->x + value->draw_width) / tex_width;
  value->ty2 = (rect->y + value->draw_height) / tex_height;

  value->tx_pixel = rect->x;
  value->ty_pixel = rect->y;

  /* The glyph has changed position so it will need to be redrawn */
  value->dirty = TRUE;
}

static gboolean
clutter_pango_glyph_cache_add_to_global_atlas (ClutterPangoGlyphCache  *cache,
                                               PangoFont               *font,
                                               PangoGlyph               glyph,
                                               PangoGlyphCacheValue    *value)
{
  CoglTexture *texture;
  GError *ignore_error = NULL;

  texture = cogl_atlas_texture_new_with_size (cache->ctx,
                                              value->draw_width,
                                              value->draw_height);
  if (!cogl_texture_allocate (texture, &ignore_error))
    {
      g_error_free (ignore_error);
      return FALSE;
    }

  value->texture = texture;
  value->tx1 = 0;
  value->ty1 = 0;
  value->tx2 = 1;
  value->ty2 = 1;
  value->tx_pixel = 0;
  value->ty_pixel = 0;

  /* The first time we store a texture in the global atlas we'll
     register for notifications when the global atlas is reorganized
     so we can forward the notification on as a glyph
     reorganization */
  if (!cache->using_global_atlas)
    {
      cogl_atlas_texture_add_reorganize_callback
        (cache->ctx,
         clutter_pango_glyph_cache_reorganize_cb, cache);
      cache->using_global_atlas = TRUE;
    }

  return TRUE;
}

static gboolean
clutter_pango_glyph_cache_add_to_local_atlas (ClutterPangoGlyphCache  *cache,
                                              CoglContext             *context,
                                              PangoFont               *font,
                                              PangoGlyph               glyph,
                                              PangoGlyphCacheValue    *value)
{
  CoglAtlas *atlas = NULL;
  GSList *l;

  /* Look for an atlas that can reserve the space */
  for (l = cache->atlases; l; l = l->next)
    if (cogl_atlas_reserve_space (l->data,
                                  value->draw_width + 1,
                                  value->draw_height + 1,
                                  value))
      {
        atlas = l->data;
        break;
      }

  /* If we couldn't find one then start a new atlas */
  if (atlas == NULL)
    {
      atlas = cogl_atlas_new (context,
                              COGL_PIXEL_FORMAT_A_8,
                              COGL_ATLAS_CLEAR_TEXTURE |
                              COGL_ATLAS_DISABLE_MIGRATION,
                              clutter_pango_glyph_cache_update_position_cb);
      CLUTTER_NOTE (PANGO, "Created new atlas for glyphs: %p", atlas);
      /* If we still can't reserve space then something has gone
         seriously wrong so we'll just give up */
      if (!cogl_atlas_reserve_space (atlas,
                                     value->draw_width + 1,
                                     value->draw_height + 1,
                                     value))
        {
          g_object_unref (atlas);
          return FALSE;
        }

      cogl_atlas_add_reorganize_callback
        (atlas, clutter_pango_glyph_cache_reorganize_cb, NULL, cache);

      cache->atlases = g_slist_prepend (cache->atlases, atlas);
    }

  return TRUE;
}

PangoGlyphCacheValue *
clutter_pango_glyph_cache_lookup (ClutterPangoGlyphCache *cache,
                                  CoglContext            *context,
                                  gboolean                create,
                                  PangoFont              *font,
                                  PangoGlyph              glyph)
{
  PangoGlyphCacheKey lookup_key;
  PangoGlyphCacheValue *value;

  lookup_key.font = font;
  lookup_key.glyph = glyph;

  value = g_hash_table_lookup (cache->hash_table, &lookup_key);

  if (create && value == NULL)
    {
      PangoGlyphCacheKey *key;
      PangoRectangle ink_rect;

      value = g_new0 (PangoGlyphCacheValue, 1);
      value->texture = NULL;

      pango_font_get_glyph_extents (font, glyph, &ink_rect, NULL);
      pango_extents_to_pixels (&ink_rect, NULL);

      value->draw_x = ink_rect.x;
      value->draw_y = ink_rect.y;
      value->draw_width = ink_rect.width;
      value->draw_height = ink_rect.height;

      /* If the glyph is zero-sized then we don't need to reserve any
         space for it and we can just avoid painting anything */
      if (ink_rect.width < 1 || ink_rect.height < 1)
        value->dirty = FALSE;
      else
        {
          /* Try adding the glyph to the global atlas... */
          if (!clutter_pango_glyph_cache_add_to_global_atlas (cache,
                                                              font,
                                                              glyph,
                                                              value) &&
              /* If it fails try the local atlas */
              !clutter_pango_glyph_cache_add_to_local_atlas (cache,
                                                             context,
                                                             font,
                                                             glyph,
                                                             value))
            {
              clutter_pango_glyph_cache_value_free (value);
              return NULL;
            }

          value->dirty = TRUE;
          cache->has_dirty_glyphs = TRUE;
        }

      key = g_new0 (PangoGlyphCacheKey, 1);
      key->font = g_object_ref (font);
      key->glyph = glyph;

      g_hash_table_insert (cache->hash_table, key, value);
    }

  return value;
}

static gboolean
font_has_color_glyphs (const PangoFont *font)
{
  cairo_scaled_font_t *scaled_font;
  gboolean has_color = FALSE;

  scaled_font = pango_cairo_font_get_scaled_font ((PangoCairoFont *) font);

  if (cairo_scaled_font_get_type (scaled_font) == CAIRO_FONT_TYPE_FT)
    {
      FT_Face ft_face = cairo_ft_scaled_font_lock_face (scaled_font);
      has_color = (FT_HAS_COLOR (ft_face) != 0);
      cairo_ft_scaled_font_unlock_face (scaled_font);
    }

  return has_color;
}

static void
clutter_pango_glyph_cache_set_dirty_glyphs_cb (void *key_ptr,
                                               void *value_ptr,
                                               void *user_data)
{
  PangoGlyphCacheKey *key = key_ptr;
  PangoGlyphCacheValue *value = value_ptr;
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_scaled_font_t *scaled_font;
  cairo_glyph_t cairo_glyph;
  cairo_format_t format_cairo;
  CoglPixelFormat format_cogl;

  if (value->dirty)
    {
      CLUTTER_NOTE (PANGO, "redrawing glyph %i", key->glyph);

      /* Glyphs that don't take up any space will end up without a
        texture. These should never become dirty so they shouldn't end up
        here */
      g_return_if_fail (value->texture != NULL);

      if (cogl_texture_get_format (value->texture) == COGL_PIXEL_FORMAT_A_8)
        {
          format_cairo = CAIRO_FORMAT_A8;
          format_cogl = COGL_PIXEL_FORMAT_A_8;
        }
      else
        {
          format_cairo = CAIRO_FORMAT_ARGB32;

          /* Cairo stores the data in native byte order as ARGB but Cogl's
            pixel formats specify the actual byte order. Therefore we
            need to use a different format depending on the
            architecture */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
          format_cogl = COGL_PIXEL_FORMAT_BGRA_8888_PRE;
#else
          format_cogl = COGL_PIXEL_FORMAT_ARGB_8888_PRE;
#endif
        }

      surface = cairo_image_surface_create (format_cairo,
                                            value->draw_width,
                                            value->draw_height);
      cr = cairo_create (surface);

      scaled_font = pango_cairo_font_get_scaled_font (PANGO_CAIRO_FONT (key->font));
      cairo_set_scaled_font (cr, scaled_font);

      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);

      cairo_glyph.x = -value->draw_x;
      cairo_glyph.y = -value->draw_y;
      /* The PangoCairo glyph numbers directly map to Cairo glyph
        numbers */
      cairo_glyph.index = key->glyph;
      cairo_show_glyphs (cr, &cairo_glyph, 1);

      cairo_destroy (cr);
      cairo_surface_flush (surface);

      /* Copy the glyph to the texture */
      cogl_texture_set_region (value->texture,
                              0, /* src_x */
                              0, /* src_y */
                              value->tx_pixel, /* dst_x */
                              value->ty_pixel, /* dst_y */
                              value->draw_width, /* dst_width */
                              value->draw_height, /* dst_height */
                              value->draw_width, /* width */
                              value->draw_height, /* height */
                              format_cogl,
                              cairo_image_surface_get_stride (surface),
                              cairo_image_surface_get_data (surface));

      cairo_surface_destroy (surface);

      value->has_color = font_has_color_glyphs (key->font);
      value->dirty = FALSE;
    }
}

void
clutter_pango_glyph_cache_set_dirty_glyphs (ClutterPangoGlyphCache *cache)
{
  /* If we know that there are no dirty glyphs then we can shortcut
     out early */
  if (!cache->has_dirty_glyphs)
    return;

  g_hash_table_foreach (cache->hash_table,
                        clutter_pango_glyph_cache_set_dirty_glyphs_cb,
                        NULL);

  cache->has_dirty_glyphs = FALSE;
}

void
clutter_pango_glyph_cache_add_reorganize_callback (ClutterPangoGlyphCache *cache,
                                                   GHookFunc               func,
                                                   void                   *user_data)
{
  GHook *hook = g_hook_alloc (&cache->reorganize_callbacks);
  hook->func = func;
  hook->data = user_data;
  g_hook_prepend (&cache->reorganize_callbacks, hook);
}

void
clutter_pango_glyph_cache_remove_reorganize_callback (ClutterPangoGlyphCache *cache,
                                                      GHookFunc               func,
                                                      void                   *user_data)
{
  GHook *hook = g_hook_find_func_data (&cache->reorganize_callbacks,
                                       FALSE,
                                       func,
                                       user_data);

  if (hook)
    g_hook_destroy_link (&cache->reorganize_callbacks, hook);
}
