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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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
#include "core/window-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

struct _MetaSurfaceActorX11
{
  MetaSurfaceActor parent;

  MetaWindow *window;

  MetaDisplay *display;

  CoglTexture *texture;
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
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->damage == None)
    return;

  meta_x11_error_trap_push (display->x11_display);
  XDamageDestroy (xdisplay, self->damage);
  self->damage = None;
  meta_x11_error_trap_pop (display->x11_display);
}

static void
detach_pixmap (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));

  if (self->pixmap == None)
    return;

  /* Get rid of all references to the pixmap before freeing it; it's unclear whether
   * you are supposed to be able to free a GLXPixmap after freeing the underlying
   * pixmap, but it certainly doesn't work with current DRI/Mesa
   */
  meta_shaped_texture_set_texture (stex, NULL);
  cogl_flush ();

  meta_x11_error_trap_push (display->x11_display);
  XFreePixmap (xdisplay, self->pixmap);
  self->pixmap = None;
  meta_x11_error_trap_pop (display->x11_display);

  g_clear_pointer (&self->texture, cogl_object_unref);
}

static void
set_pixmap (MetaSurfaceActorX11 *self,
            Pixmap               pixmap)
{
  CoglContext *ctx = clutter_backend_get_cogl_context (clutter_get_default_backend ());
  MetaShapedTexture *stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  CoglError *error = NULL;
  CoglTexture *texture;

  g_assert (self->pixmap == None);
  self->pixmap = pixmap;

  texture = COGL_TEXTURE (cogl_texture_pixmap_x11_new (ctx, self->pixmap, FALSE, &error));

  if (error != NULL)
    {
      g_warning ("Failed to allocate stex texture: %s", error->message);
      cogl_error_free (error);
    }
  else if (G_UNLIKELY (!cogl_texture_pixmap_x11_is_using_tfp_extension (COGL_TEXTURE_PIXMAP_X11 (texture))))
    g_warning ("NOTE: Not using GLX TFP!\n");

  self->texture = texture;
  meta_shaped_texture_set_texture (stex, texture);
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

      meta_x11_error_trap_push (display->x11_display);
      new_pixmap = XCompositeNameWindowPixmap (xdisplay, xwindow);

      if (meta_x11_error_trap_pop_with_return (display->x11_display) != Success)
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
          meta_verbose ("Unable to get named pixmap for %s\n",
                        meta_window_get_description (self->window));
          return;
        }

      set_pixmap (self, new_pixmap);
    }
}

static gboolean
is_visible (MetaSurfaceActorX11 *self)
{
  return (self->pixmap != None) && !self->unredirected;
}

static void
meta_surface_actor_x11_process_damage (MetaSurfaceActor *actor,
                                       int x, int y, int width, int height)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  self->received_damage = TRUE;

  if (meta_window_is_fullscreen (self->window) && !self->unredirected && !self->does_full_damage)
    {
      MetaRectangle window_rect;
      meta_window_get_frame_rect (self->window, &window_rect);

      if (x == 0 &&
          y == 0 &&
          window_rect.width == width &&
          window_rect.height == height)
        self->full_damage_frames_count++;
      else
        self->full_damage_frames_count = 0;

      if (self->full_damage_frames_count >= 100)
        self->does_full_damage = TRUE;
    }

  if (!is_visible (self))
    return;

  cogl_texture_pixmap_x11_update_area (COGL_TEXTURE_PIXMAP_X11 (self->texture),
                                       x, y, width, height);
}

static void
meta_surface_actor_x11_pre_paint (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  if (self->received_damage)
    {
      meta_x11_error_trap_push (display->x11_display);
      XDamageSubtract (xdisplay, self->damage, None, None);
      meta_x11_error_trap_pop (display->x11_display);

      self->received_damage = FALSE;
    }

  update_pixmap (self);
}

static gboolean
meta_surface_actor_x11_is_visible (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  return is_visible (self);
}

