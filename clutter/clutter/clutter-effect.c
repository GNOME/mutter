/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

/**
 * ClutterEffect:
 *
 * Base class for actor effects
 *
 * The #ClutterEffect class provides a default type and API for creating
 * effects for generic actors.
 *
 * Effects are a #ClutterActorMeta sub-class that modify the way an actor
 * is painted in a way that is not part of the actor's implementation.
 *
 * Effects should be the preferred way to affect the paint sequence of an
 * actor without sub-classing the actor itself and overriding the
 * [vfunc@Clutter.Actor.paint] virtual function.
 *
 * ## Implementing a ClutterEffect
 *
 * Creating a sub-class of #ClutterEffect requires overriding the
 * [vfunc@Clutter.Effect.paint] method. The implementation of the function should look
 * something like this:
 *
 * ```c
 * void effect_paint (ClutterEffect *effect, ClutterEffectPaintFlags flags)
 * {
 *   // Set up initialisation of the paint such as binding a
 *   // CoglOffscreen or other operations
 *
 *   // Chain to the next item in the paint sequence. This will either call
 *   // ‘paint’ on the next effect or just paint the actor if this is
 *   // the last effect.
 *   ClutterActor *actor =
 *     clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (effect));
 *
 *   clutter_actor_continue_paint (actor);
 *
 *   // perform any cleanup of state, such as popping the CoglOffscreen
 * }
 * ```
 *
 * The effect can optionally avoid calling clutter_actor_continue_paint() to skip any
 * further stages of the paint sequence. This is useful for example if the effect
 * contains a cached image of the actor. In that case it can optimise painting by
 * avoiding the actor paint and instead painting the cached image.
 *
 * The %CLUTTER_EFFECT_PAINT_ACTOR_DIRTY flag is useful in this case. Clutter will set
 * this flag when a redraw has been queued on the actor since it was last painted. The
 * effect can use this information to decide if the cached image is still valid.
 *
 * ## A simple ClutterEffect implementation
 *
 * The example below creates two rectangles: one will be painted "behind" the actor,
 * while another will be painted "on top" of the actor.
 *
 * The #ClutterActorMetaClass.set_actor() implementation will create the two pipelines
 * used for the two different rectangles; the #ClutterEffectClass.paint() implementation
 * will paint the first pipeline using cogl_rectangle(), before continuing and then it
 * will paint paint the second pipeline after.
 *
 * ```c
 *  typedef struct {
 *    ClutterEffect parent_instance;
 *
 *    CoglPipeline *rect_1;
 *    CoglPipeline *rect_2;
 *  } MyEffect;
 *
 *  typedef struct _ClutterEffectClass MyEffectClass;
 *
 *  G_DEFINE_TYPE (MyEffect, my_effect, CLUTTER_TYPE_EFFECT);
 *
 *  static void
 *  my_effect_set_actor (ClutterActorMeta *meta,
 *                       ClutterActor     *actor)
 *  {
 *    MyEffect *self = MY_EFFECT (meta);
 *    CoglColor color;
 *
 *    // Clear the previous state //
 *    if (self->rect_1)
 *      {
 *        g_object_unref (self->rect_1);
 *        self->rect_1 = NULL;
 *      }
 *
 *    if (self->rect_2)
 *      {
 *        g_object_unref (self->rect_2);
 *        self->rect_2 = NULL;
 *      }
 *
 *    // Maintain a pointer to the actor
 *    self->actor = actor;
 *
 *    // If we've been detached by the actor then we should just bail out here
 *    if (self->actor == NULL)
 *      return;
 *
 *    // Create a red pipeline
 *    self->rect_1 = cogl_pipeline_new ();
 *    cogl_color_init_from_4f (&color, 1.0, 1.0, 1.0, 1.0);
 *    cogl_pipeline_set_color (self->rect_1, &color);
 *
 *    // Create a green pipeline
 *    self->rect_2 = cogl_pipeline_new ();
 *    cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
 *    cogl_pipeline_set_color (self->rect_2, &color);
 *  }
 *
 *  static gboolean
 *  my_effect_paint (ClutterEffect *effect)
 *  {
 *    MyEffect *self = MY_EFFECT (effect);
 *    gfloat width, height;
 *
 *    clutter_actor_get_size (self->actor, &width, &height);
 *
 *    // Paint the first rectangle in the upper left quadrant
 *    cogl_set_source (self->rect_1);
 *    cogl_rectangle (0, 0, width / 2, height / 2);
 *
 *    // Continue to the rest of the paint sequence
 *    clutter_actor_continue_paint (self->actor);
 *
 *    // Paint the second rectangle in the lower right quadrant
 *    cogl_set_source (self->rect_2);
 *    cogl_rectangle (width / 2, height / 2, width, height);
 *  }
 *
 *  static void
 *  my_effect_class_init (MyEffectClass *klass)
 *  {
 *    ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
 *
 *    meta_class->set_actor = my_effect_set_actor;
 *
 *    klass->paint = my_effect_paint;
 *  }
 * ```
 */

