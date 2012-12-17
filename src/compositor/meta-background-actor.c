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

#include <cogl/cogl-texture-pixmap-x11.h>

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

struct _MetaBackground
{
  GSList *actors;

  MetaScreen   *screen;
  GCancellable *cancellable;

  float texture_width;
  float texture_height;
  CoglTexture *old_texture;
  CoglTexture *texture;

  GTask *rendering_task;
};

struct _MetaBackgroundActorPrivate
{
  MetaScreen     *screen;
  MetaBackground *background;
  GnomeBG        *settings;

  CoglPipeline *single_pipeline;
  CoglPipeline *crossfade_pipeline;
  CoglPipeline *pipeline;
  CoglMaterialWrapMode wrap_mode;

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

static void meta_background_update                    (GnomeBG             *bg,
                                                       MetaBackground      *background);
static void meta_background_actor_screen_size_changed (MetaScreen          *screen,
                                                       MetaBackgroundActor *actor);
static void meta_background_actor_constructed         (GObject             *object);

static void clear_old_texture          (MetaBackground *background);
static void set_texture                (MetaBackground *background,
                                        CoglHandle      texture);

static void
meta_background_free (MetaBackground *background)
{
  set_texture (background, COGL_INVALID_HANDLE);

  g_cancellable_cancel (background->cancellable);
  g_object_unref (background->cancellable);

  g_clear_object (&background->rendering_task);

  g_slice_free (MetaBackground, background);
}

static void
on_settings_changed (GSettings   *settings,
                     const char  *key,
                     GnomeBG     *bg)
{
  gnome_bg_load_from_preferences (bg, settings);
}

static GnomeBG *
meta_background_get_default_settings (void)
{
  GSettings *settings;
  GnomeBG *bg;

  settings = g_settings_new ("org.gnome.desktop.background");
  bg = gnome_bg_new ();

  g_signal_connect (settings, "changed",
                    G_CALLBACK (on_settings_changed), bg);
  on_settings_changed (settings, NULL, bg);

  /* Just to keep settings alive */
  g_object_set_data_full (G_OBJECT (bg), "g-settings",
                          g_object_ref (settings), g_object_unref);
  g_object_unref (settings);

  return bg;
}

static MetaBackground *
meta_background_get (MetaScreen *screen,
                     GnomeBG    *bg)
{
  static GQuark background_quark;
  MetaBackground *background;

  if (G_UNLIKELY (background_quark == 0))
    background_quark = g_quark_from_static_string ("meta-background");

  background = g_object_get_qdata (G_OBJECT (bg), background_quark);
  if (G_UNLIKELY (background == NULL))
    {
      background = g_slice_new0 (MetaBackground);
      g_object_set_qdata_full (G_OBJECT (bg), background_quark,
                               background, (GDestroyNotify) meta_background_free);

      background->screen = screen;
      background->texture_width = -1;
      background->texture_height = -1;

      g_signal_connect (bg, "transitioned",
                        G_CALLBACK (meta_background_update), background);
      g_signal_connect (bg, "changed",
                        G_CALLBACK (meta_background_update), background);

      /* GnomeBG has queued a changed event, but we need to start rendering now,
         or it will be too late when we paint the first frame.
      */
      g_object_set_data (G_OBJECT (bg), "ignore-pending-change", GINT_TO_POINTER (TRUE));
      meta_background_update (bg, background);
    }

  return background;
}

static void
update_actor_pipeline (MetaBackgroundActor *self,
                       gboolean             crossfade)
{
  MetaBackgroundActorPrivate *priv = self->priv;

  if (crossfade)
    {
      priv->pipeline = priv->crossfade_pipeline;
      priv->is_crossfading = TRUE;

      cogl_pipeline_set_layer_texture (priv->pipeline, 0, priv->background->old_texture);
      cogl_pipeline_set_layer_wrap_mode (priv->pipeline, 0, priv->wrap_mode);

      cogl_pipeline_set_layer_texture (priv->pipeline, 1, priv->background->texture);
      cogl_pipeline_set_layer_wrap_mode (priv->pipeline, 1, priv->wrap_mode);
    }
  else
    {
      priv->pipeline = priv->single_pipeline;
      priv->is_crossfading = FALSE;

      cogl_pipeline_set_layer_texture (priv->pipeline, 0, priv->background->texture);
      cogl_pipeline_set_layer_wrap_mode (priv->pipeline, 0, priv->wrap_mode);
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

static void
crossfade_completed (ClutterTimeline     *timeline,
                     MetaBackgroundActor *actor)
{
  clear_old_texture (actor->priv->background);
  update_actor_pipeline (actor, FALSE);
}

static void
clear_old_texture (MetaBackground *background)
{
  if (background->old_texture != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (background->old_texture);
      background->old_texture = COGL_INVALID_HANDLE;
    }
}

static void
set_texture (MetaBackground *background,
             CoglHandle      texture)
{
  GSList *l;
  gboolean crossfade;
  int width, height;
  CoglMaterialWrapMode wrap_mode;

  if (background->old_texture != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (background->old_texture);
      background->old_texture = COGL_INVALID_HANDLE;
    }

  if (texture != COGL_INVALID_HANDLE)
    {
      background->old_texture = background->texture;
      background->texture = cogl_handle_ref (texture);
    }
  else if (background->texture != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (background->texture);
      background->texture = COGL_INVALID_HANDLE;
    }

  if (texture != COGL_INVALID_HANDLE &&
      background->old_texture != COGL_INVALID_HANDLE)
    crossfade = TRUE;
  else
    crossfade = FALSE;

  background->texture_width = cogl_texture_get_width (background->texture);
  background->texture_height = cogl_texture_get_height (background->texture);

  meta_screen_get_size (background->screen, &width, &height);

  /* We turn off repeating when we have a full-screen pixmap to keep from
   * getting artifacts from one side of the image sneaking into the other
   * side of the image via bilinear filtering.
   */
  if (width == background->texture_width && height == background->texture_height)
    wrap_mode = COGL_MATERIAL_WRAP_MODE_CLAMP_TO_EDGE;
  else
    wrap_mode = COGL_MATERIAL_WRAP_MODE_REPEAT;

  for (l = background->actors; l; l = l->next)
    {
      MetaBackgroundActor *actor = l->data;

      actor->priv->wrap_mode = wrap_mode;
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
                background->texture != COGL_INVALID_HANDLE))
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

  g_clear_pointer (&priv->single_pipeline, cogl_object_unref);
  g_clear_pointer (&priv->crossfade_pipeline, cogl_object_unref);
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
meta_background_actor_paint (ClutterActor *actor)
{
  MetaBackgroundActor *self = META_BACKGROUND_ACTOR (actor);
  MetaBackgroundActorPrivate *priv = self->priv;
  guint8 opacity = clutter_actor_get_paint_opacity (actor);
  guint8 color_component;
  int width, height;
  CoglColor crossfade_color;

  meta_background_ensure_rendered (priv->background);

  meta_screen_get_size (priv->screen, &width, &height);

  color_component = (int)(0.5 + opacity * priv->dim_factor);

  cogl_pipeline_set_color4ub (priv->pipeline,
                              color_component,
                              color_component,
                              color_component,
                              opacity);

  if (priv->is_crossfading)
    {
      cogl_color_init_from_4f (&crossfade_color,
                               priv->crossfade_progress,
                               priv->crossfade_progress,
                               priv->crossfade_progress,
                               priv->crossfade_progress);
      cogl_pipeline_set_layer_combine_constant (priv->pipeline,
                                                1, &crossfade_color);
    }

  cogl_set_source (priv->pipeline);

  if (priv->visible_region)
    {
      int n_rectangles = cairo_region_num_rectangles (priv->visible_region);
      int i;

      for (i = 0; i < n_rectangles; i++)
        {
          cairo_rectangle_int_t rect;
          cairo_region_get_rectangle (priv->visible_region, i, &rect);

          cogl_rectangle_with_texture_coords (rect.x, rect.y,
                                              rect.x + rect.width, rect.y + rect.height,
                                              rect.x / priv->background->texture_width,
                                              rect.y / priv->background->texture_height,
                                              (rect.x + rect.width) / priv->background->texture_width,
                                              (rect.y + rect.height) / priv->background->texture_height);
        }
    }
  else
    {
      cogl_rectangle_with_texture_coords (0.0f, 0.0f,
                                          width, height,
                                          0.0f, 0.0f,
                                          width / priv->background->texture_width,
                                          height / priv->background->texture_height);
    }
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
                                    GnomeBG             *settings)
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
      meta_background_actor_set_settings (self, GNOME_BG (g_value_get_object (value)));
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
   * The #GnomeBG object holding settings for this background.
   */
  pspec = g_param_spec_object ("settings",
                               "Settings",
                               "Object holding required information to render a background",
                               GNOME_TYPE_BG,
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
  priv->wrap_mode = COGL_MATERIAL_WRAP_MODE_REPEAT;
}

static void
meta_background_actor_constructed (GObject *object)
{
  MetaBackgroundActor *self;
  MetaBackgroundActorPrivate *priv;

  G_OBJECT_CLASS (meta_background_actor_parent_class)->constructed (object);

  self = META_BACKGROUND_ACTOR (object);
  priv = self->priv;

  priv->background = meta_background_get (priv->screen, priv->settings);
  priv->background->actors = g_slist_prepend (priv->background->actors, self);

  priv->single_pipeline = meta_create_texture_material (priv->background->texture);
  priv->crossfade_pipeline = meta_create_crossfade_material (priv->background->old_texture,
                                                             priv->background->texture);

  if (priv->background->texture != COGL_INVALID_HANDLE)
    update_actor_pipeline (self, FALSE);
}

/**
 * meta_background_actor_new:
 * @screen: the #MetaScreen
 * @settings: (allow-none): a #GnomeBG holding the background configuration,
 *            or %NULL to pick the default one.
 *
 * Creates a new actor to draw the background for the given screen.
 *
 * Return value: the newly created background actor
 */
ClutterActor *
meta_background_actor_new (MetaScreen *screen,
                           GnomeBG    *settings)
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

  error = NULL;
  texture = meta_background_draw_finish (META_SCREEN (object), result, &error);

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
      set_texture (background, texture);
      cogl_handle_unref (texture);
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
 * @bg: the #GnomeBG that triggered the update
 * @background: a #MetaBackground
 *
 * Forces a redraw of the background. The redraw happens asynchronously in
 * a thread, and the actual on screen change is therefore delayed until
 * the redraw is finished.
 */
void
meta_background_update (GnomeBG        *bg,
                        MetaBackground *background)
{
  if (background->cancellable)
    {
      g_cancellable_cancel (background->cancellable);
      g_object_unref (background->cancellable);
    }

  g_clear_object (&background->rendering_task);

  background->cancellable = g_cancellable_new ();
  background->rendering_task = meta_background_draw_async (background->screen,
                                                           bg,
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
  MetaBackgroundActorPrivate *priv;
  MetaBackground *background;
  int width, height;

  priv = actor->priv;
  background = priv->background;

  meta_screen_get_size (screen, &width, &height);

  if (width == background->texture_width && height == background->texture_height)
    priv->wrap_mode = COGL_MATERIAL_WRAP_MODE_CLAMP_TO_EDGE;
  else
    priv->wrap_mode = COGL_MATERIAL_WRAP_MODE_REPEAT;

  update_actor_pipeline (actor, actor->priv->is_crossfading);
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
      cogl_pipeline_add_snippet (priv->single_pipeline, snippet);
      cogl_pipeline_add_snippet (priv->crossfade_pipeline, snippet);
    }
  else
    {
      /* In case of crossfading, apply the snippet only to the new texture.
         We can't apply it to both because declarations would be doubled. */
      cogl_pipeline_add_layer_snippet (priv->single_pipeline, 0, snippet);
      cogl_pipeline_add_layer_snippet (priv->crossfade_pipeline, 1, snippet);
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

  cogl_pipeline_set_uniform_float (priv->single_pipeline,
                                   cogl_pipeline_get_uniform_location (priv->single_pipeline,
                                                                       uniform_name),
                                   n_components, count, uniform);
  cogl_pipeline_set_uniform_float (priv->crossfade_pipeline,
                                   cogl_pipeline_get_uniform_location (priv->crossfade_pipeline,
                                                                       uniform_name),
                                   n_components, count, uniform);
}

