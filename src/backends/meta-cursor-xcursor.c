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
#include "backends/meta-logical-monitor-private.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/prefs.h"
#include "meta/util.h"
#include "third_party/xcursor/xcursor.h"

typedef struct _MetaCursorImageData
{
  int scale;
  XcursorImages *xcursor_images;
} MetaCursorImageData;

struct _MetaCursorXcursor
{
  ClutterCursor parent;

  CoglTexture *texture;
  int hot_x;
  int hot_y;

  ClutterCursorType cursor;

  GArray *cursor_images;

  int current_frame;
  XcursorImages *xcursor_images;

  int theme_scale;
  gboolean invalidated;
  gboolean texture_invalidated;
};

static XcursorImage * meta_cursor_xcursor_get_current_image (MetaCursorXcursor *cursor_xcursor);

G_DEFINE_TYPE_WITH_CODE (MetaCursorXcursor, meta_cursor_xcursor,
                         META_TYPE_CURSOR,
                         g_io_extension_point_implement (META_CURSOR_EXTENSION_POINT_NAME,
                                                         g_define_type_id, "xcursor", 0))

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
  images->nimage = 1;
  memset (images->images[0]->pixels, 0, sizeof(int32_t));

  return images;
}

static XcursorImages *
load_cursor_on_client (MetaCursorXcursor *cursor_xcursor,
                       ClutterCursorType  cursor,
                       int                scale)
{
  MetaCursor *meta_cursor = META_CURSOR (cursor_xcursor);
  XcursorImages *xcursor_images;
  int fallback_size, i;
  const char *cursor_names[2];

  if (cursor == CLUTTER_CURSOR_NONE)
    return create_blank_cursor_images ();

  cursor_names[0] = clutter_cursor_type_to_name (cursor);
  cursor_names[1] = meta_cursor_get_legacy_name (cursor);

  for (i = 0; i < G_N_ELEMENTS (cursor_names); i++)
    {
      xcursor_images =
        xcursor_library_load_images (cursor_names[i],
                                     meta_cursor_get_theme_name (meta_cursor),
                                     meta_cursor_get_size (meta_cursor) * scale);
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
load_texture_from_current_xcursor_image (MetaCursorXcursor *cursor_xcursor)
{
  MetaBackend *backend = meta_cursor_get_backend (META_CURSOR (cursor_xcursor));
  XcursorImage *xc_image;
  int width, height, rowstride;
  CoglPixelFormat cogl_format;
  ClutterBackend *clutter_backend;
  CoglContext *cogl_context;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (GError) error = NULL;

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

  g_set_object (&cursor_xcursor->texture, texture);
}

static void
meta_cursor_xcursor_set_theme_scale (MetaCursorXcursor *cursor_xcursor,
                                     int                theme_scale)
{
  if (cursor_xcursor->theme_scale == theme_scale)
    return;

  cursor_xcursor->theme_scale = theme_scale;
  cursor_xcursor->xcursor_images = NULL;
  clutter_cursor_invalidate (CLUTTER_CURSOR (cursor_xcursor));
}

static void
meta_cursor_xcursor_get_scaled_image_size (MetaCursorXcursor *cursor_xcursor,
                                           int               *width,
                                           int               *height)
{
  XcursorImage *current_image;
  int theme_size;
  int image_size;
  float effective_theme_scale;

  current_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);
  theme_size = meta_cursor_get_size (META_CURSOR (cursor_xcursor));
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

static XcursorImage *
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

  clutter_cursor_invalidate (cursor);
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

static void
load_cursor_from_theme (MetaCursorXcursor *cursor_xcursor)
{
  XcursorImages *xcursor_images = NULL;
  unsigned int i;
  ClutterCursorType cursor_type;

  cursor_type = clutter_cursor_get_cursor_type (CLUTTER_CURSOR (cursor_xcursor));

  g_assert (cursor_type != CLUTTER_CURSOR_INHERIT);

  for (i = 0; i < cursor_xcursor->cursor_images->len; i++)
    {
      MetaCursorImageData *image_data;

      image_data = &g_array_index (cursor_xcursor->cursor_images,
                                   MetaCursorImageData, i);
      if (image_data->scale == cursor_xcursor->theme_scale)
        xcursor_images = image_data->xcursor_images;
    }

  if (!xcursor_images)
    {
      MetaCursorImageData new_cursor;

      new_cursor.scale = cursor_xcursor->theme_scale;
      new_cursor.xcursor_images =
        load_cursor_on_client (cursor_xcursor,
                               cursor_type,
                               new_cursor.scale);

      xcursor_images = new_cursor.xcursor_images;
      g_array_append_val (cursor_xcursor->cursor_images, new_cursor);
    }

  if (cursor_xcursor->xcursor_images == xcursor_images)
    return;

  cursor_xcursor->xcursor_images = xcursor_images;
  cursor_xcursor->current_frame = 0;
}

static void
meta_cursor_xcursor_realize (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  XcursorImage *xc_image;

  if (!cursor_xcursor->invalidated)
    return;

  load_cursor_from_theme (cursor_xcursor);
  cursor_xcursor->invalidated = FALSE;

  xc_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);
  cursor_xcursor->hot_x = ((int) roundf ((float) xc_image->xhot /
                                         cursor_xcursor->theme_scale) *
                           cursor_xcursor->theme_scale);
  cursor_xcursor->hot_y = ((int) roundf ((float) xc_image->yhot /
                                         cursor_xcursor->theme_scale) *
                           cursor_xcursor->theme_scale);
}

static void
meta_cursor_xcursor_invalidate (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  cursor_xcursor->invalidated = TRUE;
  cursor_xcursor->texture_invalidated = TRUE;
  clutter_cursor_emit_image_changed (CLUTTER_CURSOR (cursor_xcursor));
}

static void
meta_cursor_xcursor_prepare_at (ClutterCursor *cursor,
                                float          best_scale,
                                float          x,
                                float          y)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  MetaBackend *backend = meta_cursor_get_backend (META_CURSOR (cursor));

  if (meta_backend_is_stage_views_scaled (backend))
    {
      if (best_scale != 0.0f)
        {
          float ceiled_scale;
          int cursor_width, cursor_height;

          ceiled_scale = ceilf (best_scale);
          meta_cursor_xcursor_set_theme_scale (cursor_xcursor,
                                               (int) ceiled_scale);

          meta_cursor_xcursor_realize (cursor);
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
          clutter_cursor_set_scale (cursor, 1.0f);

          clutter_cursor_reset_viewport_dst_size (cursor);
        }
    }
}

