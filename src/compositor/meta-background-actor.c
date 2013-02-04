/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * meta-background-actor.c: Actor for painting the root window background
 *
 * Copyright 2009 Sander Dijkhuis
 * Copyright 2010 Red Hat, Inc.
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
 * Portions adapted from gnome-shell/src/shell-global.c
 */

#include <config.h>

#include <clutter/clutter.h>

#include "cogl-utils.h"
#include "compositor-private.h"
#include <meta/errors.h>
#include "meta-background-actor-private.h"

#define CROSSFADE_DURATION 1000

/* We allow creating multiple MetaBackgroundActors for the same MetaScreen and GnomeBG to
 * allow different rendering options to be set for different copies.
 * But we want to share the same underlying CoglTexture for efficiency.
 *
 * This structure holds common information.
 */
typedef struct _MetaBackground MetaBackground;

typedef struct {
  CoglTexture                *texture;
  float                       texture_width;
  float                       texture_height;

  /* ClutterColor, not CoglColor, because
     the latter is opaque... */
  ClutterColor                colors[2];
  GDesktopBackgroundStyle     style;
  GDesktopBackgroundShading   shading;
  char                       *picture_uri;
} MetaBackgroundState;

struct _MetaBackground
{
  GSList *actors;

  MetaScreen   *screen;
  GCancellable *cancellable;

  MetaBackgroundState old_state;
  MetaBackgroundState state;
  GSettings   *settings;

  GTask *rendering_task;
};

struct _MetaBackgroundActorPrivate
{
  MetaScreen     *screen;
  MetaBackground *background;
  GSettings      *settings;

  CoglPipeline *solid_pipeline;
  CoglPipeline *pipeline;

  cairo_region_t *visible_region;
  float dim_factor;
  float crossfade_progress;
  guint is_crossfading : 1;
};

enum
{
  PROP_0,

  PROP_SCREEN,
  PROP_SETTINGS,

