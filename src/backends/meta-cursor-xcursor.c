/*
 * Copyright 2013, 2018 Red Hat, Inc.
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

#include "backends/meta-cursor-xcursor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/prefs.h"
#include "meta/util.h"

typedef struct _MetaCursorXcursorKey
{
  ClutterCursorType cursor;
  int theme_scale;
} MetaCursorXcursorKey;

struct _MetaCursorXcursor
{
  ClutterCursor parent;

  MetaCursorTracker *cursor_tracker;
  CoglTexture *texture;
  int hot_x;
  int hot_y;

  ClutterCursorType cursor;

  int current_frame;
  XcursorImages *xcursor_images;

  int theme_scale;
  gboolean invalidated;
};

G_DEFINE_TYPE (MetaCursorXcursor, meta_cursor_xcursor,
               CLUTTER_TYPE_CURSOR)

static unsigned int
cursor_key_hash (gconstpointer data)
{
  const MetaCursorXcursorKey *key = data;

  return key->cursor << 0  &
         key->theme_scale << 8;
}

static gboolean
cursor_key_equal (gconstpointer data1,
                  gconstpointer data2)
{
  const MetaCursorXcursorKey *key1 = data1;
  const MetaCursorXcursorKey *key2 = data2;

  return (key1->cursor == key2->cursor &&
          key1->theme_scale == key2->theme_scale);
}

static GHashTable *
ensure_cache (MetaCursorXcursor *cursor_xcursor)
{
  MetaCursorTracker *cursor_tracker = cursor_xcursor->cursor_tracker;
  GHashTable *cache;
  static GOnce quark_once = G_ONCE_INIT;

  g_once (&quark_once, (GThreadFunc) g_quark_from_static_string,
          (gpointer) "-meta-cursor-xcursor-cache");

  cache = g_object_get_qdata (G_OBJECT (cursor_tracker),
                              GPOINTER_TO_INT (quark_once.retval));
  if (!cache)
    {
      cache = g_hash_table_new_full (cursor_key_hash, cursor_key_equal,
                                     g_free,
                                     (GDestroyNotify) xcursor_images_destroy);

      g_object_set_qdata_full (G_OBJECT (cursor_tracker),
                               GPOINTER_TO_INT (quark_once.retval),
                               cache,
                               (GDestroyNotify) g_hash_table_unref);
    }

  return cache;
}

static void
drop_cache (MetaCursorXcursor *cursor_xcursor)
{
  GHashTable *cache = ensure_cache (cursor_xcursor);

  g_hash_table_remove_all (cache);
}

const char *
meta_cursor_get_name (ClutterCursorType cursor)
{
  switch (cursor)
    {
    case CLUTTER_CURSOR_DEFAULT:
      return "default";
    case CLUTTER_CURSOR_CONTEXT_MENU:
      return "context-menu";
    case CLUTTER_CURSOR_HELP:
      return "help";
    case CLUTTER_CURSOR_POINTER:
      return "pointer";
    case CLUTTER_CURSOR_PROGRESS:
      return "progress";
    case CLUTTER_CURSOR_WAIT:
      return "wait";
    case CLUTTER_CURSOR_CELL:
      return "cell";
    case CLUTTER_CURSOR_CROSSHAIR:
      return "crosshair";
    case CLUTTER_CURSOR_TEXT:
      return "text";
    case CLUTTER_CURSOR_VERTICAL_TEXT:
      return "vertical-text";
    case CLUTTER_CURSOR_ALIAS:
      return "alias";
    case CLUTTER_CURSOR_COPY:
      return "copy";
    case CLUTTER_CURSOR_MOVE:
      return "move";
    case CLUTTER_CURSOR_NO_DROP:
      return "no-drop";
    case CLUTTER_CURSOR_NOT_ALLOWED:
      return "not-allowed";
    case CLUTTER_CURSOR_GRAB:
      return "grab";
    case CLUTTER_CURSOR_GRABBING:
      return "grabbing";
    case CLUTTER_CURSOR_E_RESIZE:
      return "e-resize";
    case CLUTTER_CURSOR_N_RESIZE:
      return "n-resize";
    case CLUTTER_CURSOR_NE_RESIZE:
      return "ne-resize";
    case CLUTTER_CURSOR_NW_RESIZE:
      return "nw-resize";
    case CLUTTER_CURSOR_S_RESIZE:
      return "s-resize";
    case CLUTTER_CURSOR_SE_RESIZE:
      return "se-resize";
    case CLUTTER_CURSOR_SW_RESIZE:
      return "sw-resize";
    case CLUTTER_CURSOR_W_RESIZE:
      return "w-resize";
    case CLUTTER_CURSOR_EW_RESIZE:
      return "ew-resize";
    case CLUTTER_CURSOR_NS_RESIZE:
      return "ns-resize";
    case CLUTTER_CURSOR_NESW_RESIZE:
      return "nesw-resize";
    case CLUTTER_CURSOR_NWSE_RESIZE:
      return "nwse-resize";
    case CLUTTER_CURSOR_COL_RESIZE:
      return "col-resize";
    case CLUTTER_CURSOR_ROW_RESIZE:
      return "row-resize";
    case CLUTTER_CURSOR_ALL_SCROLL:
      return "all-scroll";
    case CLUTTER_CURSOR_ZOOM_IN:
      return "zoom-in";
    case CLUTTER_CURSOR_ZOOM_OUT:
      return "zoom-out";
    case CLUTTER_CURSOR_DND_ASK:
      return "dnd-ask";
    case CLUTTER_CURSOR_ALL_RESIZE:
      return "all-resize";
    case CLUTTER_CURSOR_INHERIT:
    case CLUTTER_CURSOR_NONE:
      break;
    }

  g_assert_not_reached ();
  return NULL;
}

const char *
meta_cursor_get_legacy_name (ClutterCursorType cursor)
{
  switch (cursor)
    {
    case CLUTTER_CURSOR_DEFAULT:
      return "left_ptr";
    case CLUTTER_CURSOR_CONTEXT_MENU:
      return "left_ptr";
    case CLUTTER_CURSOR_HELP:
      return "question_arrow";
    case CLUTTER_CURSOR_POINTER:
      return "hand";
    case CLUTTER_CURSOR_PROGRESS:
      return "left_ptr_watch";
    case CLUTTER_CURSOR_WAIT:
      return "watch";
    case CLUTTER_CURSOR_CELL:
      return "crosshair";
    case CLUTTER_CURSOR_CROSSHAIR:
      return "cross";
    case CLUTTER_CURSOR_TEXT:
      return "xterm";
    case CLUTTER_CURSOR_VERTICAL_TEXT:
      return "xterm";
    case CLUTTER_CURSOR_ALIAS:
      return "dnd-link";
    case CLUTTER_CURSOR_COPY:
      return "dnd-copy";
    case CLUTTER_CURSOR_MOVE:
      return "dnd-move";
    case CLUTTER_CURSOR_NO_DROP:
      return "dnd-none";
    case CLUTTER_CURSOR_NOT_ALLOWED:
      return "crossed_circle";
    case CLUTTER_CURSOR_GRAB:
      return "hand2";
    case CLUTTER_CURSOR_GRABBING:
      return "hand2";
    case CLUTTER_CURSOR_E_RESIZE:
      return "right_side";
    case CLUTTER_CURSOR_N_RESIZE:
      return "top_side";
    case CLUTTER_CURSOR_NE_RESIZE:
      return "top_right_corner";
    case CLUTTER_CURSOR_NW_RESIZE:
      return "top_left_corner";
    case CLUTTER_CURSOR_S_RESIZE:
      return "bottom_side";
    case CLUTTER_CURSOR_SE_RESIZE:
      return "bottom_right_corner";
    case CLUTTER_CURSOR_SW_RESIZE:
      return "bottom_left_corner";
    case CLUTTER_CURSOR_W_RESIZE:
      return "left_side";
    case CLUTTER_CURSOR_EW_RESIZE:
      return "h_double_arrow";
    case CLUTTER_CURSOR_NS_RESIZE:
      return "v_double_arrow";
    case CLUTTER_CURSOR_NESW_RESIZE:
      return "fd_double_arrow";
    case CLUTTER_CURSOR_NWSE_RESIZE:
      return "bd_double_arrow";
    case CLUTTER_CURSOR_COL_RESIZE:
      return "h_double_arrow";
    case CLUTTER_CURSOR_ROW_RESIZE:
      return "v_double_arrow";
    case CLUTTER_CURSOR_ALL_SCROLL:
      return "left_ptr";
    case CLUTTER_CURSOR_ZOOM_IN:
      return "left_ptr";
    case CLUTTER_CURSOR_ZOOM_OUT:
      return "left_ptr";
    case CLUTTER_CURSOR_DND_ASK:
      return "dnd-copy";
    case CLUTTER_CURSOR_ALL_RESIZE:
      return "dnd-move";
    case CLUTTER_CURSOR_INHERIT:
    case CLUTTER_CURSOR_NONE:
      break;
    }

  g_assert_not_reached ();
  return NULL;
}

static XcursorImages *
create_blank_cursor_images (void)
{
  XcursorImages *images;

  images = xcursor_images_create (1);
  images->images[0] = xcursor_image_create (1, 1);

  images->images[0]->xhot = 0;
  images->images[0]->yhot = 0;
  memset (images->images[0]->pixels, 0, sizeof(int32_t));

  return images;
}

ClutterCursorType
meta_cursor_xcursor_get_cursor (MetaCursorXcursor *cursor_xcursor)
{
  return cursor_xcursor->cursor;
}

static XcursorImages *
load_cursor_on_client (ClutterCursorType cursor,
                       int               scale)
{
  XcursorImages *xcursor_images;
  int fallback_size, i;
  const char *cursor_names[2];

  if (cursor == CLUTTER_CURSOR_NONE)
    return create_blank_cursor_images ();

  cursor_names[0] = meta_cursor_get_name (cursor);
  cursor_names[1] = meta_cursor_get_legacy_name (cursor);

  for (i = 0; i < G_N_ELEMENTS (cursor_names); i++)
    {
      xcursor_images =
        xcursor_library_load_images (cursor_names[i],
                                     meta_prefs_get_cursor_theme (),
                                     meta_prefs_get_cursor_size () * scale);
      if (xcursor_images)
        return xcursor_images;
    }

  g_warning_once ("No cursor theme available, please install a cursor theme");

  fallback_size = 24 * scale;
  xcursor_images = xcursor_images_create (1);
  xcursor_images->images[0] = xcursor_image_create (fallback_size, fallback_size);
  xcursor_images->images[0]->xhot = 0;
  xcursor_images->images[0]->yhot = 0;
  memset (xcursor_images->images[0]->pixels, 0xc0,
          fallback_size * fallback_size * sizeof (int32_t));
  return xcursor_images;
}

static void
load_from_current_xcursor_image (MetaCursorXcursor *cursor_xcursor)
{
  MetaCursorTracker *cursor_tracker = cursor_xcursor->cursor_tracker;
  MetaBackend *backend =
    meta_cursor_tracker_get_backend (cursor_tracker);
  XcursorImage *xc_image;
  int width, height, rowstride;
  CoglPixelFormat cogl_format;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  int hotspot_x, hotspot_y;

  xc_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);
  width = (int) xc_image->width;
  height = (int) xc_image->height;
  rowstride = width * 4;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  cogl_format = COGL_PIXEL_FORMAT_BGRA_8888_PRE;
#else
  cogl_format = COGL_PIXEL_FORMAT_ARGB_8888_PRE;
#endif

  clutter_backend = meta_backend_get_clutter_backend (backend);
  cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width, height,
                                           cogl_format,
                                           rowstride,
                                           (uint8_t *) xc_image->pixels,
                                           &error);
  if (!texture)
    g_warning ("Failed to allocate cursor texture: %s", error->message);

  hotspot_x = ((int) roundf ((float) xc_image->xhot /
                              cursor_xcursor->theme_scale) *
                cursor_xcursor->theme_scale);
  hotspot_y = ((int) roundf ((float) xc_image->yhot /
                              cursor_xcursor->theme_scale) *
                cursor_xcursor->theme_scale);

  g_set_object (&cursor_xcursor->texture, texture);
  cursor_xcursor->hot_x = hotspot_x;
  cursor_xcursor->hot_y = hotspot_y;
  clutter_cursor_emit_texture_changed (CLUTTER_CURSOR (cursor_xcursor));
}

void
meta_cursor_xcursor_set_theme_scale (MetaCursorXcursor *cursor_xcursor,
                                     int                theme_scale)
{
  if (cursor_xcursor->theme_scale == theme_scale)
    return;

  cursor_xcursor->theme_scale = theme_scale;
  cursor_xcursor->xcursor_images = NULL;
}

void
meta_cursor_xcursor_get_scaled_image_size (MetaCursorXcursor *cursor_xcursor,
                                           int               *width,
                                           int               *height)
{
  XcursorImage *current_image;
  int theme_size;
  int image_size;
  float effective_theme_scale;

  current_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);
  theme_size = meta_prefs_get_cursor_size ();
  image_size = current_image->size;
  effective_theme_scale = (float) theme_size / image_size;

  *width = (int) ceilf (current_image->width * effective_theme_scale);
  *height = (int) ceilf (current_image->width * effective_theme_scale);
}

static gboolean
meta_cursor_xcursor_is_animated (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  return (cursor_xcursor->xcursor_images &&
          cursor_xcursor->xcursor_images->nimage > 1);
}

XcursorImage *
meta_cursor_xcursor_get_current_image (MetaCursorXcursor *cursor_xcursor)
{
  return cursor_xcursor->xcursor_images->images[cursor_xcursor->current_frame];
}

static void
meta_cursor_xcursor_tick_frame (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  if (!clutter_cursor_is_animated (cursor))
    return;

  cursor_xcursor->current_frame++;

  if (cursor_xcursor->current_frame >= cursor_xcursor->xcursor_images->nimage)
    cursor_xcursor->current_frame = 0;

  load_from_current_xcursor_image (cursor_xcursor);
}

static unsigned int
meta_cursor_xcursor_get_current_frame_time (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  XcursorImages *xcursor_images;

  g_return_val_if_fail (clutter_cursor_is_animated (cursor), 0);

  xcursor_images = cursor_xcursor->xcursor_images;
  return xcursor_images->images[cursor_xcursor->current_frame]->delay;
}

static gboolean
load_cursor_from_theme (MetaCursorXcursor *cursor_xcursor)
{
  GHashTable *cache = ensure_cache (cursor_xcursor);
  XcursorImages *xcursor_images;
  MetaCursorXcursorKey key = {
    .cursor = cursor_xcursor->cursor,
    .theme_scale = cursor_xcursor->theme_scale,
  };

  g_assert (cursor_xcursor->cursor != CLUTTER_CURSOR_INHERIT);

  xcursor_images = g_hash_table_lookup (cache, &key);
  if (!xcursor_images)
    {
      xcursor_images = load_cursor_on_client (cursor_xcursor->cursor,
                                              cursor_xcursor->theme_scale);

      g_hash_table_insert (cache,
                           g_memdup2 (&key, sizeof (key)),
                           xcursor_images);
    }

  if (cursor_xcursor->xcursor_images == xcursor_images)
    return FALSE;

  cursor_xcursor->xcursor_images = xcursor_images;
  cursor_xcursor->current_frame = 0;
  load_from_current_xcursor_image (cursor_xcursor);
  return TRUE;
}

static gboolean
meta_cursor_xcursor_realize_texture (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  gboolean retval = cursor_xcursor->invalidated;

  if (load_cursor_from_theme (cursor_xcursor))
    retval = TRUE;

  cursor_xcursor->invalidated = FALSE;

  return retval;
}

static void
meta_cursor_xcursor_invalidate (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  cursor_xcursor->invalidated = TRUE;
}

static void
meta_cursor_xcursor_prepare_at (ClutterCursor *cursor,
                                float          best_scale,
                                int            x,
                                int            y)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  MetaCursorTracker *cursor_tracker = cursor_xcursor->cursor_tracker;
  MetaBackend *backend =
    meta_cursor_tracker_get_backend (cursor_tracker);

  if (meta_backend_is_stage_views_scaled (backend))
    {
      if (best_scale != 0.0f)
        {
          float ceiled_scale;
          int cursor_width, cursor_height;

          ceiled_scale = ceilf (best_scale);
          meta_cursor_xcursor_set_theme_scale (cursor_xcursor,
                                               (int) ceiled_scale);

          clutter_cursor_realize_texture (cursor);
          meta_cursor_xcursor_get_scaled_image_size (cursor_xcursor,
                                                     &cursor_width,
                                                     &cursor_height);
          clutter_cursor_set_viewport_dst_size (cursor,
                                                cursor_width,
                                                cursor_height);
        }
    }
  else
    {
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *logical_monitor;

      logical_monitor =
        meta_monitor_manager_get_logical_monitor_at (monitor_manager, x, y);

      /* Reload the cursor texture if the scale has changed. */
      if (logical_monitor)
        {
          meta_cursor_xcursor_set_theme_scale (cursor_xcursor,
                                               (int) logical_monitor->scale);
          clutter_cursor_set_texture_scale (cursor, 1.0f);
        }
    }
}

