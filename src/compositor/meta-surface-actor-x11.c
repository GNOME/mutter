/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 * Written by:
 *     Owen Taylor <otaylor@redhat.com>
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "compositor/meta-surface-actor-x11.h"

#include <X11/extensions/Xcomposite.h>

#include "cogl/winsys/cogl-texture-pixmap-x11.h"
#include "compositor/meta-cullable.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

struct _MetaSurfaceActorX11
{
  MetaSurfaceActor parent;

  MetaWindow *window;

  MetaDisplay *display;

  MetaMultiTexture *texture;
  Pixmap pixmap;
  Damage damage;

  int last_width;
  int last_height;

  /* This is used to detect fullscreen windows that need to be unredirected */
  guint full_damage_frames_count;
  guint does_full_damage  : 1;

  /* Other state... */
  guint received_damage : 1;
  guint size_changed : 1;

  guint unredirected   : 1;
};

G_DEFINE_TYPE (MetaSurfaceActorX11,
               meta_surface_actor_x11,
               META_TYPE_SURFACE_ACTOR)

static void
free_damage (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay;

  if (self->damage == None)
    return;

  xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  mtk_x11_error_trap_push (xdisplay);
  XDamageDestroy (xdisplay, self->damage);
  self->damage = None;
  mtk_x11_error_trap_pop (xdisplay);
}

static void
detach_pixmap (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  Display *xdisplay;

  if (self->pixmap == None)
    return;

  xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  meta_shaped_texture_set_texture (stex, NULL);
  cogl_context_flush (cogl_context);

  mtk_x11_error_trap_push (xdisplay);
  XFreePixmap (xdisplay, self->pixmap);
  self->pixmap = None;
  mtk_x11_error_trap_pop (xdisplay);

  g_clear_object (&self->texture);
}

static void
set_pixmap (MetaSurfaceActorX11 *self,
            Pixmap               pixmap)
{
  ClutterContext *clutter_context =
    clutter_actor_get_context (CLUTTER_ACTOR (self));
  ClutterBackend *backend = clutter_context_get_backend (clutter_context);
  CoglContext *ctx = clutter_backend_get_cogl_context (backend);
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  GError *error = NULL;
  CoglTexture *cogl_texture;

  g_assert (self->pixmap == None);
  self->pixmap = pixmap;

  cogl_texture = cogl_texture_pixmap_x11_new (ctx, self->pixmap, FALSE, &error);

  if (error != NULL)
    {
      g_warning ("Failed to allocate stex texture: %s", error->message);
      g_error_free (error);
    }
  else if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (COGL_TEXTURE_PIXMAP_X11 (cogl_texture))))
    g_warning ("NOTE: Not using GLX TFP!");

  self->texture = meta_multi_texture_new_simple (cogl_texture);
  meta_shaped_texture_set_texture (stex, self->texture);
}

static void
update_pixmap (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->size_changed)
    {
      detach_pixmap (self);
      self->size_changed = FALSE;
    }

  if (self->pixmap == None)
    {
      Pixmap new_pixmap;
      Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

      mtk_x11_error_trap_push (xdisplay);
      new_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (mtk_x11_error_trap_pop_with_return (xdisplay) != Success)
        {
          /* Probably a BadMatch if the window isn't viewable; we could
           * GrabServer/GetWindowAttributes/NameWindowPixmap/UngrabServer/Sync
           * to avoid this, but there's no reason to take two round trips
           * when one will do. (We need that Sync if we want to handle failures
           * for any reason other than !viewable. That's unlikely, but maybe
           * we'll BadAlloc or something.)
           */
          new_pixmap = None;
        }

      if (new_pixmap == None)
        {
          meta_topic (META_DEBUG_RENDER,
                      "Unable to get named pixmap for %s",
                      meta_window_get_description (self->window));
          return;
        }

      set_pixmap (self, new_pixmap);
    }
}

gboolean
meta_surface_actor_x11_is_visible (MetaSurfaceActorX11 *self)
{
  return (self->pixmap != None) && !self->unredirected;
}

static void
meta_surface_actor_x11_process_damage (MetaSurfaceActor   *actor,
                                       const MtkRectangle *area)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  CoglTexturePixmapX11 *pixmap;

  self->received_damage = TRUE;

  if (meta_window_is_fullscreen (self->window) && !self->unredirected && !self->does_full_damage)
    {
      MtkRectangle window_rect;
      meta_window_get_frame_rect (self->window, &window_rect);

      if (area->x == 0 &&
          area->y == 0 &&
          window_rect.width == area->width &&
          window_rect.height == area->height)
        self->full_damage_frames_count++;
      else
        self->full_damage_frames_count = 0;

      if (self->full_damage_frames_count >= 100)
        self->does_full_damage = TRUE;
    }

  if (!meta_surface_actor_x11_is_visible (self))
    return;

  /* We don't support multi-plane or YUV based formats in X */
  if (!meta_multi_texture_is_simple (self->texture))
    return;

  pixmap = COGL_TEXTURE_PIXMAP_X11 (meta_multi_texture_get_plane (self->texture, 0));
  cogl_texture_pixmap_x11_update_area (pixmap, area);
  meta_surface_actor_update_area (actor, area);
}

