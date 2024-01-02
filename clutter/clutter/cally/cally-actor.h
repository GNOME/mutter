/* CALLY - The Clutter Accessibility Implementation Library
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * Some parts are based on GailWidget from GAIL
 * GAIL - The GNOME Accessibility Implementation Library
 * Copyright 2001, 2002, 2003 Sun Microsystems Inc.
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

#pragma once

#if !defined(__CALLY_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <cally/cally.h> can be included directly."
#endif

#include <atk/atk.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define CALLY_TYPE_ACTOR            (cally_actor_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (CallyActor,
                          cally_actor,
                          CALLY,
                          ACTOR,
                          AtkGObjectAccessible)

typedef struct _CallyActor           CallyActor;
typedef struct _CallyActorClass      CallyActorClass;
typedef struct _CallyActorPrivate    CallyActorPrivate;

/**
 * CallyActionFunc:
 * @cally_actor: a #CallyActor
 *
 * Action function, to be used on #AtkAction implementations as a individual
 * action
 */
typedef void (* CallyActionFunc) (CallyActor *cally_actor);

/**
 * CallyActionCallback:
 * @cally_actor: a #CallyActor
 * @user_data: user data passed to the function
 *
 * Action function, to be used on #AtkAction implementations as
 * an individual action.
 *
 * Unlike #CallyActionFunc, this function uses the @user_data
 * argument passed to [method@Actor.add_action_full].
 */
typedef void (* CallyActionCallback) (CallyActor *cally_actor,
                                      gpointer    user_data);

/**
 * CallyActorClass:
 * @notify_clutter: Signal handler for notify signal on Clutter actor
 * @add_actor: Signal handler for child-added signal on Clutter actor
 * @remove_actor: Signal handler for child-removed signal on Clutter actor
 */
struct _CallyActorClass
{
  /*< private >*/
  AtkGObjectAccessibleClass parent_class;

  /*< public >*/
  void     (*notify_clutter) (GObject    *object,
                              GParamSpec *pspec);

  gint     (*add_actor)      (ClutterActor *container,
                              ClutterActor *actor,
                              gpointer      data);

  gint     (*remove_actor)   (ClutterActor *container,
                              ClutterActor *actor,
                              gpointer      data);
};

CLUTTER_EXPORT
AtkObject* cally_actor_new                   (ClutterActor        *actor);

CLUTTER_EXPORT
guint      cally_actor_add_action            (CallyActor          *cally_actor,
                                              const gchar         *action_name,
                                              const gchar         *action_description,
                                              const gchar         *action_keybinding,
                                              CallyActionFunc      action_func);
CLUTTER_EXPORT
guint      cally_actor_add_action_full       (CallyActor          *cally_actor,
                                              const gchar         *action_name,
                                              const gchar         *action_description,
                                              const gchar         *action_keybinding,
                                              CallyActionCallback  callback,
                                              gpointer             user_data,
                                              GDestroyNotify       notify);

CLUTTER_EXPORT
gboolean   cally_actor_remove_action         (CallyActor          *cally_actor,
                                              gint                 action_id);

CLUTTER_EXPORT
gboolean   cally_actor_remove_action_by_name (CallyActor          *cally_actor,
                                              const gchar         *action_name);

G_END_DECLS