static gboolean
meta_surface_actor_x11_is_opaque (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  /* If we're not ARGB32, then we're opaque. */
  if (!meta_surface_actor_is_argb32 (actor))
    return TRUE;

  cairo_region_t *opaque_region = meta_surface_actor_get_opaque_region (actor);

  /* If we have no opaque region, then no pixels are opaque. */
  if (!opaque_region)
    return FALSE;

  MetaWindow *window = self->window;
  cairo_rectangle_int_t client_area;
  meta_window_get_client_area_rect (window, &client_area);

  /* Otherwise, check if our opaque region covers our entire surface. */
  if (cairo_region_contains_rectangle (opaque_region, &client_area) == CAIRO_REGION_OVERLAP_IN)
    return TRUE;

  return FALSE;
}

static gboolean
meta_surface_actor_x11_should_unredirect (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  MetaWindow *window = self->window;

  if (meta_window_requested_dont_bypass_compositor (window))
    return FALSE;

  if (window->opacity != 0xFF)
    return FALSE;

  if (window->shape_region != NULL)
    return FALSE;

  if (!meta_window_is_monitor_sized (window))
    return FALSE;

  if (meta_window_requested_bypass_compositor (window))
    return TRUE;

  if (!meta_surface_actor_x11_is_opaque (actor))
    return FALSE;

  if (meta_window_is_override_redirect (window))
    return TRUE;

  if (self->does_full_damage)
    return TRUE;

  return FALSE;
}

static void
sync_unredirected (MetaSurfaceActorX11 *self)
{
  MetaDisplay *display = self->display;
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  meta_x11_error_trap_push (display->x11_display);

  if (self->unredirected)
    {
      detach_pixmap (self);
      XCompositeUnredirectWindow (xdisplay, xwindow, CompositeRedirectManual);
    }
  else
    {
      XCompositeRedirectWindow (xdisplay, xwindow, CompositeRedirectManual);
    }

  meta_x11_error_trap_pop (display->x11_display);
}

static void
meta_surface_actor_x11_set_unredirected (MetaSurfaceActor *actor,
                                         gboolean          unredirected)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  if (self->unredirected == unredirected)
    return;

  self->unredirected = unredirected;
  sync_unredirected (self);
}

static gboolean
meta_surface_actor_x11_is_unredirected (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);

  return self->unredirected;
}

static void
meta_surface_actor_x11_dispose (GObject *object)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (object);

  detach_pixmap (self);
  free_damage (self);

  G_OBJECT_CLASS (meta_surface_actor_x11_parent_class)->dispose (object);
}

static MetaWindow *
meta_surface_actor_x11_get_window (MetaSurfaceActor *actor)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (actor);
  return self->window;
}

static void
meta_surface_actor_x11_class_init (MetaSurfaceActorX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);

  object_class->dispose = meta_surface_actor_x11_dispose;

  surface_actor_class->process_damage = meta_surface_actor_x11_process_damage;
  surface_actor_class->pre_paint = meta_surface_actor_x11_pre_paint;
  surface_actor_class->is_visible = meta_surface_actor_x11_is_visible;

  surface_actor_class->should_unredirect = meta_surface_actor_x11_should_unredirect;
  surface_actor_class->set_unredirected = meta_surface_actor_x11_set_unredirected;
  surface_actor_class->is_unredirected = meta_surface_actor_x11_is_unredirected;

  surface_actor_class->get_window = meta_surface_actor_x11_get_window;
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
  Display *xdisplay = meta_x11_display_get_xdisplay (self->display->x11_display);
  Window xwindow = meta_window_x11_get_toplevel_xwindow (self->window);

  self->damage = XDamageCreate (xdisplay, xwindow, XDamageReportBoundingBox);
}

static void
window_decorated_notify (MetaWindow *window,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
  MetaSurfaceActorX11 *self = META_SURFACE_ACTOR_X11 (user_data);

  detach_pixmap (self);
  free_damage (self);
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

  self->unredirected = FALSE;
  sync_unredirected (self);

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
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
