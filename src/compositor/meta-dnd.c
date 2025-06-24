/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2016 Hyungwon Hwang
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

#include "compositor/meta-dnd-private.h"

#include "meta/meta-backend.h"
#include "compositor/compositor-private.h"
#include "core/display-private.h"
#include "backends/meta-dnd-private.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-data-device.h"

struct _MetaDndClass
{
  GObjectClass parent_class;
};

typedef struct _MetaDndPrivate MetaDndPrivate;

struct _MetaDndPrivate
{
  MetaBackend *backend;

  gboolean dnd_during_modal;
};

struct _MetaDnd
{
  GObject parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaDnd, meta_dnd, G_TYPE_OBJECT);

enum
{
  ENTER,
  POSITION_CHANGE,
  LEAVE,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
meta_dnd_class_init (MetaDndClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  signals[ENTER] =
    g_signal_new ("dnd-enter",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[POSITION_CHANGE] =
    g_signal_new ("dnd-position-change",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  signals[LEAVE] =
    g_signal_new ("dnd-leave",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_dnd_init (MetaDnd *dnd)
{
}

MetaDnd *
meta_dnd_new (MetaBackend *backend)
{
  MetaDnd *dnd;
  MetaDndPrivate *priv;

  dnd = g_object_new (META_TYPE_DND, NULL);
  priv = meta_dnd_get_instance_private (dnd);
  priv->backend = backend;

  return dnd;
}

static void
meta_dnd_notify_dnd_enter (MetaDnd *dnd)
{
  g_signal_emit (dnd, signals[ENTER], 0);
}

static void
meta_dnd_notify_dnd_position_change (MetaDnd *dnd,
                                      int      x,
                                      int      y)
{
  g_signal_emit (dnd, signals[POSITION_CHANGE], 0, x, y);
}

static void
meta_dnd_notify_dnd_leave (MetaDnd *dnd)
{
  g_signal_emit (dnd, signals[LEAVE], 0);
}

void
meta_dnd_wayland_on_motion_event (MetaDnd            *dnd,
                                  const ClutterEvent *event)
{
  gfloat event_x, event_y;

  g_return_if_fail (event != NULL);

  clutter_event_get_coords (event, &event_x, &event_y);
  meta_dnd_notify_dnd_position_change (dnd, (int)event_x, (int)event_y);
}

void
meta_dnd_wayland_handle_begin_modal (MetaCompositor *compositor)
{
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (context);
  MetaWaylandDataDevice *data_device = &wayland_compositor->seat->data_device;
  MetaBackend *backend = meta_context_get_backend (context);
  MetaDnd *dnd = meta_backend_get_dnd (backend);
  MetaDndPrivate *priv = meta_dnd_get_instance_private (dnd);

  if (!priv->dnd_during_modal &&
      meta_wayland_data_device_get_current_grab (data_device))
    {
      priv->dnd_during_modal = TRUE;

      meta_dnd_notify_dnd_enter (dnd);
    }
}

void
meta_dnd_wayland_handle_end_modal (MetaCompositor *compositor)
{
  MetaDisplay *display = meta_compositor_get_display (compositor);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaDnd *dnd = meta_backend_get_dnd (backend);
  MetaDndPrivate *priv = meta_dnd_get_instance_private (dnd);

  if (!priv->dnd_during_modal)
    return;

  priv->dnd_during_modal = FALSE;

  meta_dnd_notify_dnd_leave (dnd);
}
