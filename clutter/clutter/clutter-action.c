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
 * ClutterAction:
 * 
 * Abstract class for event-related logic
 *
 * #ClutterAction is an abstract base class for event-related actions that
 * modify the user interaction of a [class@Actor], just like
 * [class@Constraint] is an abstract class for modifiers of an actor's
 * position or size.
 *
 * Implementations of #ClutterAction are associated to an actor and can
 * provide behavioral changes when dealing with user input - for instance
 * drag and drop capabilities, or scrolling, or panning - by using the
 * various event-related signals provided by [class@Actor] itself.
 */

#include "config.h"

#include "clutter/clutter-action.h"
#include "clutter/clutter-action-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"

typedef struct _ClutterActionPrivate ClutterActionPrivate;

struct _ClutterActionPrivate
{
  ClutterEventPhase phase;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterAction, clutter_action,
                                     CLUTTER_TYPE_ACTOR_META)

static gboolean
clutter_action_handle_event_default (ClutterAction      *action,
                                     const ClutterEvent *event)
{
  return FALSE;
}

static void
clutter_action_class_init (ClutterActionClass *klass)
{
  klass->handle_event = clutter_action_handle_event_default;
}

static void
clutter_action_init (ClutterAction *self)
{
}

void
clutter_action_set_phase (ClutterAction     *action,
                          ClutterEventPhase  phase)
{
  ClutterActionPrivate *priv;

  priv = clutter_action_get_instance_private (action);
  priv->phase = phase;
}

ClutterEventPhase
clutter_action_get_phase (ClutterAction *action)
{
  ClutterActionPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_ACTION (action), CLUTTER_PHASE_CAPTURE);

  priv = clutter_action_get_instance_private (action);

  return priv->phase;
}

gboolean
clutter_action_handle_event (ClutterAction      *action,
                             const ClutterEvent *event)
{
  return CLUTTER_ACTION_GET_CLASS (action)->handle_event (action, event);
}

void
clutter_action_sequence_cancelled (ClutterAction        *action,
                                   ClutterInputDevice   *device,
                                   ClutterEventSequence *sequence)
{
  ClutterActionClass *action_class = CLUTTER_ACTION_GET_CLASS (action);

  if (action_class->sequence_cancelled)
    action_class->sequence_cancelled (action, device, sequence);
}
