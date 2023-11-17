/*
 * Clutter.
 *
 * Copyright (C) 2023 Red Hat Inc.
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

#include "clutter-grab-private.h"

G_DEFINE_FINAL_TYPE (ClutterGrab, clutter_grab, G_TYPE_OBJECT)

static void
clutter_grab_dispose (GObject *object)
{
  clutter_grab_dismiss (CLUTTER_GRAB (object));
  G_OBJECT_CLASS (clutter_grab_parent_class)->dispose (object);
}

static void
clutter_grab_finalize (GObject *object)
{
  ClutterGrab *grab = CLUTTER_GRAB (object);

  if (grab->owns_actor)
    g_clear_pointer (&grab->actor, clutter_actor_destroy);

  G_OBJECT_CLASS (clutter_grab_parent_class)->finalize (object);
}

static void
clutter_grab_class_init (ClutterGrabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = clutter_grab_dispose;
  object_class->finalize = clutter_grab_finalize;
}

static void
clutter_grab_init (ClutterGrab *grab)
{
}

ClutterGrab *
clutter_grab_new (ClutterStage *stage,
                  ClutterActor *actor,
                  gboolean      owns_actor)
{
  ClutterGrab *grab;

  grab = g_object_new (CLUTTER_TYPE_GRAB, NULL);
  grab->stage = stage;

  grab->actor = actor;
  if (owns_actor)
    grab->owns_actor = TRUE;

  return grab;
}