static ClutterColorState *
ensure_xcursor_color_state (MetaCursorTracker *cursor_tracker)
{
  ClutterColorState *color_state;
  static GOnce quark_once = G_ONCE_INIT;

  g_once (&quark_once, (GThreadFunc) g_quark_from_static_string,
          (gpointer) "-meta-cursor-xcursor-color-state");

  color_state = g_object_get_qdata (G_OBJECT (cursor_tracker),
                                    GPOINTER_TO_INT (quark_once.retval));
  if (!color_state)
    {
      MetaBackend *backend =
        meta_cursor_tracker_get_backend (cursor_tracker);
      ClutterContext *clutter_context =
        meta_backend_get_clutter_context (backend);
      ClutterColorManager *color_manager =
        clutter_context_get_color_manager (clutter_context);

      color_state = clutter_color_manager_get_default_color_state (color_manager);

      g_object_set_qdata_full (G_OBJECT (cursor_tracker),
                               GPOINTER_TO_INT (quark_once.retval),
                               g_object_ref (color_state),
                               g_object_unref);
    }

  return color_state;
}

static void
on_prefs_changed (ClutterCursor *cursor,
                  gpointer       user_data)
{
  MetaCursorXcursor *cursor_xcursor =
    META_CURSOR_XCURSOR (user_data);

  drop_cache (cursor_xcursor);
  cursor_xcursor->xcursor_images = NULL;
}