#include "config.h"

#include "clutter/clutter-effect.h"

#include "clutter/clutter-actor-meta-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-effect-private.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-paint-node-private.h"
#include "clutter/clutter-paint-nodes.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-actor-private.h"

G_DEFINE_ABSTRACT_TYPE (ClutterEffect,
                        clutter_effect,
                        CLUTTER_TYPE_ACTOR_META);

static gboolean
clutter_effect_real_pre_paint (ClutterEffect       *effect,
                               ClutterPaintNode    *node,
                               ClutterPaintContext *paint_context)
{
  return TRUE;
}

static void
clutter_effect_real_post_paint (ClutterEffect       *effect,
                                ClutterPaintNode    *node,
                                ClutterPaintContext *paint_context)
{
}

static gboolean
clutter_effect_real_modify_paint_volume (ClutterEffect      *effect,
                                         ClutterPaintVolume *volume)
{
  return TRUE;
}

static void
add_actor_node (ClutterEffect    *effect,
                ClutterPaintNode *node)
{
  ClutterPaintNode *actor_node;
  ClutterActor *actor;

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (effect));

  actor_node = clutter_actor_node_new (actor, -1);
  clutter_paint_node_add_child (node, actor_node);
  clutter_paint_node_unref (actor_node);
}

static void
clutter_effect_real_paint_node (ClutterEffect           *effect,
                                ClutterPaintNode        *node,
                                ClutterPaintContext     *paint_context,
                                ClutterEffectPaintFlags  flags)
{
  add_actor_node (effect, node);
}

static void
clutter_effect_real_paint (ClutterEffect           *effect,
                           ClutterPaintNode        *node,
                           ClutterPaintContext     *paint_context,
                           ClutterEffectPaintFlags  flags)
{
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_GET_CLASS (effect);
  gboolean pre_paint_succeeded;

  /* The default implementation provides a compatibility wrapper for
     effects that haven't migrated to use the 'paint' virtual yet. This
     just calls the old pre and post virtuals before chaining on */

  pre_paint_succeeded = effect_class->pre_paint (effect, node,paint_context);

  if (pre_paint_succeeded)
    {
      effect_class->paint_node (effect, node, paint_context, flags);
      effect_class->post_paint (effect, node, paint_context);
    }
  else
    {
      /* Just paint the actor as fallback */
      add_actor_node (effect, node);
    }
}

static void
clutter_effect_real_pick (ClutterEffect      *effect,
                          ClutterPickContext *pick_context)
{
  ClutterActorMeta *actor_meta = CLUTTER_ACTOR_META (effect);
  ClutterActor *actor;

  actor = clutter_actor_meta_get_actor (actor_meta);
  clutter_actor_continue_pick (actor, pick_context);
}

static void
clutter_effect_set_enabled (ClutterActorMeta *meta,
                            gboolean          is_enabled)
{
  ClutterActorMetaClass *parent_class =
    CLUTTER_ACTOR_META_CLASS (clutter_effect_parent_class);
  ClutterActor *actor;

  actor = clutter_actor_meta_get_actor (meta);
  if (actor)
    clutter_actor_queue_redraw (actor);

  parent_class->set_enabled (meta, is_enabled);
}