  PROP_DIM_FACTOR,
  PROP_CROSSFADE_PROGRESS,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (MetaBackgroundActor, meta_background_actor, CLUTTER_TYPE_ACTOR);

static void meta_background_update                    (MetaBackground      *background,
                                                       const char          *picture_uri);
static void meta_background_actor_screen_size_changed (MetaScreen          *screen,
                                                       MetaBackgroundActor *actor);
static void meta_background_actor_constructed         (GObject             *object);

static void clear_state                (MetaBackgroundState *state);
static void set_texture                (MetaBackground  *background,
                                        CoglHandle       texture,
                                        const char      *picture_uri);

static void
meta_background_free (MetaBackground *background)
{
  clear_state (&background->old_state);
  clear_state (&background->state);

  g_cancellable_cancel (background->cancellable);
  g_object_unref (background->cancellable);

  g_clear_object (&background->rendering_task);

  g_slice_free (MetaBackground, background);
}

static void
on_settings_changed (GSettings      *settings,
                     const char     *key,
                     MetaBackground *background)
{
  char *picture_uri;

  picture_uri = g_settings_get_string (settings, "picture-uri");
  meta_background_update (background, picture_uri);
  g_free (picture_uri);
}

static GSettings *
meta_background_get_default_settings (void)
{
  return g_settings_new ("org.gnome.desktop.background");
}

static MetaBackground *
meta_background_get (MetaScreen *screen,
                     GSettings  *settings)
{
  static GQuark background_quark;
  MetaBackground *background;

  if (G_UNLIKELY (background_quark == 0))
    background_quark = g_quark_from_static_string ("meta-background");

  background = g_object_get_qdata (G_OBJECT (settings), background_quark);
  if (G_UNLIKELY (background == NULL))
    {
      background = g_slice_new0 (MetaBackground);
      g_object_set_qdata_full (G_OBJECT (settings), background_quark,
                               background, (GDestroyNotify) meta_background_free);

      background->settings = settings;
      background->screen = screen;

      g_signal_connect (settings, "changed",
                        G_CALLBACK (on_settings_changed), background);
      on_settings_changed (settings, NULL, background);
    }

  return background;
}

static void
update_actor_pipeline (MetaBackgroundActor *self,
                       gboolean             crossfade)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  priv->is_crossfading = crossfade;
  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

static inline void
clear_state (MetaBackgroundState *state)
{
  g_clear_pointer (&state->texture, cogl_handle_unref);
  g_free (state->picture_uri);
  state->picture_uri = NULL;
}

static void
crossfade_completed (ClutterTimeline     *timeline,
                     MetaBackgroundActor *actor)
{
  clear_state (&actor->priv->background->old_state);
  update_actor_pipeline (actor, FALSE);
}

static inline gboolean
state_is_valid (MetaBackgroundState *state)
{
  return state->texture != COGL_INVALID_HANDLE;
}

static inline void
get_color_from_settings (GSettings    *settings,
                         const char   *key,
                         ClutterColor *color)
{
  char *str;

  str = g_settings_get_string (settings, key);
  clutter_color_from_string (color, str);

  g_free (str);
}

static void
set_texture (MetaBackground *background,
             CoglHandle      texture,
             const char     *picture_uri)
{
  GSList *l;
  gboolean crossfade;
  int width, height;

  g_assert (texture != COGL_INVALID_HANDLE);

  clear_state (&background->old_state);

  if (state_is_valid (&background->state))
    background->old_state = background->state;

  background->state.texture = cogl_handle_ref (texture);
  get_color_from_settings (background->settings, "primary-color",
                           &background->state.colors[0]);
  get_color_from_settings (background->settings, "secondary-color",
                           &background->state.colors[1]);
  background->state.style = g_settings_get_enum (background->settings, "picture-options");
  background->state.shading = g_settings_get_enum (background->settings, "color-shading-type");
  background->state.texture_width = cogl_texture_get_width (background->state.texture);
  background->state.texture_height = cogl_texture_get_height (background->state.texture);
  background->state.picture_uri = g_strdup (picture_uri);

  crossfade = state_is_valid (&background->old_state);

  meta_screen_get_size (background->screen, &width, &height);

  for (l = background->actors; l; l = l->next)
    {
      MetaBackgroundActor *actor = l->data;

      update_actor_pipeline (actor, crossfade);

      if (crossfade)
        {
          ClutterTransition *transition;
          ClutterInterval *interval;

          interval = clutter_interval_new (G_TYPE_FLOAT, 0.0, 1.0);
          transition = g_object_new (CLUTTER_TYPE_PROPERTY_TRANSITION,
                                     "animatable", actor,
                                     "property-name", "crossfade-progress",
                                     "interval", interval,
                                     "remove-on-complete", TRUE,
                                     "duration", CROSSFADE_DURATION,
                                     "progress-mode", CLUTTER_EASE_OUT_QUAD,
                                     NULL);

          g_signal_connect_object (transition, "completed",
                                   G_CALLBACK (crossfade_completed), actor, 0);

          clutter_actor_remove_transition (CLUTTER_ACTOR (actor), "crossfade");
          clutter_actor_add_transition (CLUTTER_ACTOR (actor), "crossfade",
                                        transition);
        }
    }
}

static inline void
meta_background_ensure_rendered (MetaBackground *background)
{
  if (G_LIKELY (background->rendering_task == NULL ||
                background->state.texture != COGL_INVALID_HANDLE))
    return;

  g_task_wait_sync (background->rendering_task);
}

static void
meta_background_actor_dispose (GObject *object)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);
  MetaBackgroundActorPrivate *priv = self->priv;

  meta_background_actor_set_visible_region (self, NULL);

  if (priv->background != NULL)
    {
      priv->background->actors = g_slist_remove (priv->background->actors, self);
      priv->background = NULL;
    }

  g_clear_pointer (&priv->solid_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->pipeline, cogl_object_unref);
  g_clear_object (&priv->settings);

  G_OBJECT_CLASS (meta_background_actor_parent_class)->dispose (object);
}

static void
meta_background_actor_get_preferred_width (ClutterActor *actor,
                                           gfloat        for_height,
                                           gfloat       *min_width_p,
                                           gfloat       *natural_width_p)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  if (min_width_p)
    *min_width_p = width;
  if (natural_width_p)
    *natural_width_p = width;
}

