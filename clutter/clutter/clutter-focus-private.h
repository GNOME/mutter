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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-focus.h"

ClutterStage * clutter_focus_get_stage (ClutterFocus *focus);

gboolean clutter_focus_set_current_actor (ClutterFocus       *focus,
                                          ClutterActor       *actor,
                                          ClutterInputDevice *source_device,
                                          uint32_t            time_ms);

CLUTTER_EXPORT
ClutterActor * clutter_focus_get_current_actor (ClutterFocus *focus);

void clutter_focus_propagate_event (ClutterFocus       *focus,
                                    const ClutterEvent *event);

void clutter_focus_update_from_event (ClutterFocus       *focus,
                                      const ClutterEvent *event);

void clutter_focus_notify_grab (ClutterFocus *focus,
                                ClutterGrab  *grab,
                                ClutterActor *grab_actor,
                                ClutterActor *old_grab_actor);
