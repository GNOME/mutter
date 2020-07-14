/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
 * Copyright 2020 DisplayLink (UK) Ltd.
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "backends/native/meta-cursor-renderer-native.h"

#include <string.h>
#include <errno.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/native/meta-renderer-native.h"
#include "core/boxes-private.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

struct _MetaCursorRendererNative
{
  MetaCursorRenderer parent;
};

struct _MetaCursorRendererNativePrivate
{
  MetaBackend *backend;
  MetaKmsCursorRenderer *kms_cursor_renderer;

  gboolean use_kms_cursor;

  MetaCursorSprite *last_cursor;
  guint animation_timeout_id;
};
typedef struct _MetaCursorRendererNativePrivate MetaCursorRendererNativePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRendererNative, meta_cursor_renderer_native, META_TYPE_CURSOR_RENDERER);

static void
meta_cursor_renderer_native_finalize (GObject *object)
{
  MetaCursorRendererNative *renderer = META_CURSOR_RENDERER_NATIVE (object);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (renderer);

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);
  g_clear_object (&priv->kms_cursor_renderer);

  G_OBJECT_CLASS (meta_cursor_renderer_native_parent_class)->finalize (object);
}

static gboolean
should_have_kms_cursor (MetaCursorRenderer *renderer,
                        MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *cursor_renderer_native =
    META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);
  CoglTexture *texture;

  if (!cursor_sprite)
    return FALSE;

  if (!priv->kms_cursor_renderer)
    return FALSE;

  if (meta_backend_is_hw_cursors_inhibited (priv->backend))
    return FALSE;

  texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  if (!texture)
    return FALSE;

  return TRUE;
}

static gboolean
meta_cursor_renderer_native_update_animation (MetaCursorRendererNative *native)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorSprite *cursor_sprite = meta_cursor_renderer_get_cursor (renderer);

  priv->animation_timeout_id = 0;
  meta_cursor_sprite_tick_frame (cursor_sprite);
  meta_cursor_renderer_force_update (renderer);

  return G_SOURCE_REMOVE;
}

static void
maybe_schedule_cursor_sprite_animation_frame (MetaCursorRendererNative *native,
                                              MetaCursorSprite         *cursor_sprite)
{
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  gboolean cursor_change;
  guint delay;

  cursor_change = cursor_sprite != priv->last_cursor;
  priv->last_cursor = cursor_sprite;

  if (!cursor_change && priv->animation_timeout_id)
    return;

  g_clear_handle_id (&priv->animation_timeout_id, g_source_remove);

  if (cursor_sprite && meta_cursor_sprite_is_animated (cursor_sprite))
    {
      delay = meta_cursor_sprite_get_current_frame_time (cursor_sprite);

      if (delay == 0)
        return;

      priv->animation_timeout_id =
        g_timeout_add (delay,
                       (GSourceFunc) meta_cursor_renderer_native_update_animation,
                       native);
      g_source_set_name_by_id (priv->animation_timeout_id,
                               "[mutter] meta_cursor_renderer_native_update_animation");
    }
}

static gboolean
meta_cursor_renderer_native_update_cursor (MetaCursorRenderer *renderer,
                                           MetaCursorSprite   *cursor_sprite)
{
  MetaCursorRendererNative *native = META_CURSOR_RENDERER_NATIVE (renderer);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);
  gboolean kms_cursor_drawn = FALSE;

  if (cursor_sprite)
    meta_cursor_sprite_realize_texture (cursor_sprite);

  maybe_schedule_cursor_sprite_animation_frame (native, cursor_sprite);

  priv->use_kms_cursor = should_have_kms_cursor (renderer, cursor_sprite);

  if (priv->kms_cursor_renderer)
    {
      if (priv->use_kms_cursor)
        {
          kms_cursor_drawn =
            meta_kms_cursor_renderer_update_cursor (priv->kms_cursor_renderer,
                                                    cursor_sprite);
        }
      else
        {
          meta_kms_cursor_renderer_update_cursor (priv->kms_cursor_renderer, NULL);
        }
    }

  return (kms_cursor_drawn ||
          !cursor_sprite ||
          !meta_cursor_sprite_get_cogl_texture (cursor_sprite));
}

static void
meta_cursor_renderer_native_class_init (MetaCursorRendererNativeClass *klass)
{
  MetaCursorRendererClass *renderer_class = META_CURSOR_RENDERER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_cursor_renderer_native_finalize;
  renderer_class->update_cursor = meta_cursor_renderer_native_update_cursor;
}

static void
force_update_kms_cursor (MetaCursorRendererNative *native)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);

  if (priv->kms_cursor_renderer)
    meta_kms_cursor_renderer_invalidate_state (priv->kms_cursor_renderer);
  meta_cursor_renderer_force_update (renderer);
}

static void
on_monitors_changed (MetaMonitorManager       *monitors,
                     MetaCursorRendererNative *native)
{
  force_update_kms_cursor (native);
}

MetaCursorRendererNative *
meta_cursor_renderer_native_new (MetaBackend        *backend,
                                 ClutterInputDevice *device)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRendererNative *cursor_renderer_native;
  MetaCursorRendererNativePrivate *priv;

  cursor_renderer_native = g_object_new (META_TYPE_CURSOR_RENDERER_NATIVE,
                                         "backend", backend,
                                         "device", device,
                                         NULL);
  priv =
    meta_cursor_renderer_native_get_instance_private (cursor_renderer_native);

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           cursor_renderer_native, 0);

  priv->backend = backend;

  return cursor_renderer_native;
}

static void
meta_cursor_renderer_native_init (MetaCursorRendererNative *native)
{
}

void
meta_cursor_renderer_native_set_kms_cursor_renderer (MetaCursorRendererNative *native,
                                                     MetaKmsCursorRenderer    *kms_cursor_renderer)
{
  MetaCursorRenderer *renderer = META_CURSOR_RENDERER (native);
  MetaCursorRendererNativePrivate *priv =
    meta_cursor_renderer_native_get_instance_private (native);

  if (priv->kms_cursor_renderer == kms_cursor_renderer)
    return;

  if (g_set_object (&priv->kms_cursor_renderer, kms_cursor_renderer))
    {
      if (priv->kms_cursor_renderer)
        {
          meta_kms_cursor_renderer_set_cursor_renderer (priv->kms_cursor_renderer,
                                                        renderer);
        }

      meta_cursor_renderer_force_update (renderer);
    }
}