MetaCursorXcursor *
meta_cursor_xcursor_new (ClutterCursorType  cursor_type,
                         MetaCursorTracker *cursor_tracker)
{
  MetaCursorXcursor *cursor_xcursor;
  ClutterColorState *color_state;

  color_state = ensure_xcursor_color_state (cursor_tracker);

  cursor_xcursor = g_object_new (META_TYPE_CURSOR_XCURSOR,
                                 "color-state", color_state,
                                 NULL);
  cursor_xcursor->cursor = cursor_type;
  cursor_xcursor->cursor_tracker = cursor_tracker;

  g_signal_connect_object (cursor_tracker, "cursor-prefs-changed",
                           G_CALLBACK (on_prefs_changed),
                           cursor_xcursor,
                           G_CONNECT_DEFAULT);

  return cursor_xcursor;
}

static CoglTexture *
meta_cursor_xcursor_get_texture (ClutterCursor *cursor,
                                 int           *hot_x,
                                 int           *hot_y)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  if (hot_x)
    *hot_x = cursor_xcursor->hot_x;
  if (hot_y)
    *hot_y = cursor_xcursor->hot_y;

  return cursor_xcursor->texture;
}

static void
meta_cursor_xcursor_finalize (GObject *object)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (object);

  g_clear_object (&cursor_xcursor->texture);

  G_OBJECT_CLASS (meta_cursor_xcursor_parent_class)->finalize (object);
}

static void
meta_cursor_xcursor_init (MetaCursorXcursor *cursor_xcursor)
{
  cursor_xcursor->theme_scale = 1;
}

static void
meta_cursor_xcursor_class_init (MetaCursorXcursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterCursorClass *cursor_class = CLUTTER_CURSOR_CLASS (klass);

  object_class->finalize = meta_cursor_xcursor_finalize;

  cursor_class->realize_texture = meta_cursor_xcursor_realize_texture;
  cursor_class->invalidate = meta_cursor_xcursor_invalidate;
  cursor_class->is_animated = meta_cursor_xcursor_is_animated;
  cursor_class->tick_frame = meta_cursor_xcursor_tick_frame;
  cursor_class->get_current_frame_time =
    meta_cursor_xcursor_get_current_frame_time;
  cursor_class->prepare_at = meta_cursor_xcursor_prepare_at;
  cursor_class->get_texture = meta_cursor_xcursor_get_texture;
}