static void
meta_background_actor_get_preferred_height (ClutterActor *actor,
                                            gfloat        for_width,
                                            gfloat       *min_height_p,
                                            gfloat       *natural_height_p)

{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  if (min_height_p)
    *min_height_p = height;
  if (natural_height_p)
    *natural_height_p = height;
}

static void
paint_gradient (MetaBackgroundState *state,
                CoglPipeline        *pipeline,
                float                color_factor,
                float                alpha_factor,
                ClutterActorBox     *monitor_box,
                float                actor_width,
                float                actor_height,
                float                screen_width,
                float                screen_height)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *context = clutter_backend_get_cogl_context (backend);
  CoglPrimitive *primitive;
  int i;

  /* Order is:
   * 1 -- 0
   * |    |
   * 3 -- 2
   *
   * (tri strip with counter-clockwise winding)
   */
  CoglVertexP2C4 vertices[4] = {
    { monitor_box->x2 * (actor_width / screen_width), monitor_box->y1 * (actor_height / screen_height),
      state->colors[0].red, state->colors[0].green, state->colors[0].blue, state->colors[0].alpha },

    { monitor_box->x1 * (actor_width / screen_width), monitor_box->y1 * (actor_height / screen_height),
      state->colors[0].red, state->colors[0].green, state->colors[0].blue, state->colors[0].alpha },

    { monitor_box->x2 * (actor_width / screen_width), monitor_box->y2 * (actor_height / screen_height),
      state->colors[0].red, state->colors[0].green, state->colors[0].blue, state->colors[0].alpha },

    { monitor_box->x1 * (actor_width / screen_width), monitor_box->y2 * (actor_height / screen_height),
      state->colors[0].red, state->colors[0].green, state->colors[0].blue, state->colors[0].alpha },
  };

  /* These styles cover the entire screen */
  if (state->style == G_DESKTOP_BACKGROUND_STYLE_WALLPAPER ||
      state->style == G_DESKTOP_BACKGROUND_STYLE_STRETCHED ||
      state->style == G_DESKTOP_BACKGROUND_STYLE_ZOOM)
    return;

  if (state->shading != G_DESKTOP_BACKGROUND_SHADING_SOLID)
    {
      if (state->shading == G_DESKTOP_BACKGROUND_SHADING_HORIZONTAL)
        {
          vertices[2].r = vertices[3].r = state->colors[1].red;
          vertices[2].g = vertices[3].g = state->colors[1].green;
          vertices[2].b = vertices[3].b = state->colors[1].blue;
          vertices[2].a = vertices[3].a = state->colors[1].alpha;
        }
      else
        {
          vertices[0].r = vertices[2].r = state->colors[1].red;
          vertices[0].g = vertices[2].g = state->colors[1].green;
          vertices[0].b = vertices[2].b = state->colors[1].blue;
          vertices[0].a = vertices[2].a = state->colors[1].alpha;
        }
    }

  for (i = 0; i < 4; i++)
    {
      vertices[i].r *= color_factor;
      vertices[i].g *= color_factor;
      vertices[i].b *= color_factor;
      vertices[i].a *= alpha_factor;
    }

  primitive = cogl_primitive_new_p2c4 (context,
                                       COGL_VERTICES_MODE_TRIANGLE_STRIP,
                                       4, vertices);
  cogl_framebuffer_draw_primitive (cogl_get_draw_framebuffer (),
                                   pipeline, primitive);
  cogl_object_unref (primitive);
}