void
meta_surface_actor_x11_handle_updates (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->received_damage)
    {
      mtk_x11_error_trap_push (xdisplay);
      XDamageSubtract (xdisplay, self->damage, None, None);
      mtk_x11_error_trap_pop (xdisplay);

      self->received_damage = FALSE;
    }

  update_pixmap (self);
}

static gboolean
meta_surface_actor_x11_is_opaque (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  MetaShapedTexture *stex = meta_surface_actor_get_texture (actor);

  if (meta_surface_actor_x11_is_unredirected (self))
    return TRUE;

  return meta_shaped_texture_is_opaque (stex);
}

gboolean
meta_surface_actor_x11_should_unredirect (MetaSurfaceActorX11 *self)
{
  if (!meta_surface_actor_x11_is_opaque (META_SURFACE_ACTOR (self)))
    return FALSE;

  if (!(meta_window_is_fullscreen (self->window) && self->does_full_damage) &&
      !meta_window_is_override_redirect (self->window))
    return FALSE;

  return TRUE;
}

static void
sync_unredirected (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  mtk_x11_error_trap_push (xdisplay);

  if (self->unredirected)
    {
      XCompositeUnredirectWindow (xdisplay, xwindow, CompositeRedirectManual);
      XSync (xdisplay, False);
      detach_pixmap (self);
    }
  else
    {
      XCompositeRedirectWindow (xdisplay, xwindow, CompositeRedirectManual);
      XSync (xdisplay, False);
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }

  mtk_x11_error_trap_pop (xdisplay);
}

void
meta_surface_actor_x11_set_unredirected (MetaSurfaceActorX11 *self,
                                         gboolean             unredirected)
{
  if (self->unredirected == unredirected)
    return;

  self->unredirected = unredirected;
  sync_unredirected (self);
}

gboolean
meta_surface_actor_x11_is_unredirected (MetaSurfaceActorX11 *self)
{
  return self->unredirected;
}

static void
release_x11_resources (MetaSurfaceActorX11 *self)
{
  MetaX11Display *x11_display = meta_display_get_x11_display (self->display);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  detach_pixmap (self);
  free_damage (self);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
meta_surface_actor_x11_dispose (GObject *object)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (object);

  release_x11_resources (self);

  G_OBJECT_CLASS (meta_surface_actor_x11_parent_class)->dispose (object);
}

static void
meta_surface_actor_x11_class_init (MetaSurfaceActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_x11_dispose;

  surface_actor_class->process_damage = meta_surface_actor_x11_process_damage;
  surface_actor_class->is_opaque = meta_surface_actor_x11_is_opaque;
}

static void
meta_surface_actor_x11_init (MetaSurfaceActorX11 *self)
{
  self->last_width = -1;
  self->last_height = -1;
}

static void
create_damage (MetaSurfaceActorX11 *self)
{
  MetaX11Display *x11_display = meta_display_get_x11_display (self->display);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  mtk_x11_error_trap_push (xdisplay);
  self->damage = XDamageCreate (xdisplay, xwindow, XDamageReportBoundingBox);
  mtk_x11_error_trap_pop (xdisplay);
}

static void
window_decorated_notify (MetaWindow *window,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (user_data);

  release_x11_resources (self);
  create_damage (self);
}

static void
reset_texture (MetaSurfaceActorX11 *self)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));

  if (!self->texture)
    return;

  /* Setting the texture to NULL will cause all the FBO's cached by the
   * shaped texture's MetaTextureTower to be discarded and recreated.
   */
  meta_shaped_texture_set_texture (stex, NULL);
  meta_shaped_texture_set_texture (stex, self->texture);
}

MetaSurfaceActor *
meta_surface_actor_x11_new (MetaWindow *window)
{
  MetaSurfaceActorX11 *self = g_object_new (META_TYPE_SURFACE_ACTOR_X11, NULL);
  MetaDisplay *display = meta_window_get_display (window);

  g_assert (!meta_is_wayland_compositor ());

  self->window = window;
  self->display = display;

  g_signal_connect_object (self->display, "gl-video-memory-purged",
                           G_CALLBACK (reset_texture), self, G_CONNECT_SWAPPED);

  create_damage (self);
  g_signal_connect_object (self->window, "notify::decorated",
                           G_CALLBACK (window_decorated_notify), self, 0);

  g_signal_connect_object (meta_window_actor_from_window (window), "destroy",
                           G_CALLBACK (release_x11_resources), self,
                           G_CONNECT_SWAPPED);

  self->unredirected = FALSE;
  sync_unredirected (self);

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  clutter_actor_set_accessible_name (CLUTTER_ACTOR (self), "X11 surface");
  return META_SURFACE_ACTOR (self);
}

void
meta_surface_actor_x11_set_size (MetaSurfaceActorX11 *self,
                                 int width, int height)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));

  if (self->last_width == width &&
      self->last_height == height)
    return;

  self->size_changed = TRUE;
  self->last_width = width;
  self->last_height = height;
  meta_shaped_texture_set_fallback_size (stex, width, height);
}
