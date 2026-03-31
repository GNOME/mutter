/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat Inc.
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

#include "backends/meta-screen-cast-stream.h"

#include "backends/meta-eis.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-remote-desktop-session.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-stream-area.h"
#include "backends/meta-stream-monitor.h"
#include "backends/meta-stream-source.h"
#include "backends/meta-stream-virtual.h"
#include "backends/meta-stream-window.h"
#include "backends/meta-stream.h"

#include "meta-private-enum-types.h"

#define META_SCREEN_CAST_STREAM_DBUS_IFACE "org.gnome.Mutter.ScreenCast.Stream"
#define META_SCREEN_CAST_STREAM_DBUS_PATH "/org/gnome/Mutter/ScreenCast/Stream"

enum
{
  PROP_0,

  PROP_SESSION,
  PROP_CONNECTION,
  PROP_STREAM,
  PROP_FLAGS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaScreenCastStream
{
  MetaDBusScreenCastStreamSkeleton parent;
};

typedef struct _MetaScreenCastStreamPrivate
{
  MetaScreenCastSession *session;

  GDBusConnection *connection;
  char *object_path;

  MetaStream *stream;

  MetaScreenCastFlag flags;

} MetaScreenCastStreamPrivate;

static void
meta_screen_cast_stream_init_iface (MetaDBusScreenCastStreamIface *iface);

static void
meta_screen_cast_stream_init_initable_iface (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastStream,
                         meta_screen_cast_stream,
                         META_DBUS_TYPE_SCREEN_CAST_STREAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SCREEN_CAST_STREAM,
                                                meta_screen_cast_stream_init_iface)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                meta_screen_cast_stream_init_initable_iface)
                         G_ADD_PRIVATE (MetaScreenCastStream))

static void
on_stream_closed (MetaStream           *stream,
                  MetaScreenCastStream *screen_cast_stream)
{
  g_signal_emit (screen_cast_stream, signals[CLOSED], 0);
}

static void
on_stream_ready (MetaStream           *stream,
                 MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);
  GDBusConnection *connection = priv->connection;
  MetaStreamSource *source;
  char *peer_name;
  uint32_t node_id;

  peer_name = meta_screen_cast_session_get_peer_name (priv->session);

  source = meta_stream_get_source (stream);
  g_return_if_fail (source);

  node_id = meta_stream_source_get_node_id (source);
  g_dbus_connection_emit_signal (connection,
                                 peer_name,
                                 priv->object_path,
                                 META_SCREEN_CAST_STREAM_DBUS_IFACE,
                                 "PipeWireStreamAdded",
                                 g_variant_new ("(u)", node_id),
                                 NULL);
}

MetaScreenCastSession *
meta_screen_cast_stream_get_session (MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  return priv->session;
}

gboolean
meta_screen_cast_stream_start (MetaScreenCastStream  *screen_cast_stream,
                               GError               **error)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  if (meta_stream_is_started (priv->stream))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Stream already started");
      return FALSE;
    }

  if (!meta_stream_start (priv->stream, error))
    return FALSE;

  g_signal_connect (priv->stream, "ready", G_CALLBACK (on_stream_ready),
                    screen_cast_stream);
  g_signal_connect (priv->stream, "closed", G_CALLBACK (on_stream_closed),
                    screen_cast_stream);

  return TRUE;
}

void
meta_screen_cast_stream_close (MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  meta_stream_stop (priv->stream);
}

char *
meta_screen_cast_stream_get_object_path (MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  return priv->object_path;
}

MetaScreenCastFlag
meta_screen_cast_stream_get_flags (MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  return priv->flags;
}