static void
paint_background_box (MetaBackgroundState *state,
                      CoglPipeline        *pipeline,
                      float                color_factor,
                      float                alpha_factor,
                      ClutterActorBox     *monitor_box,
                      float                actor_width,
                      float                actor_height,
                      float                screen_width,
                      float                screen_height)
{
  CoglPrimitive *primitive;
  CoglPipeline *pipeline_copy;
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *context = clutter_backend_get_cogl_context (backend);
  int i;
  float monitor_width, monitor_height;
  float image_width, image_height;

  /* Order is:
   * 1 -- 0
   * |    |
   * 3 -- 2
   *
   * (tri strip with counter-clockwise winding)
   */
  CoglVertexP2T2 vertices[4];

  if (state->style == G_DESKTOP_BACKGROUND_STYLE_NONE)
    return;

  vertices[0].s = 1.0;
  vertices[0].t = 0.0;
  vertices[1].s = 0.0;
  vertices[1].t = 0.0;
  vertices[2].s = 1.0;
  vertices[2].t = 1.0;
  vertices[3].s = 0.0;
  vertices[3].t = 1.0;

  clutter_actor_box_get_size (monitor_box, &monitor_width, &monitor_height);

  switch (state->style) {
  case G_DESKTOP_BACKGROUND_STYLE_STRETCHED:
    /* Stretched:
       The easy case: the image is stretched to cover the monitor fully.
    */
    image_width = monitor_width;
    image_height = monitor_height;
    break;

  case G_DESKTOP_BACKGROUND_STYLE_SPANNED:
    /* Spanned:
       The image is scaled to fit and drawn across all monitors.
       If this is the case, monitor_box is actually the union of all monitors,
       and we can fall through to scaled.
    */
  case G_DESKTOP_BACKGROUND_STYLE_SCALED:
    /* Scaled:
       The image is scaled to fit (so one dimension is always covered)
    */
    {
      float scaling_factor = MIN (monitor_width / state->texture_width,
                                  monitor_height / state->texture_height);
      image_width = state->texture_width * scaling_factor;
      image_height = state->texture_height * scaling_factor;
    }
    break;

  case G_DESKTOP_BACKGROUND_STYLE_ZOOM:
    /* Zoom:
       The image is zoomed to fill the screen.
    */
    {
      float scaling_factor = MAX (monitor_width / state->texture_width,
                                  monitor_height / state->texture_height);
      image_width = state->texture_width * scaling_factor;
      image_height = state->texture_height * scaling_factor;
    }
    break;

  case G_DESKTOP_BACKGROUND_STYLE_CENTERED:
    /* Centered:
       The image is centered and not scaled.
    */
  case G_DESKTOP_BACKGROUND_STYLE_WALLPAPER:
    /* Wallpaper:
       The image is not scaled but tiled.
    */

    image_width = state->texture_width;
    image_height = state->texture_height;
    break;

  default:
    g_assert_not_reached ();
  }

  pipeline_copy = cogl_pipeline_copy (pipeline);
  cogl_pipeline_set_color4f (pipeline_copy,
                             color_factor,
                             color_factor,
                             color_factor,
                             alpha_factor);

  if (state->style == G_DESKTOP_BACKGROUND_STYLE_WALLPAPER)
    {
      for (i = 0; i < 4; i++)
        {
          vertices[i].s *= (monitor_width / image_width);
          vertices[i].t *= (monitor_height / image_height);
        }

      vertices[0].x = monitor_width;
      vertices[0].y = 0;
      vertices[1].x = 0;
      vertices[1].y = 0;
      vertices[2].x = monitor_width;
      vertices[2].y = monitor_height;
      vertices[3].x = 0;
      vertices[3].y = monitor_height;

      cogl_pipeline_set_layer_wrap_mode (pipeline_copy, 0, COGL_PIPELINE_WRAP_MODE_REPEAT);
    }
  else
    {
      /* Coordinates of the vertices, relative to the image */
      float x0, x1, y0, y1;

      x0 = (monitor_width - image_width) / 2;
      y0 = (monitor_height - image_height) / 2;
      x1 = x0 + image_width;
      y1 = y0 + image_height;

      vertices[0].x = x1;
      vertices[0].y = y0;
      vertices[1].x = x0;
      vertices[1].y = y0;
      vertices[2].x = x1;
      vertices[2].y = y1;
      vertices[3].x = x0;
      vertices[3].y = y1;
    }

  for (i = 0; i < 4; i++)
    {
      vertices[i].x *= (actor_width / screen_width);
      vertices[i].y *= (actor_height / screen_height);
    }

  primitive = cogl_primitive_new_p2t2 (context,
                                       COGL_VERTICES_MODE_TRIANGLE_STRIP,
                                       4, vertices);
  cogl_framebuffer_draw_primitive (cogl_get_draw_framebuffer (),
                                   pipeline_copy, primitive);
  cogl_object_unref (primitive);
  cogl_object_unref (pipeline_copy);
}

