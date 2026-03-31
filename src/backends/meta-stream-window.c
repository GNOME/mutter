/*
 * Copyright (C) 2018-2026 Red Hat Inc.
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

#include "backends/meta-stream-window.h"

#include "backends/meta-eis.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-screen-cast-window.h"
#include "backends/meta-stream-source-window.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"

enum
{
  PROP_0,

  PROP_WINDOW,
};

struct _MetaStreamWindow
{
  MetaStream parent;

  MetaWindow *window;

  int stream_width;
  int stream_height;
  int logical_width;
  int logical_height;

  unsigned long window_unmanaged_handler_id;
};

static void
meta_stream_window_init_initable_iface (GInitableIface *iface);

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamWindow,
                         meta_stream_window,
                         META_TYPE_STREAM,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_stream_window_init_initable_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

static MetaStreamSource *
meta_stream_window_create_source (MetaStream  *stream,
                                  GError     **error)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (stream);
  MetaStreamSourceWindow *source_window;

  source_window = meta_stream_source_window_new (stream_window,
                                                 error);
  if (!source_window)
    return NULL;

  return META_STREAM_SOURCE (source_window);
}

static gboolean
meta_stream_window_transform_position (MetaStream *stream,
                                       double      stream_x,
                                       double      stream_y,
                                       double     *x,
                                       double     *y)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (stream);
  MetaScreenCastWindow *screen_cast_window =
    META_SCREEN_CAST_WINDOW (meta_window_actor_from_window (stream_window->window));

  meta_screen_cast_window_transform_relative_position (screen_cast_window,
                                                       stream_x,
                                                       stream_y,
                                                       x,
                                                       y);
  return TRUE;
}

static void
on_window_unmanaged (MetaStreamWindow *stream_window)
{
  meta_stream_stop (META_STREAM (stream_window));
}

static void
meta_stream_window_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      stream_window->window = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_window_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_value_set_object (value, stream_window->window);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_window_finalize (GObject *object)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (object);
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (stream_window->window);
  if (window_actor)
    {
      MetaScreenCastWindow *screen_cast_window;

      screen_cast_window = META_SCREEN_CAST_WINDOW (window_actor);
      meta_screen_cast_window_dec_usage (screen_cast_window);
    }

  g_clear_signal_handler (&stream_window->window_unmanaged_handler_id,
                          stream_window->window);

  G_OBJECT_CLASS (meta_stream_window_parent_class)->finalize (object);
}

static gboolean
meta_stream_window_initable_init (GInitable     *initable,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (initable);
  MetaStream *stream = META_STREAM (initable);
  MetaBackend *backend = meta_stream_get_backend (stream);
  MetaWindow *window = stream_window->window;
  MetaScreenCastWindow *screen_cast_window =
    META_SCREEN_CAST_WINDOW (meta_window_actor_from_window (window));
  MetaLogicalMonitor *logical_monitor;
  int scale;

  logical_monitor = meta_window_get_main_logical_monitor (window);
  if (!logical_monitor)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Main logical monitor not found");
      return FALSE;
    }

  stream_window->window_unmanaged_handler_id =
    g_signal_connect_swapped (window, "unmanaged",
                              G_CALLBACK (on_window_unmanaged),
                              stream_window);

  if (meta_backend_is_stage_views_scaled (backend))
    scale = (int) ceilf (meta_logical_monitor_get_scale (logical_monitor));
  else
    scale = 1;

  /* We cannot set the stream size to the exact size of the window, because
   * windows can be resized, whereas streams cannot.
   * So we set a size equals to the size of the logical monitor for the window.
   */
  stream_window->logical_width = logical_monitor->rect.width;
  stream_window->logical_height = logical_monitor->rect.height;
  stream_window->stream_width = logical_monitor->rect.width * scale;
  stream_window->stream_height = logical_monitor->rect.height * scale;

  meta_screen_cast_window_inc_usage (screen_cast_window);

  return TRUE;
}

static void
meta_stream_window_init_initable_iface (GInitableIface *iface)
{
  iface->init = meta_stream_window_initable_init;
}

static gboolean
meta_stream_window_is_standalone (MetaEisViewport *viewport)
{
  return TRUE;
}

static const char *
meta_stream_window_get_mapping_id (MetaEisViewport *viewport)
{
  MetaStream *stream = META_STREAM (viewport);

  return meta_stream_get_mapping_id (stream);
}

static gboolean
meta_stream_window_get_position (MetaEisViewport *viewport,
                                 int             *out_x,
                                 int             *out_y)
{
  return FALSE;
}

static void
meta_stream_window_get_size (MetaEisViewport *viewport,
                             int             *out_width,
                             int             *out_height)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (viewport);

  *out_width = stream_window->stream_width;
  *out_height = stream_window->stream_height;
}

static double
meta_stream_window_get_physical_scale (MetaEisViewport *viewport)
{
  return 1.0;
}

static gboolean
meta_stream_window_transform_coordinate (MetaEisViewport *viewport,
                                         double           x,
                                         double           y,
                                         double          *out_x,
                                         double          *out_y)
{
  MetaStreamWindow *stream_window = META_STREAM_WINDOW (viewport);
  MetaScreenCastWindow *screen_cast_window =
    META_SCREEN_CAST_WINDOW (meta_window_actor_from_window (stream_window->window));

  meta_screen_cast_window_transform_relative_position (screen_cast_window,
                                                       x,
                                                       y,
                                                       out_x,
                                                       out_y);
  return TRUE;
}

static void
meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface)
{
  eis_viewport_iface->is_standalone = meta_stream_window_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_stream_window_get_mapping_id;
  eis_viewport_iface->get_position = meta_stream_window_get_position;
  eis_viewport_iface->get_size = meta_stream_window_get_size;
  eis_viewport_iface->get_physical_scale = meta_stream_window_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_stream_window_transform_coordinate;
}

static void
meta_stream_window_class_init (MetaStreamWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamClass *stream_class = META_STREAM_CLASS (klass);

  object_class->set_property = meta_stream_window_set_property;
  object_class->get_property = meta_stream_window_get_property;
  object_class->finalize = meta_stream_window_finalize;

  stream_class->create_source = meta_stream_window_create_source;
  stream_class->transform_position = meta_stream_window_transform_position;

  g_object_class_install_property (object_class,
                                   PROP_WINDOW,
                                   g_param_spec_object ("window", NULL, NULL,
                                                        META_TYPE_WINDOW,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
meta_stream_window_init (MetaStreamWindow *stream_window)
{
}

MetaWindow *
meta_stream_window_get_window (MetaStreamWindow *stream_window)
{
  return stream_window->window;
}

int
meta_stream_window_get_width (MetaStreamWindow *stream_window)
{
  return stream_window->stream_width;
}

int
meta_stream_window_get_height (MetaStreamWindow *stream_window)
{
  return stream_window->stream_height;
}

MetaStreamWindow *
meta_stream_window_new (MetaBackend           *backend,
                        MetaWindow            *window,
                        MetaStreamCursorMode   cursor_mode,
                        GError               **error)
{
  return g_initable_new (META_TYPE_STREAM_WINDOW,
                         NULL,
                         error,
                         "backend", backend,
                         "cursor-mode", cursor_mode,
                         "window", window,
                         "is-configured", TRUE,
                         NULL);
}
