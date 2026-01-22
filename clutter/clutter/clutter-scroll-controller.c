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

#include "clutter-scroll-controller.h"

#include "clutter/clutter-backend.h"
#include "clutter/clutter-context.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-sprite.h"
#include "clutter/clutter-stage.h"

enum
{
  SCROLL,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };

enum
{
  PROP_0,
  PROP_FLAGS,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

struct _ClutterScrollController
{
  ClutterAction parent_instance;
  ClutterSprite *current_sprite;
  double accum_x;
  double accum_y;
  ClutterScrollSource current_scroll_source;
  ClutterScrollControllerFlags flags;
};

G_DEFINE_TYPE (ClutterScrollController,
               clutter_scroll_controller,
               CLUTTER_TYPE_ACTION)

static void
clutter_scroll_controller_reset_accumulated (ClutterScrollController *scroll_controller)
{
  scroll_controller->current_sprite = NULL;
  scroll_controller->current_scroll_source = CLUTTER_SCROLL_SOURCE_UNKNOWN;
  scroll_controller->accum_x = 0;
  scroll_controller->accum_y = 0;
}

static void
clutter_scroll_controller_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  ClutterScrollController *scroll_controller = CLUTTER_SCROLL_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_FLAGS:
      scroll_controller->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_scroll_controller_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  ClutterScrollController *scroll_controller = CLUTTER_SCROLL_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_FLAGS:
      g_value_set_flags (value, scroll_controller->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
clutter_scroll_controller_handle_event (ClutterAction      *action,
                                        const ClutterEvent *event)
{
  ClutterScrollController *scroll_controller = CLUTTER_SCROLL_CONTROLLER (action);
  ClutterEventType evtype = clutter_event_type (event);
  ClutterActor *actor =
    clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (action));
  ClutterStage *stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
  ClutterContext *context = clutter_actor_get_context (actor);
  ClutterBackend *backend = clutter_context_get_backend (context);

  if (evtype == CLUTTER_SCROLL &&
      !(clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_POINTER_EMULATED))
    {
      ClutterSprite *sprite =
        clutter_backend_get_sprite (backend, stage, event);
      ClutterScrollDirection scroll_dir =
        clutter_event_get_scroll_direction (event);
      ClutterScrollSource scroll_source = clutter_event_get_scroll_source (event);
      double dx, dy, x, y;

      if (sprite != scroll_controller->current_sprite ||
          scroll_source != scroll_controller->current_scroll_source)
        clutter_scroll_controller_reset_accumulated (scroll_controller);

      scroll_controller->current_sprite = sprite;

      switch (scroll_dir)
        {
        case CLUTTER_SCROLL_UP:
          scroll_controller->accum_y -= 1.0;
          break;
        case CLUTTER_SCROLL_DOWN:
          scroll_controller->accum_y += 1.0;
          break;
        case CLUTTER_SCROLL_LEFT:
          scroll_controller->accum_x -= 1.0;
          break;
        case CLUTTER_SCROLL_RIGHT:
          scroll_controller->accum_x += 1.0;
          break;
        case CLUTTER_SCROLL_SMOOTH:
          clutter_event_get_scroll_delta (event, &dx, &dy);
          scroll_controller->accum_x += dx;
          scroll_controller->accum_y += dy;
          break;
        }

      if (!(scroll_controller->flags & CLUTTER_SCROLL_CONTROLLER_FLAG_SCROLL_HORIZONTAL))
        scroll_controller->accum_x = 0;
      if (!(scroll_controller->flags & CLUTTER_SCROLL_CONTROLLER_FLAG_SCROLL_VERTICAL))
        scroll_controller->accum_y = 0;

      if (!!(scroll_controller->flags & CLUTTER_SCROLL_CONTROLLER_FLAG_PHYSICAL_DIRECTION) &&
          !!(clutter_event_get_scroll_flags (event) & CLUTTER_SCROLL_INVERTED))
        {
          scroll_controller->accum_x *= -1;
          scroll_controller->accum_y *= -1;
        }

      if (!!(scroll_controller->flags & CLUTTER_SCROLL_CONTROLLER_FLAG_DISCRETE))
        {
          x = (scroll_controller->accum_x > 0) ?
            floor (scroll_controller->accum_x) :
            ceil (scroll_controller->accum_x);
          y = (scroll_controller->accum_y > 0) ?
            floor (scroll_controller->accum_y) :
            ceil (scroll_controller->accum_y);

          scroll_controller->accum_x -= x;
          scroll_controller->accum_y -= y;
        }
      else
        {
          x = scroll_controller->accum_x;
          y = scroll_controller->accum_y;
          scroll_controller->accum_x = scroll_controller->accum_y = 0;
        }

      if (x != 0 || y != 0)
        {
          g_signal_emit (scroll_controller, signals[SCROLL], 0,
                         sprite, scroll_source, x, y);
          return CLUTTER_EVENT_STOP;
        }
    }
  else if (evtype == CLUTTER_LEAVE)
    {
      ClutterSprite *sprite =
        clutter_backend_get_sprite (backend, stage, event);

      if (sprite == scroll_controller->current_sprite)
        clutter_scroll_controller_reset_accumulated (scroll_controller);
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
clutter_scroll_controller_sequence_cancelled (ClutterAction *action,
                                              ClutterSprite *sprite)
{
  ClutterScrollController *scroll_controller = CLUTTER_SCROLL_CONTROLLER (action);

  if (sprite == scroll_controller->current_sprite)
    clutter_scroll_controller_reset_accumulated (scroll_controller);
}

static void
clutter_scroll_controller_class_init (ClutterScrollControllerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  object_class->set_property = clutter_scroll_controller_set_property;
  object_class->get_property = clutter_scroll_controller_get_property;

  action_class->handle_event = clutter_scroll_controller_handle_event;
  action_class->sequence_cancelled =
    clutter_scroll_controller_sequence_cancelled;

  /**
   * ClutterScrollController:flags:
   *
   * Flags specifying scroll controller behavior
   */
  props[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        CLUTTER_TYPE_SCROLL_CONTROLLER_FLAGS,
                        CLUTTER_SCROLL_CONTROLLER_FLAG_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  /**
   * ClutterScrollController::scroll:
   * @scroll_controller: the scroll controller
   * @sprite: sprite triggering scroll events
   * @source: source of the scroll events
   * @dx: delta in the X axis of the scroll motion
   * @dy: delta in the Y axis of the scroll motion
   *
   * Emitted when there is an input scroll event to handle
   */
  signals[SCROLL] =
    g_signal_new ("scroll",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 4,
                  CLUTTER_TYPE_SPRITE,
                  CLUTTER_TYPE_SCROLL_SOURCE,
                  G_TYPE_DOUBLE,
                  G_TYPE_DOUBLE);
}

static void
clutter_scroll_controller_init (ClutterScrollController *scroll_controller)
{
  clutter_scroll_controller_reset_accumulated (scroll_controller);
}

/**
 * clutter_scroll_controller_new:
 * @flags: flags affecting controller behavior
 *
 * Returns a newly created scroll controller. This controller may
 * be used to handle discrete or continuous scroll on either axis.
 *
 * Returns: a newly created scroll controller
 **/
ClutterAction *
clutter_scroll_controller_new (ClutterScrollControllerFlags flags)
{
  return g_object_new (CLUTTER_TYPE_SCROLL_CONTROLLER,
                       "flags", flags,
                       NULL);
}