static void
clutter_effect_class_init (ClutterEffectClass *klass)
{
  ClutterActorMetaClass *actor_meta_class = CLUTTER_ACTOR_META_CLASS (klass);

  actor_meta_class->set_enabled = clutter_effect_set_enabled;

  klass->pre_paint = clutter_effect_real_pre_paint;
  klass->post_paint = clutter_effect_real_post_paint;
  klass->modify_paint_volume = clutter_effect_real_modify_paint_volume;
  klass->paint = clutter_effect_real_paint;
  klass->paint_node = clutter_effect_real_paint_node;
  klass->pick = clutter_effect_real_pick;
}

static void
clutter_effect_init (ClutterEffect *self)
{
}

void
_clutter_effect_paint (ClutterEffect           *effect,
                       ClutterPaintNode        *node,
                       ClutterPaintContext     *paint_context,
                       ClutterEffectPaintFlags  flags)
{
  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  CLUTTER_EFFECT_GET_CLASS (effect)->paint (effect,
                                            node,
                                            paint_context,
                                            flags);
}

void
_clutter_effect_pick (ClutterEffect      *effect,
                      ClutterPickContext *pick_context)
{
  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  CLUTTER_EFFECT_GET_CLASS (effect)->pick (effect, pick_context);
}

gboolean
_clutter_effect_modify_paint_volume (ClutterEffect      *effect,
                                     ClutterPaintVolume *volume)
{
  g_return_val_if_fail (CLUTTER_IS_EFFECT (effect), FALSE);
  g_return_val_if_fail (volume != NULL, FALSE);

  return CLUTTER_EFFECT_GET_CLASS (effect)->modify_paint_volume (effect,
                                                                 volume);
}

gboolean
_clutter_effect_has_custom_paint_volume (ClutterEffect *effect)
{
  g_return_val_if_fail (CLUTTER_IS_EFFECT (effect), FALSE);

  return CLUTTER_EFFECT_GET_CLASS (effect)->modify_paint_volume != clutter_effect_real_modify_paint_volume;
}

/**
 * clutter_effect_queue_repaint:
 * @effect: A #ClutterEffect which needs redrawing
 *
 * Queues a repaint of the effect. The effect can detect when the ‘paint’
 * method is called as a result of this function because it will not
 * have the %CLUTTER_EFFECT_PAINT_ACTOR_DIRTY flag set. In that case the
 * effect is free to assume that the actor has not changed its
 * appearance since the last time it was painted so it doesn't need to
 * call clutter_actor_continue_paint() if it can draw a cached
 * image. This is mostly intended for effects that are using a
 * %CoglOffscreen to redirect the actor (such as
 * %ClutterOffscreenEffect). In that case the effect can save a bit of
 * rendering time by painting the cached texture without causing the
 * entire actor to be painted.
 *
 * This function can be used by effects that have their own animatable
 * parameters. For example, an effect which adds a varying degree of a
 * red tint to an actor by redirecting it through a CoglOffscreen
 * might have a property to specify the level of tint. When this value
 * changes, the underlying actor doesn't need to be redrawn so the
 * effect can call clutter_effect_queue_repaint() to make sure the
 * effect is repainted.
 *
 * Note however that modifying the position of the parent of an actor
 * may change the appearance of the actor because its transformation
 * matrix would change. In this case a redraw wouldn't be queued on
 * the actor itself so the %CLUTTER_EFFECT_PAINT_ACTOR_DIRTY would still
 * not be set. The effect can detect this case by keeping track of the
 * last modelview matrix that was used to render the actor and
 * verifying that it remains the same in the next paint.
 *
 * Any other effects that are layered on top of the passed in effect
 * will still be passed the %CLUTTER_EFFECT_PAINT_ACTOR_DIRTY flag. If
 * anything queues a redraw on the actor without specifying an effect
 * or with an effect that is lower in the chain of effects than this
 * one then that will override this call. In that case this effect
 * will instead be called with the %CLUTTER_EFFECT_PAINT_ACTOR_DIRTY
 * flag set.
 */
void
clutter_effect_queue_repaint (ClutterEffect *effect)
{
  ClutterActor *actor;

  g_return_if_fail (CLUTTER_IS_EFFECT (effect));

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (effect));

  /* If the effect has no actor then nothing needs to be done */
  if (actor != NULL)
    _clutter_actor_queue_redraw_full (actor,
                                      NULL, /* clip volume */
                                      effect /* effect */);
}