static void
paint_background_unclipped (MetaBackgroundActor *self,
                            MetaBackgroundState *state,
                            float                color_factor,
                            float                alpha_factor)
{
  MetaBackgroundActorPrivate *priv;
  int screen_width, screen_height;
  float actor_width, actor_height;
  ClutterActorBox monitor_box;

  priv = self->priv;
  clutter_actor_get_size (CLUTTER_ACTOR (self), &actor_width, &actor_height);
  meta_screen_get_size (priv->screen, &screen_width, &screen_height);

  if (state->style == G_DESKTOP_BACKGROUND_STYLE_SPANNED)
    {
      /* Prepare a box covering the screen */
      monitor_box.x1 = 0;
      monitor_box.y1 = 0;
      monitor_box.x2 = screen_width;
      monitor_box.y2 = screen_height;

      paint_gradient (state, priv->solid_pipeline,
                      color_factor, alpha_factor,
                      &monitor_box,
                      actor_width, actor_height,
                      screen_width, screen_height);
      paint_background_box (state, priv->pipeline,
                            color_factor, alpha_factor,
                            &monitor_box,
                            actor_width, actor_height,
                            screen_width, screen_height);
    }
  else
    {
      /* Iterate each monitor */
      int i, n;
      MetaRectangle monitor_rect;

      n = meta_screen_get_n_monitors (priv->screen);
      for (i = 0; i < n; i++)
        {
          meta_screen_get_monitor_geometry (priv->screen, i, &monitor_rect);
          monitor_box.x1 = monitor_rect.x;
          monitor_box.y1 = monitor_rect.y;
          monitor_box.x2 = monitor_rect.x + monitor_rect.width;
          monitor_box.y2 = monitor_rect.y + monitor_rect.height;

          /* Sad truth: we need to paint twice to get the border of the image
             right.

             A similar effect could be obtained with GL_CLAMP_TO_BORDER, but
             Cogl doesn't expose that.
          */
          paint_gradient (state, priv->solid_pipeline,
                          color_factor, alpha_factor,
                          &monitor_box,
                          actor_width, actor_height,
                          screen_width, screen_height);
          paint_background_box (state, priv->pipeline,
                                color_factor, alpha_factor,
                                &monitor_box,
                                actor_width, actor_height,
                                screen_width, screen_height);
        }
    }
}

static void
paint_background (MetaBackgroundActor *self,
                  MetaBackgroundState *state,
                  float                color_factor,
                  float                alpha_factor)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  cogl_pipeline_set_layer_texture (priv->pipeline, 0, state->texture);

  if (priv->visible_region)
    {
      int n_rectangles = cairo_region_num_rectangles (priv->visible_region);
      int i;

      cogl_path_new ();

      for (i = 0; i < n_rectangles; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (priv->visible_region, i, &rect);

          cogl_path_rectangle (rect.x,
                               rect.y,
                               rect.x + rect.width,
                               rect.y + rect.height);
        }

      cogl_clip_push_from_path ();
      paint_background_unclipped (self, state, color_factor, alpha_factor);
      cogl_clip_pop ();
    }
  else
    {
      paint_background_unclipped (self, state, color_factor, alpha_factor);
    }
}

static void
meta_background_actor_paint (ClutterActor *actor)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  guint8 opacity = clutter_actor_get_paint_opacity (actor);
  float crossfade_progress;
  float first_color_factor, first_alpha_factor,
    second_color_factor, second_alpha_factor;

  meta_background_ensure_rendered (priv->background);

  if (priv->is_crossfading)
    crossfade_progress = priv->crossfade_progress;
  else
    crossfade_progress = 1.0;

  first_color_factor = (opacity / 256.f) * priv->dim_factor * crossfade_progress;
  first_alpha_factor = (opacity / 256.f) * crossfade_progress;
  second_color_factor = (opacity / 256.f) * priv->dim_factor * (1 - crossfade_progress);
  second_alpha_factor = (opacity / 256.f) * (1 - crossfade_progress);

  if (priv->is_crossfading)
    {
      paint_background (self, &priv->background->old_state,
                        second_color_factor,
                        second_alpha_factor);
    }

  paint_background (self, &priv->background->state,
                    first_color_factor,
                    first_alpha_factor);
}

