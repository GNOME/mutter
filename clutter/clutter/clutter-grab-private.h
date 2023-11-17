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

#pragma once

#include "clutter-grab.h"
#include "clutter-stage.h"

G_BEGIN_DECLS

struct _ClutterGrab
{
  GObject parent_instance;
  ClutterStage *stage;

  ClutterActor *actor;
  gboolean owns_actor;

  ClutterGrab *prev;
  ClutterGrab *next;
};

ClutterGrab * clutter_grab_new (ClutterStage *stage,
                                ClutterActor *actor,
                                gboolean      owns_actor);

void clutter_grab_notify (ClutterGrab *grab);

G_END_DECLS