static void
meta_screen_cast_stream_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  switch (prop_id)
    {
    case PROP_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_STREAM:
      priv->stream = g_value_dup_object (value);
      break;
    case PROP_FLAGS:
      priv->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_STREAM:
      g_value_set_object (value, priv->stream);
      break;
    case PROP_FLAGS:
      g_value_set_flags (value, priv->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_stream_dispose (GObject *object)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (object);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  if (priv->stream && meta_stream_is_started (priv->stream))
    meta_screen_cast_stream_close (screen_cast_stream);

  g_clear_object (&priv->stream);
  g_clear_pointer (&priv->object_path, g_free);

  G_OBJECT_CLASS (meta_screen_cast_stream_parent_class)->dispose (object);
}

static gboolean
check_permission (MetaScreenCastStream  *stream,
                  GDBusMethodInvocation *invocation)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (stream);
  char *peer_name;

  peer_name = meta_screen_cast_session_get_peer_name (priv->session);
  return g_strcmp0 (peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static gboolean
handle_start (MetaDBusScreenCastStream *skeleton,
              GDBusMethodInvocation    *invocation)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (skeleton);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);
  g_autoptr (GError) error = NULL;

  if (!check_permission (screen_cast_stream, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (!meta_screen_cast_session_is_active (priv->session))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to start stream: "
                                             "session not started");
      return TRUE;
    }

  if (!meta_screen_cast_stream_start (screen_cast_stream, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to start stream: %s",
                                             error->message);
      return TRUE;
    }

  meta_dbus_screen_cast_stream_complete_start (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_stop (MetaDBusScreenCastStream *skeleton,
             GDBusMethodInvocation    *invocation)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (skeleton);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  if (!check_permission (screen_cast_stream, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_stream_is_started (priv->stream))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Stream not started");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_screen_cast_stream_close (screen_cast_stream);

  meta_dbus_screen_cast_stream_complete_stop (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_screen_cast_stream_init_iface (MetaDBusScreenCastStreamIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
}

static void
set_stream_parameters (MetaScreenCastStream *screen_cast_stream,
                       GVariantBuilder      *parameters_builder)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);
  MetaStream *stream = priv->stream;

  if (META_IS_STREAM_MONITOR (stream))
    {
      MtkRectangle geometry;
      MetaMonitor *monitor;
      const char *output_name;

      meta_stream_monitor_get_geometry (META_STREAM_MONITOR (stream),
                                        &geometry);
      g_variant_builder_add (parameters_builder, "{sv}",
                             "position",
                             g_variant_new ("(ii)",
                                            geometry.x,
                                            geometry.y));
      g_variant_builder_add (parameters_builder, "{sv}",
                             "size",
                             g_variant_new ("(ii)",
                                            geometry.width,
                                            geometry.height));

      monitor = meta_stream_monitor_get_monitor (META_STREAM_MONITOR (stream));
      output_name = meta_monitor_get_connector (monitor);
      g_variant_builder_add (parameters_builder, "{sv}", "output-name",
                             g_variant_new ("s", output_name));
    }
  else if (META_IS_STREAM_AREA (stream))
    {
      MtkRectangle area;

      meta_stream_area_get_area (META_STREAM_AREA (stream), &area);
      g_variant_builder_add (parameters_builder, "{sv}",
                             "size",
                             g_variant_new ("(ii)", area.width, area.height));
    }
  else if (META_IS_STREAM_WINDOW (stream))
    {
      int width, height;

      width = meta_stream_window_get_width (META_STREAM_WINDOW (stream));
      height = meta_stream_window_get_width (META_STREAM_WINDOW (stream));
      g_variant_builder_add (parameters_builder, "{sv}",
                             "size",
                             g_variant_new ("(ii)", width, height));
    }
}

static gboolean
meta_screen_cast_stream_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
  MetaScreenCastStream *screen_cast_stream = META_SCREEN_CAST_STREAM (initable);
  MetaDBusScreenCastStream *skeleton =
    META_DBUS_SCREEN_CAST_STREAM (screen_cast_stream);
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);
  MetaRemoteDesktopSession *remote_desktop_session;
  GVariantBuilder parameters_builder;
  GVariant *parameters_variant;
  static unsigned int global_stream_number = 0;

  g_variant_builder_init (&parameters_builder, G_VARIANT_TYPE_VARDICT);
  set_stream_parameters (screen_cast_stream, &parameters_builder);

  remote_desktop_session =
    meta_screen_cast_session_get_remote_desktop_session (priv->session);
  if (remote_desktop_session)
    {
      MetaEis *eis =
        meta_remote_desktop_session_get_eis (remote_desktop_session);
      const char *mapping_id;

      meta_stream_map_input (priv->stream, eis);

      mapping_id = meta_stream_get_mapping_id (priv->stream);
      g_variant_builder_add (&parameters_builder, "{sv}",
                             "mapping-id",
                             g_variant_new ("s", mapping_id));
    }

  parameters_variant = g_variant_builder_end (&parameters_builder);
  meta_dbus_screen_cast_stream_set_parameters (skeleton, parameters_variant);

  priv->object_path =
    g_strdup_printf (META_SCREEN_CAST_STREAM_DBUS_PATH "/u%u",
                     ++global_stream_number);
  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (screen_cast_stream),
                                         priv->connection,
                                         priv->object_path,
                                         error))
    return FALSE;

  return TRUE;
}

static void
meta_screen_cast_stream_init_initable_iface (GInitableIface *iface)
{
  iface->init = meta_screen_cast_stream_initable_init;
}

static void
meta_screen_cast_stream_init (MetaScreenCastStream *screen_cast_stream)
{
}

static void
meta_screen_cast_stream_class_init (MetaScreenCastStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_screen_cast_stream_dispose;
  object_class->set_property = meta_screen_cast_stream_set_property;
  object_class->get_property = meta_screen_cast_stream_get_property;

  obj_props[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         META_TYPE_SCREEN_CAST_SESSION,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CONNECTION] =
    g_param_spec_object ("connection", NULL, NULL,
                         G_TYPE_DBUS_CONNECTION,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_STREAM] =
    g_param_spec_object ("stream", NULL, NULL,
                         META_TYPE_STREAM,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        META_TYPE_SCREEN_CAST_FLAG,
                        META_SCREEN_CAST_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

MetaScreenCastStream *
meta_screen_cast_stream_new (MetaScreenCastSession  *session,
                             GDBusConnection        *connection,
                             MetaStream             *stream,
                             MetaScreenCastFlag      flags,
                             GError                **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_STREAM,
                         NULL, error,
                         "session", session,
                         "connection", connection,
                         "stream", stream,
                         "flags", flags,
                         NULL);
}

MetaStream *
meta_screen_cast_stream_get_stream (MetaScreenCastStream *screen_cast_stream)
{
  MetaScreenCastStreamPrivate *priv =
    meta_screen_cast_stream_get_instance_private (screen_cast_stream);

  return priv->stream;
}
