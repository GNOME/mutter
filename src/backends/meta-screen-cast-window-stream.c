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

enum
{
  PROP_0,

  PROP_WINDOW,
};

struct _MetaScreenCastWindowStream
{
  MetaScreenCastStream parent;

  MetaWindow *window;

  int initial_width;
  int initial_height;
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
  return window_stream->initial_width;
}

int
meta_screen_cast_window_stream_get_height (MetaScreenCastWindowStream *window_stream)
{
  return window_stream->initial_height;
}

MetaScreenCastWindowStream *
meta_screen_cast_window_stream_new (GDBusConnection *connection,
                                    MetaWindow      *window,
                                    GError         **error)
{
  MetaScreenCastWindowStream *window_stream;

  window_stream = g_initable_new (META_TYPE_SCREEN_CAST_WINDOW_STREAM,
                                  NULL,
                                  error,
                                  "connection", connection,
                                  "window", window,
                                  NULL);
  if (!window_stream)
    return NULL;

  window_stream->window = window;
  window_stream->initial_width = window->rect.width;
  window_stream->initial_height = window->rect.height;

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

  g_variant_builder_add (parameters_builder, "{sv}",
                         "position",
                         g_variant_new ("(ii)",
                                        0, 0));
  g_variant_builder_add (parameters_builder, "{sv}",
                         "size",
                         g_variant_new ("(ii)",
                                        window_stream->initial_width,
                                        window_stream->initial_height));
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

  window = window_stream->window;

  *x = CLAMP (window->rect.x + stream_x,
              window->rect.x,
              window->rect.x + window->rect.width);
  *y = CLAMP (window->rect.y + stream_y,
              window->rect.y,
              window->rect.y + window->rect.height);
}

static void
on_window_unmanaged (MetaScreenCastWindowStream *window_stream)
{
  meta_screen_cast_stream_close (META_SCREEN_CAST_STREAM (window_stream));
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
      g_set_object (&window_stream->window, g_value_get_object (value));
      g_signal_connect_swapped (window_stream->window, "unmanaged",
                                G_CALLBACK (on_window_unmanaged),
                                window_stream);
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
