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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-actor-meta.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_ACTION (clutter_action_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterAction, clutter_action,
                          CLUTTER, ACTION, ClutterActorMeta);

/**
 * ClutterActionClass:
 *
 * The ClutterActionClass structure contains only private data
 */
struct _ClutterActionClass
{
  /*< private >*/
  ClutterActorMetaClass parent_class;

  gboolean (* handle_event) (ClutterAction      *action,
                             const ClutterEvent *event);

  void (* sequence_cancelled) (ClutterAction        *action,
                               ClutterInputDevice   *device,
                               ClutterEventSequence *sequence);

  gboolean (* register_sequence) (ClutterAction      *self,
                                  const ClutterEvent *event);

  int (* setup_sequence_relationship) (ClutterAction        *action_1,
                                       ClutterAction        *action_2,
                                       ClutterInputDevice   *device,
                                       ClutterEventSequence *sequence);
};

/* ClutterActor API */
CLUTTER_EXPORT
void           clutter_actor_add_action            (ClutterActor  *self,
                                                    ClutterAction *action);
CLUTTER_EXPORT
void           clutter_actor_add_action_with_name  (ClutterActor  *self,
                                                    const gchar   *name,
                                                    ClutterAction *action);
CLUTTER_EXPORT
void           clutter_actor_add_action_full       (ClutterActor      *self,
                                                    const char        *name,
                                                    ClutterEventPhase  phase,
                                                    ClutterAction     *action);
CLUTTER_EXPORT
void           clutter_actor_remove_action         (ClutterActor  *self,
                                                    ClutterAction *action);
CLUTTER_EXPORT
void           clutter_actor_remove_action_by_name (ClutterActor  *self,
                                                    const gchar   *name);
CLUTTER_EXPORT
ClutterAction *clutter_actor_get_action            (ClutterActor  *self,
                                                    const gchar   *name);
CLUTTER_EXPORT
GList *        clutter_actor_get_actions           (ClutterActor  *self);
CLUTTER_EXPORT
void           clutter_actor_clear_actions         (ClutterActor  *self);

CLUTTER_EXPORT
gboolean       clutter_actor_has_actions           (ClutterActor  *self);

ClutterEventPhase clutter_action_get_phase (ClutterAction *action);

G_END_DECLS
