/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Red Hat Inc.
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
 */

#include "clutter/clutter-build-config.h"

#include "clutter/clutter-input-only-action.h"

#include "clutter/clutter-action-private.h"
#include "clutter/clutter.h"

struct _ClutterInputOnlyAction
{
  ClutterAction parent;

  ClutterInputOnlyHandleEvent handle_event;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
};

G_DEFINE_TYPE (ClutterInputOnlyAction, clutter_input_only_action,
               CLUTTER_TYPE_ACTION)

static void
clutter_input_only_action_dispose (GObject *object)
{
  ClutterInputOnlyAction *input_only_action =
    CLUTTER_INPUT_ONLY_ACTION (object);

  if (input_only_action->user_data_destroy)
    {
      g_clear_pointer (&input_only_action->user_data,
                       input_only_action->user_data_destroy);
    }

  G_OBJECT_CLASS (clutter_input_only_action_parent_class)->dispose (object);
}

static gboolean
clutter_input_only_action_handle_event (ClutterAction      *action,
                                        const ClutterEvent *event)
{
  ClutterInputOnlyAction *input_only_action =
    CLUTTER_INPUT_ONLY_ACTION (action);

  return input_only_action->handle_event (event, input_only_action->user_data);
}

static void
clutter_input_only_action_class_init (ClutterInputOnlyActionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActionClass *action_class = CLUTTER_ACTION_CLASS (klass);

  object_class->dispose = clutter_input_only_action_dispose;

  action_class->handle_event = clutter_input_only_action_handle_event;
}

static void
clutter_input_only_action_init (ClutterInputOnlyAction *input_only_action)
{
}

ClutterInputOnlyAction *
clutter_input_only_action_new (ClutterInputOnlyHandleEvent handle_event,
                               gpointer                    user_data,
                               GDestroyNotify              user_data_destroy)
{
  ClutterInputOnlyAction *input_only_action;

  input_only_action = g_object_new (CLUTTER_TYPE_INPUT_ONLY_ACTION, NULL);
  input_only_action->handle_event = handle_event;
  input_only_action->user_data = user_data;
  input_only_action->user_data_destroy = user_data_destroy;
  clutter_action_set_phase (CLUTTER_ACTION (input_only_action),
                            CLUTTER_PHASE_CAPTURE);

  return input_only_action;
}
