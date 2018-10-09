/*
 * Copyright (C) 2018 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-screen-cast-window-stream.h"
#include "backends/meta-screen-cast-window-stream-src.h"
#include "core/window-private.h"
#include "compositor/compositor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-logical-monitor.h"

enum
{
  PROP_0,

  PROP_WINDOW,
};

struct _MetaScreenCastWindowStream
{
  MetaScreenCastStream parent;

  MetaWindow *window;

  int stream_width;
  int stream_height;

  unsigned long window_unmanaged_handler_id;
};

G_DEFINE_TYPE (MetaScreenCastWindowStream,
               meta_screen_cast_window_stream,
               META_TYPE_SCREEN_CAST_STREAM)

MetaWindow *
meta_screen_cast_window_stream_get_window (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->window;
}

int
meta_screen_cast_window_stream_get_width (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->stream_width;
}

int
meta_screen_cast_window_stream_get_height (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->stream_height;
}

static ClutterActor *
get_clutter_actor (MetaScreenCastWindowStream *window_stream)
{
  MetaWindow *window = window_stream->window;
  MetaWindowActor *window_actor;

  window = window_stream->window;
  window_actor = meta_window_actor_from_window (window);

  return meta_window_actor_get_texture (window_actor);
}

gboolean
meta_screen_cast_window_stream_get_crop_rect (MetaScreenCastWindowStream *window_stream,
                                              MetaRectangle              *crop_rect)
{
  MetaWindow *window;
  ClutterActor *clutter_actor;
  cairo_rectangle_int_t buffer_rect;
  cairo_rectangle_int_t stream_rect;
  double scale_x, scale_y;

  clutter_actor = get_clutter_actor (window_stream);
  if (!clutter_actor)
    return FALSE;

  window = window_stream->window;
  meta_window_get_buffer_rect (window, &buffer_rect);

  clutter_actor_get_scale (clutter_actor, &scale_x, &scale_y);

  buffer_rect.x = (int) floor ((window->rect.x - buffer_rect.x) / scale_x);
  buffer_rect.y = (int) floor ((window->rect.y - buffer_rect.y) / scale_y);
  buffer_rect.width = (int) ceil (window->rect.width / scale_x);
  buffer_rect.height = (int) ceil (window->rect.height / scale_y);

  stream_rect.x = 0;
  stream_rect.y = 0;
  stream_rect.width = window_stream->stream_width;
  stream_rect.height = window_stream->stream_height;

  meta_rectangle_intersect (&buffer_rect, &stream_rect, crop_rect);

  return TRUE;
}


MetaScreenCastWindowStream *
meta_screen_cast_window_stream_new (GDBusConnection  *connection,
                                    MetaWindow       *window,
                                    GError          **error)
{
  MetaScreenCastWindowStream *window_stream;
  MetaLogicalMonitor *logical_monitor;
  float scale;

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (!logical_monitor)
    return NULL;

  window_stream = g_initable_new (META_TYPE_SCREEN_CAST_WINDOW_STREAM,
                                  NULL,
                                  error,
                                  "connection", connection,
                                  "window", window,
                                  NULL);
  if (!window_stream)
    return NULL;

  window_stream->window = window;
  /* We cannot set the stream size to the exact size of the window, because
   * windows can be resized, whereas streams cannot.
   * So we set a size equals to the size of the logical monitor for the window.
   */
  scale = ceil (meta_logical_monitor_get_scale (logical_monitor));
  window_stream->stream_width = roundf (logical_monitor->rect.width * scale);
  window_stream->stream_height = roundf (logical_monitor->rect.height * scale);

  return window_stream;
}

static MetaScreenCastStreamSrc *
meta_screen_cast_window_stream_create_src (MetaScreenCastStream  *stream,
                                           GError               **error)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);
  MetaScreenCastWindowStreamSrc *window_stream_src;

  window_stream_src = meta_screen_cast_window_stream_src_new (window_stream,
                                                              error);
  if (!window_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (window_stream_src);
}

