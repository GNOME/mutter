/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat Inc.
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

#include "clutter/clutter-focus-private.h"

#include "clutter/clutter-grab.h"
#include "clutter/clutter-stage.h"

enum
{
  PROP_0,
  PROP_STAGE,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

typedef struct _ClutterFocusPrivate
{
  ClutterStage *stage;
} ClutterFocusPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (ClutterFocus, clutter_focus, G_TYPE_OBJECT)

static void
clutter_focus_init (ClutterFocus *focus)
{
}

static void
clutter_focus_finalize (GObject *object)
{
  ClutterFocus *focus = CLUTTER_FOCUS (object);
  ClutterFocusPrivate *priv = clutter_focus_get_instance_private (focus);
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_GET_CLASS (focus);

  focus_class->set_current_actor (focus, NULL, NULL, CLUTTER_CURRENT_TIME);
  g_clear_object (&priv->stage);

  G_OBJECT_CLASS (clutter_focus_parent_class)->finalize (object);
}

static void
clutter_focus_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  ClutterFocus *focus = CLUTTER_FOCUS (object);
  ClutterFocusPrivate *priv = clutter_focus_get_instance_private (focus);

  switch (prop_id)
    {
    case PROP_STAGE:
      priv->stage = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_focus_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  ClutterFocus *focus = CLUTTER_FOCUS (object);
  ClutterFocusPrivate *priv = clutter_focus_get_instance_private (focus);

  switch (prop_id)
    {
    case PROP_STAGE:
      g_value_set_object (value, priv->stage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_focus_class_init (ClutterFocusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_focus_finalize;
  object_class->set_property = clutter_focus_set_property;
  object_class->get_property = clutter_focus_get_property;

  props[PROP_STAGE] =
    g_param_spec_object ("stage", NULL, NULL,
                         CLUTTER_TYPE_STAGE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

ClutterStage *
clutter_focus_get_stage (ClutterFocus *focus)
{
  ClutterFocusPrivate *priv = clutter_focus_get_instance_private (focus);

  return priv->stage;
}

gboolean
clutter_focus_set_current_actor (ClutterFocus       *focus,
                                 ClutterActor       *actor,
                                 ClutterInputDevice *source_device,
                                 uint32_t            time_ms)
{
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_GET_CLASS (focus);

  return focus_class->set_current_actor (focus, actor, source_device, time_ms);
}

ClutterActor *
clutter_focus_get_current_actor (ClutterFocus *focus)
{
  ClutterFocusClass *focus_class = CLUTTER_FOCUS_GET_CLASS (focus);

  return focus_class->get_current_actor (focus);
}

void
clutter_focus_notify_grab (ClutterFocus *focus,
                           ClutterGrab  *grab,
                           ClutterActor *grab_actor,
                           ClutterActor *old_grab_actor)
{
  ClutterFocusClass *focus_class;

  focus_class = CLUTTER_FOCUS_GET_CLASS (focus);
  focus_class->notify_grab (focus, grab, grab_actor, old_grab_actor);
}

void
clutter_focus_propagate_event (ClutterFocus       *focus,
                               const ClutterEvent *event)
{
  ClutterFocusClass *focus_class;

  focus_class = CLUTTER_FOCUS_GET_CLASS (focus);
  focus_class->propagate_event (focus, event);
}

void
clutter_focus_update_from_event (ClutterFocus       *focus,
                                 const ClutterEvent *event)
{
  ClutterFocusClass *focus_class;

  focus_class = CLUTTER_FOCUS_GET_CLASS (focus);
  if (focus_class->update_from_event)
    focus_class->update_from_event (focus, event);
}