static void
meta_cursor_xcursor_get_geometry (ClutterCursor *cursor,
                                  int           *width,
                                  int           *height,
                                  int           *hot_x,
                                  int           *hot_y)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  XcursorImage *xc_image;

  meta_cursor_xcursor_realize (cursor);
  xc_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);

  if (width)
    *width = xc_image->width;
  if (height)
    *height = xc_image->height;

  if (hot_x)
    *hot_x = cursor_xcursor->hot_x;
  if (hot_y)
    *hot_y = cursor_xcursor->hot_y;
}

static uint8_t *
meta_cursor_xcursor_get_data (ClutterCursor *cursor,
                              int           *out_stride)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);
  XcursorImage *xc_image;

  meta_cursor_xcursor_realize (cursor);
  xc_image = meta_cursor_xcursor_get_current_image (cursor_xcursor);

  if (out_stride)
    *out_stride = xc_image->width * sizeof (XcursorPixel);

  return (uint8_t *) xc_image->pixels;
}

static CoglTexture *
meta_cursor_xcursor_get_texture (ClutterCursor *cursor)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (cursor);

  if (cursor_xcursor->texture_invalidated)
    {
      meta_cursor_xcursor_realize (cursor);
      load_texture_from_current_xcursor_image (cursor_xcursor);
      cursor_xcursor->texture_invalidated = FALSE;
    }

  return cursor_xcursor->texture;
}

static void
meta_cursor_xcursor_finalize (GObject *object)
{
  MetaCursorXcursor *cursor_xcursor = META_CURSOR_XCURSOR (object);

  g_clear_object (&cursor_xcursor->texture);
  g_clear_pointer (&cursor_xcursor->cursor_images, g_array_unref);

  G_OBJECT_CLASS (meta_cursor_xcursor_parent_class)->finalize (object);
}

static void
clear_cursor_image_data (MetaCursorImageData *data)
{
  g_clear_pointer (&data->xcursor_images, xcursor_images_destroy);
}

static void
meta_cursor_xcursor_init (MetaCursorXcursor *cursor_xcursor)
{
  cursor_xcursor->invalidated = TRUE;
  cursor_xcursor->texture_invalidated = TRUE;
  cursor_xcursor->theme_scale = 1;
  cursor_xcursor->cursor_images =
    g_array_new (FALSE, FALSE, sizeof (MetaCursorImageData));
  g_array_set_clear_func (cursor_xcursor->cursor_images,
                          (GDestroyNotify) clear_cursor_image_data);
}

static void
meta_cursor_xcursor_class_init (MetaCursorXcursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterCursorClass *cursor_class = CLUTTER_CURSOR_CLASS (klass);

  object_class->finalize = meta_cursor_xcursor_finalize;

  cursor_class->invalidate = meta_cursor_xcursor_invalidate;
  cursor_class->is_animated = meta_cursor_xcursor_is_animated;
  cursor_class->tick_frame = meta_cursor_xcursor_tick_frame;
  cursor_class->get_current_frame_time =
    meta_cursor_xcursor_get_current_frame_time;
  cursor_class->prepare_at = meta_cursor_xcursor_prepare_at;
  cursor_class->get_geometry = meta_cursor_xcursor_get_geometry;
  cursor_class->get_data = meta_cursor_xcursor_get_data;
  cursor_class->get_texture = meta_cursor_xcursor_get_texture;
}