static gboolean
meta_background_actor_get_paint_volume (ClutterActor       *actor,
                                        ClutterPaintVolume *volume)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  int width, height;

  meta_screen_get_size (priv->screen, &width, &height);

  clutter_paint_volume_set_width (volume, width);
  clutter_paint_volume_set_height (volume, height);

  return TRUE;
}

static void
meta_background_actor_set_crossfade_progress (MetaBackgroundActor *self,
                                              gfloat               crossfade_progress)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  if (priv->crossfade_progress == crossfade_progress)
    return;

  priv->crossfade_progress = crossfade_progress;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CROSSFADE_PROGRESS]);
}

static void
meta_background_actor_set_dim_factor (MetaBackgroundActor *self,
                                      gfloat               dim_factor)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  if (priv->dim_factor == dim_factor)
    return;

  priv->dim_factor = dim_factor;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));

  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_DIM_FACTOR]);
}

static void
meta_background_actor_set_screen (MetaBackgroundActor *self,
                                  MetaScreen          *screen)
{
  self->priv->screen = screen;
  g_object_add_weak_pointer (G_OBJECT (screen), (void**) &self->priv->screen);

  g_signal_connect_object (screen, "monitors-changed",
                           G_CALLBACK (meta_background_actor_screen_size_changed), self, 0);
}

static void
meta_background_actor_set_settings (MetaBackgroundActor *self,
                                    GSettings           *settings)
{
  MetaBackgroundActorPrivate *priv;

  priv = self->priv;

  if (settings)
    priv->settings = g_object_ref (settings);
  else
    priv->settings = meta_background_get_default_settings ();
}

