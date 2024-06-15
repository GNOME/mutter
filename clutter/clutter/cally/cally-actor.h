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
 * CallyActorClass:
 * @notify_clutter: Signal handler for notify signal on Clutter actor
 */
struct _CallyActorClass
{
  /*< private >*/
  AtkGObjectAccessibleClass parent_class;

  /*< public >*/
  void     (*notify_clutter) (GObject    *object,
                              GParamSpec *pspec);
};

CLUTTER_EXPORT
AtkObject* cally_actor_new                   (ClutterActor        *actor);


G_END_DECLS
