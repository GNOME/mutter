/*
 * Copyright (C) 2026 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "clutter-motion-controller.h"

#include "clutter/clutter-backend.h"
#include "clutter/clutter-context.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-sprite.h"
#include "clutter/clutter-stage.h"

enum
{
  ENTER,
  MOTION,
  LEAVE,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };

struct _ClutterMotionController
{
  ClutterAction parent_instance;
};

G_DEFINE_TYPE (ClutterMotionController,
               clutter_motion_controller,
               CLUTTER_TYPE_ACTION)

static gboolean
clutter_motion_controller_handle_event (ClutterAction      *action,
                                        const ClutterEvent *event)
{
  ClutterEventType evtype = clutter_event_type (event);

  if (evtype == CLUTTER_MOTION ||
      evtype == CLUTTER_ENTER ||
      evtype == CLUTTER_LEAVE)
    {
      ClutterActor *actor =
        clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
      ClutterStage *stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
      ClutterContext *context = clutter_actor_get_context (actor);
      ClutterBackend *backend = clutter_context_get_backend (context);
      ClutterSprite *sprite = clutter_backend_get_sprite (backend, stage, event);
      float x, y;

      clutter_event_get_coords (event, &x, &y);
      clutter_actor_transform_stage_point (actor, x, y, &x, &y);

      if (evtype == CLUTTER_ENTER)
        g_signal_emit (action, signals[ENTER], 0, sprite, x, y);
      else if (evtype == CLUTTER_MOTION)
        g_signal_emit (action, signals[MOTION], 0, sprite, x, y);
      else if (evtype == CLUTTER_LEAVE)
        g_signal_emit (action, signals[LEAVE], 0, sprite);
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_motion_controller_class_init (ClutterMotionControllerClass *klass)
{
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  action_class->handle_event = clutter_motion_controller_handle_event;

  /**
   * ClutterMotionController::enter:
   * @motion_controller: the motion controller
   * @sprite: the sprite in motion
   * @x: the position in the X axis, in actor-relative coordinates
   * @y: the position in the Y axis, in actor-relative coordinates
   *
   * Emitted when @sprite enters into the actor.
   */
  signals[ENTER] =
    g_signal_new ("enter",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_SPRITE,
                  G_TYPE_FLOAT,
                  G_TYPE_FLOAT);
  /**
   * ClutterMotionController::motion:
   * @motion_controller: the motion controller
   * @sprite: the sprite in motion
   * @x: the position in the X axis, in actor-relative coordinates
   * @y: the position in the Y axis, in actor-relative coordinates
   *
   * Emitted when @sprite moves across actor
   */
  signals[MOTION] =
    g_signal_new ("motion",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_SPRITE,
                  G_TYPE_FLOAT,
                  G_TYPE_FLOAT);
  /**
   * ClutterMotionController::leave:
   * @motion_controller: the motion controller
   * @sprite: the sprite in motion
   *
   * Emitted when @sprite leaves the actor
   */
  signals[LEAVE] =
    g_signal_new ("leave",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_SPRITE);
}

static void
clutter_motion_controller_init (ClutterMotionController *controller)
{
}

/**
 * clutter_motion_controller_new:
 *
 * Returns a newly created motion controller. This object can
 * be used for motion tracking inside an actor.
 *
 * Returns: a new motion controller
 **/
ClutterAction *
clutter_motion_controller_new (void)
{
  return g_object_new (CLUTTER_TYPE_MOTION_CONTROLLER, NULL);
}
