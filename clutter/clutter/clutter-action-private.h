/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2021 Red Hat Inc.
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
 *   Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-action.h"

G_BEGIN_DECLS

void clutter_action_set_phase (ClutterAction     *action,
                               ClutterEventPhase  phase);

gboolean clutter_action_handle_event (ClutterAction      *action,
                                      const ClutterEvent *event);

void clutter_action_sequence_cancelled (ClutterAction        *action,
                                        ClutterInputDevice   *device,
                                        ClutterEventSequence *sequence);

gboolean clutter_action_register_sequence (ClutterAction      *self,
                                           const ClutterEvent *event);

int clutter_action_setup_sequence_relationship (ClutterAction        *action_1,
                                                ClutterAction        *action_2,
                                                ClutterInputDevice   *device,
                                                ClutterEventSequence *sequence);

G_END_DECLS