static void
meta_background_actor_get_property(GObject         *object,
                                   guint            prop_id,
                                   GValue          *value,
                                   GParamSpec      *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);
  MetaBackgroundActorPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_DIM_FACTOR:
      g_value_set_float (value, priv->dim_factor);
      break;
    case PROP_CROSSFADE_PROGRESS:
      g_value_set_float (value, priv->crossfade_progress);
      break;
    case PROP_SETTINGS:
      g_value_set_object (value, priv->settings);
      break;
    case PROP_SCREEN:
      g_value_set_object (value, priv->screen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_set_property(GObject         *object,
                                   guint            prop_id,
                                   const GValue    *value,
                                   GParamSpec      *pspec)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (object);

  switch (prop_id)
    {
    case PROP_DIM_FACTOR:
      meta_background_actor_set_dim_factor (self, g_value_get_float (value));
      break;
    case PROP_CROSSFADE_PROGRESS:
      meta_background_actor_set_crossfade_progress (self, g_value_get_float (value));
      break;
    case PROP_SETTINGS:
      meta_background_actor_set_settings (self, G_SETTINGS (g_value_get_object (value)));
      break;
    case PROP_SCREEN:
      meta_background_actor_set_screen (self, META_SCREEN (g_value_get_object (value)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_background_actor_class_init (MetaBackgroundActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (MetaBackgroundActorPrivate));

  object_class->dispose = meta_background_actor_dispose;
  object_class->get_property = meta_background_actor_get_property;
  object_class->set_property = meta_background_actor_set_property;
  object_class->constructed = meta_background_actor_constructed;

  actor_class->get_preferred_width = meta_background_actor_get_preferred_width;
  actor_class->get_preferred_height = meta_background_actor_get_preferred_height;
  actor_class->paint = meta_background_actor_paint;
  actor_class->get_paint_volume = meta_background_actor_get_paint_volume;

  /**
   * MetaBackgroundActor:screen:
   *
   * The #MetaScreen this actor is operating on.
   */
  pspec = g_param_spec_object ("screen",
                               "Screen",
                               "The screen the actor is on",
                               META_TYPE_SCREEN,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SCREEN] = pspec;

  /**
   * MetaBackgroundActor:settings:
   *
   * The #GSettings object holding settings for this background.
   */
  pspec = g_param_spec_object ("settings",
                               "Settings",
                               "Object holding required information to render a background",
                               G_TYPE_SETTINGS,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SETTINGS] = pspec;

  /**
   * MetaBackgroundActor:dim-factor:
   *
   * Factor to dim the background by, between 0.0 (black) and 1.0 (original
   * colors)
   */
  pspec = g_param_spec_float ("dim-factor",
                              "Dim factor",
                              "Factor to dim the background by",
                              0.0, 1.0,
                              1.0,
                              G_PARAM_READWRITE);
  obj_props[PROP_DIM_FACTOR] = pspec;

  /**
   * MetaBackgroundActor:crossfade-progress: (skip)
   */
  pspec = g_param_spec_float ("crossfade-progress",
                              "", "",
                              0.0, 1.0,
                              1.0,
                              G_PARAM_READWRITE);
  obj_props[PROP_CROSSFADE_PROGRESS] = pspec;

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

static void
meta_background_actor_init (MetaBackgroundActor *self)
{
  MetaBackgroundActorPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   META_TYPE_BACKGROUND_ACTOR,
                                                   MetaBackgroundActorPrivate);
  priv->dim_factor = 1.0;
  priv->crossfade_progress = 0.0;
}

static void
meta_background_actor_constructed (GObject *object)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  CoglContext *context = clutter_backend_get_cogl_context (backend);
  MetaBackgroundActor *self;
  MetaBackgroundActorPrivate *priv;

  G_OBJECT_CLASS (meta_background_actor_parent_class)->constructed (object);

  self = META_BACKGROUND_ACTOR (object);
  priv = self->priv;

  priv->background = meta_background_get (priv->screen, priv->settings);
  priv->background->actors = g_slist_prepend (priv->background->actors, self);

  priv->solid_pipeline = cogl_pipeline_new (context);
  priv->pipeline = meta_create_texture_material (COGL_INVALID_HANDLE);
  cogl_pipeline_set_layer_filters (priv->pipeline, 0,
                                   COGL_MATERIAL_FILTER_LINEAR_MIPMAP_LINEAR,
                                   COGL_MATERIAL_FILTER_LINEAR_MIPMAP_LINEAR);
  update_actor_pipeline (self, FALSE);
}

/**
 * meta_background_actor_new:
 * @screen: the #MetaScreen
 * @settings: (allow-none): a #GSettings object holding the background configuration,
 *            or %NULL to pick the default one.
 *
 * Creates a new actor to draw the background for the given screen.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
meta_background_actor_new (MetaScreen *screen,
                           GSettings  *settings)
{
  g_return_val_if_fail (META_IS_SCREEN (screen), NULL);

  return g_object_new (META_TYPE_BACKGROUND_ACTOR,
                       "screen", screen,
                       "settings", settings,
                       NULL);
}

static void
on_background_drawn (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  MetaBackground *background;
  CoglHandle texture;
  GError *error;
  char *picture_uri;

  error = NULL;
  texture = meta_background_draw_finish (META_SCREEN (object), result, &picture_uri, &error);

  /* Don't even access user_data if cancelled, it might be already
     freed */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_error_free (error);
      return;
    }

  background = user_data;

  g_clear_object (&background->rendering_task);
  g_clear_object (&background->cancellable);

  if (texture != COGL_INVALID_HANDLE)
    {
      set_texture (background, texture, picture_uri);
      cogl_handle_unref (texture);
      g_free (picture_uri);
      return;
    }
  else
    {
      g_warning ("Failed to render background: %s",
                 error->message);
      g_error_free (error);
    }
}

/*
 * meta_background_update:
 * @background: a #MetaBackground
 * @picture_uri: the new image URI
 *
 * Forces a redraw of the background. The redraw happens asynchronously in
 * a thread, and the actual on screen change is therefore delayed until
 * the redraw is finished.
 */
void
meta_background_update (MetaBackground *background,
                        const char     *picture_uri)
{
  if (g_strcmp0 (background->state.picture_uri, picture_uri) == 0)
    return;

  if (background->cancellable)
    {
      g_cancellable_cancel (background->cancellable);
      g_object_unref (background->cancellable);
    }

  g_clear_object (&background->rendering_task);

  background->cancellable = g_cancellable_new ();

  background->rendering_task = meta_background_draw_async (background->screen,
                                                           picture_uri,
                                                           background->cancellable,
                                                           on_background_drawn, background);
}

