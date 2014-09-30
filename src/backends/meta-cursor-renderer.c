/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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

#include "meta-cursor-renderer.h"
#include "meta-cursor-private.h"

#include <meta/meta-backend.h>
#include <meta/util.h>

#include <cogl/cogl.h>
#include <clutter/clutter.h>

#include "meta-stage.h"

typedef struct
{
  CoglTexture *texture;
  MetaRectangle current_rect;
  int current_x, current_y;
} MetaCursorLayer;

struct _MetaCursorRendererPrivate
{
  MetaCursorLayer core_layer;
  MetaCursorLayer dnd_layer;

  MetaCursorReference *displayed_cursor;
  int dnd_surface_offset_x, dnd_surface_offset_y;
  gboolean cursor_handled_by_backend;
};
typedef struct _MetaCursorRendererPrivate MetaCursorRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCursorRenderer, meta_cursor_renderer, G_TYPE_OBJECT);

static void
queue_redraw (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  CoglTexture *texture;

  /* During early initialization, we can have no stage */
  if (!stage)
    return;

  /* Pointer cursor */
  if (!priv->cursor_handled_by_backend)
    texture = priv->core_layer.texture;
  else
    texture = NULL;

  meta_stage_set_cursor (META_STAGE (stage), texture,
                         &priv->core_layer.current_rect);

  /* DnD surface */
  meta_stage_set_dnd_surface (META_STAGE (stage),
                              priv->dnd_layer.texture,
                              &priv->dnd_layer.current_rect);
}

static gboolean
meta_cursor_renderer_real_update_cursor (MetaCursorRenderer *renderer)
{
  return FALSE;
}

static void
meta_cursor_renderer_class_init (MetaCursorRendererClass *klass)
{
  klass->update_cursor = meta_cursor_renderer_real_update_cursor;
}

static void
meta_cursor_renderer_init (MetaCursorRenderer *renderer)
{
}

static void
update_layer (MetaCursorRenderer *renderer,
              MetaCursorLayer    *layer,
              CoglTexture        *texture,
              int                 offset_x,
              int                 offset_y)
{
  layer->texture = texture;

  if (layer->texture)
    {
      layer->current_rect.x = layer->current_x + offset_x;
      layer->current_rect.y = layer->current_y + offset_y;
      layer->current_rect.width = cogl_texture_get_width (layer->texture);
      layer->current_rect.height = cogl_texture_get_height (layer->texture);
    }
  else
    {
      layer->current_rect.x = 0;
      layer->current_rect.y = 0;
      layer->current_rect.width = 0;
      layer->current_rect.height = 0;
    }
}

static void
emit_update_cursor (MetaCursorRenderer *renderer,
                    gboolean            force)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  gboolean handled_by_backend, should_redraw = FALSE;

  handled_by_backend = META_CURSOR_RENDERER_GET_CLASS (renderer)->update_cursor (renderer);

  if (handled_by_backend != priv->cursor_handled_by_backend)
    {
      priv->cursor_handled_by_backend = handled_by_backend;
      should_redraw = TRUE;
    }

  if (force || !handled_by_backend || priv->dnd_layer.texture)
    should_redraw = TRUE;

  if (should_redraw)
    queue_redraw (renderer);
}

static void
update_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  CoglTexture *texture;
  int hot_x, hot_y;

  /* Cursor layer */
  if (priv->displayed_cursor)
    {
      texture = meta_cursor_reference_get_cogl_texture (priv->displayed_cursor,
                                                        &hot_x, &hot_y);
    }
  else
    {
      texture = NULL;
      hot_x = 0;
      hot_y = 0;
    }

  update_layer (renderer, &priv->core_layer, texture, -hot_x, -hot_y);
  emit_update_cursor (renderer, FALSE);
}

MetaCursorRenderer *
meta_cursor_renderer_new (void)
{
  return g_object_new (META_TYPE_CURSOR_RENDERER, NULL);
}

void
meta_cursor_renderer_set_cursor (MetaCursorRenderer  *renderer,
                                 MetaCursorReference *cursor)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  if (priv->displayed_cursor == cursor)
    return;

  priv->displayed_cursor = cursor;
  update_cursor (renderer);
}

void
meta_cursor_renderer_set_dnd_surface (MetaCursorRenderer *renderer,
                                      CoglTexture        *texture,
                                      int                 offset_x,
                                      int                 offset_y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  g_assert (meta_is_wayland_compositor ());

  priv->dnd_surface_offset_x = offset_x;
  priv->dnd_surface_offset_y = offset_y;

  update_layer (renderer, &priv->dnd_layer, texture, offset_x, offset_y);
  emit_update_cursor (renderer, TRUE);
}

void
meta_cursor_renderer_set_position (MetaCursorRenderer *renderer,
                                   int x, int y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  g_assert (meta_is_wayland_compositor ());

  priv->core_layer.current_x = x;
  priv->core_layer.current_y = y;

  update_cursor (renderer);
}

void
meta_cursor_renderer_set_dnd_surface_position (MetaCursorRenderer *renderer,
                                               int                 x,
                                               int                 y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  g_assert (meta_is_wayland_compositor ());

  priv->dnd_layer.current_x = x;
  priv->dnd_layer.current_y = y;

  update_layer (renderer, &priv->dnd_layer, priv->dnd_layer.texture,
                priv->dnd_surface_offset_x, priv->dnd_surface_offset_y);
  emit_update_cursor (renderer, FALSE);
}

void
meta_cursor_renderer_dnd_failed (MetaCursorRenderer *renderer,
                                 int                 dest_x,
                                 int                 dest_y)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);

  g_assert (meta_is_wayland_compositor ());

  if (priv->dnd_layer.texture)
    meta_stage_dnd_failed (META_STAGE (stage),
                           dest_x + priv->dnd_surface_offset_x,
                           dest_y + priv->dnd_surface_offset_y);
}

MetaCursorReference *
meta_cursor_renderer_get_cursor (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return priv->displayed_cursor;
}

const MetaRectangle *
meta_cursor_renderer_get_rect (MetaCursorRenderer *renderer)
{
  MetaCursorRendererPrivate *priv = meta_cursor_renderer_get_instance_private (renderer);

  return &priv->core_layer.current_rect;
}