static void
meta_screen_cast_window_stream_set_parameters (MetaScreenCastStream *stream,
                                               GVariantBuilder      *parameters_builder)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);
  ClutterActor *clutter_actor;
  int width, height;

  clutter_actor = get_clutter_actor (window_stream);
  if (clutter_actor)
    {
      width = (int) clutter_actor_get_width (clutter_actor);
      height = (int) clutter_actor_get_height (clutter_actor);
    }
  else
    {
      width = window_stream->stream_width;
      height = window_stream->stream_height;
    }

  g_variant_builder_add (parameters_builder, "{sv}",
                         "position",
                         g_variant_new ("(ii)",
                                        0, 0));
  g_variant_builder_add (parameters_builder, "{sv}",
                         "size",
                         g_variant_new ("(ii)",
                                        width,
                                        height));
}

static void
meta_screen_cast_window_stream_transform_position (MetaScreenCastStream *stream,
                                                   double                stream_x,
                                                   double                stream_y,
                                                   double               *x,
                                                   double               *y)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (stream);
  MetaWindow *window;
  MetaRectangle crop_rect;
  MetaWindowActor *window_actor;
  MetaSurfaceActor *surface_actor;
  MetaShapedTexture *stex;
  ClutterVertex v1, v2;
  float dest_x, dest_y;

  window = window_stream->window;
  window_actor = meta_window_actor_from_window (window);
  surface_actor = meta_window_actor_get_surface (window_actor);
  stex = meta_surface_actor_get_texture (surface_actor);

  meta_screen_cast_window_stream_get_crop_rect (window_stream, &crop_rect);
  /* We work in buffer coordinates */
  dest_x = CLAMP (crop_rect.x + stream_x,
                  crop_rect.x,
                  crop_rect.x + crop_rect.width);
  dest_y = CLAMP (crop_rect.y + stream_y,
                  crop_rect.y,
                  crop_rect.y + crop_rect.height);

  /* Translate to clutter actor coordinates */
  v1.x = dest_x;
  v1.y = dest_y;
  v1.z = 0;
  clutter_actor_apply_transform_to_point (CLUTTER_ACTOR (stex), &v1, &v2);

  *x = (double) v2.x;
  *y = (double) v2.y;
}

static void
on_window_unmanaged (MetaScreenCastWindowStream *window_stream)
{
  meta_screen_cast_stream_close (META_SCREEN_CAST_STREAM (window_stream));
}

static void
add_unmanage_handler (MetaScreenCastWindowStream *window_stream)
{
  g_return_if_fail (window_stream->window_unmanaged_handler_id == 0);

  window_stream->window_unmanaged_handler_id =
    g_signal_connect_swapped (window_stream->window, "unmanaged",
                              G_CALLBACK (on_window_unmanaged),
                              window_stream);
}

static void
remove_unmanage_handler (MetaScreenCastWindowStream *window_stream)
{
  if (window_stream->window_unmanaged_handler_id)
    {
      g_signal_handler_disconnect (window_stream->window,
                                   window_stream->window_unmanaged_handler_id);
      window_stream->window_unmanaged_handler_id = 0;
    }
}

static void
meta_screen_cast_window_stream_constructed (GObject *object)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  add_unmanage_handler (window_stream);
}

static void
meta_screen_cast_window_stream_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      window_stream->window = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_window_stream_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_value_set_object (value, window_stream->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_window_stream_finalize (GObject *object)
{
  MetaScreenCastWindowStream *window_stream =
    META_SCREEN_CAST_WINDOW_STREAM (object);

  remove_unmanage_handler (window_stream);
  g_clear_object (&window_stream->window);

  G_OBJECT_CLASS (meta_screen_cast_window_stream_parent_class)->finalize (object);
}

static void
meta_screen_cast_window_stream_init (MetaScreenCastWindowStream *window_stream)
{
}

static void
meta_screen_cast_window_stream_class_init (MetaScreenCastWindowStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  object_class->constructed = meta_screen_cast_window_stream_constructed;
  object_class->set_property = meta_screen_cast_window_stream_set_property;
  object_class->get_property = meta_screen_cast_window_stream_get_property;
  object_class->finalize = meta_screen_cast_window_stream_finalize;

  stream_class->create_src = meta_screen_cast_window_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_window_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_window_stream_transform_position;

  g_object_class_install_property (object_class,
                                   PROP_WINDOW,
                                   g_param_spec_object ("window",
                                                        "window",
                                                        "MetaWindow",
                                                        META_TYPE_WINDOW,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}