/**
 * meta_background_actor_set_visible_region:
 * @self: a #MetaBackgroundActor
 * @visible_region: (allow-none): the area of the actor (in allocate-relative
 *   coordinates) that is visible.
 *
 * Sets the area of the background that is unobscured by overlapping windows.
 * This is used to optimize and only paint the visible portions.
 */
void
meta_background_actor_set_visible_region (MetaBackgroundActor *self,
                                          cairo_region_t      *visible_region)
{
  MetaBackgroundActorPrivate *priv;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (self));

  priv = self->priv;

  if (priv->visible_region)
    {
      cairo_region_destroy (priv->visible_region);
      priv->visible_region = NULL;
    }

  if (visible_region)
    {
      cairo_rectangle_int_t screen_rect = { 0 };
      meta_screen_get_size (priv->screen, &screen_rect.width, &screen_rect.height);

      /* Doing the intersection here is probably unnecessary - MetaWindowGroup
       * should never compute a visible area that's larger than the root screen!
       * but it's not that expensive and adds some extra robustness.
       */
      priv->visible_region = cairo_region_create_rectangle (&screen_rect);
      cairo_region_intersect (priv->visible_region, visible_region);
    }
}

static void
meta_background_actor_screen_size_changed (MetaScreen          *screen,
                                           MetaBackgroundActor *actor)
{
  clutter_actor_queue_relayout (CLUTTER_ACTOR (actor));
}

/**
 * meta_background_actor_add_glsl_snippet:
 * @actor: a #MetaBackgroundActor
 * @hook: where to insert the code
 * @declarations: GLSL declarations
 * @code: GLSL code
 * @is_replace: wheter Cogl code should be replaced by the custom shader
 *
 * Adds a GLSL snippet to the pipeline used for drawing the background.
 * See #CoglSnippet for details.
 */
void
meta_background_actor_add_glsl_snippet (MetaBackgroundActor *actor,
                                        MetaSnippetHook      hook,
                                        const char          *declarations,
                                        const char          *code,
                                        gboolean             is_replace)
{
  MetaBackgroundActorPrivate *priv;
  CoglSnippet *snippet;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (actor));

  priv = actor->priv;

  if (is_replace)
    {
      snippet = cogl_snippet_new (hook, declarations, NULL);
      cogl_snippet_set_replace (snippet, code);
    }
  else
    {
      snippet = cogl_snippet_new (hook, declarations, code);
    }

  if (hook == META_SNIPPET_HOOK_VERTEX ||
      hook == META_SNIPPET_HOOK_FRAGMENT)
    {
      cogl_pipeline_add_snippet (priv->pipeline, snippet);
    }
  else
    {
      cogl_pipeline_add_layer_snippet (priv->pipeline, 0, snippet);
    }

  cogl_object_unref (snippet);
}

/**
 * meta_background_actor_set_uniform_float:
 * @actor: a #MetaBackgroundActor
 * @uniform_name:
 * @n_components: number of components (for vector uniforms)
 * @count: number of uniforms (for array uniforms)
 * @uniform: (array length=uniform_length): the float values to set
 * @uniform_length: the length of @uniform. Must be exactly @n_components x @count,
 *                  and is provided mainly for language bindings.
 *
 * Sets a new GLSL uniform to the provided value. This is mostly
 * useful in congiunction with meta_background_actor_add_glsl_snippet().
 */

void
meta_background_actor_set_uniform_float (MetaBackgroundActor *actor,
                                         const char          *uniform_name,
                                         int                  n_components,
                                         int                  count,
                                         const float         *uniform,
                                         int                  uniform_length)
{
  MetaBackgroundActorPrivate *priv;

  g_return_if_fail (META_IS_BACKGROUND_ACTOR (actor));
  g_return_if_fail (uniform_length == n_components * count);

  priv = actor->priv;

  cogl_pipeline_set_uniform_float (priv->pipeline,
                                   cogl_pipeline_get_uniform_location (priv->pipeline,
                                                                       uniform_name),
                                   n_components, count, uniform);
}

