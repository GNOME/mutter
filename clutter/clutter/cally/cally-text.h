/* CALLY - The Clutter Accessibility Implementation Library
 *
 * Copyright (C) 2009 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
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

#include "clutter/clutter.h"
#include "cally/cally-actor.h"

G_BEGIN_DECLS

#define CALLY_TYPE_TEXT                 (cally_text_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (CallyText,
                          cally_text,
                          CALLY,
                          TEXT,
                          CallyActor)

typedef struct _CallyText CallyText;
typedef struct _CallyTextClass CallyTextClass;

struct _CallyTextClass
{
  /*< private >*/
  CallyActorClass parent_class;
};

CLUTTER_EXPORT
AtkObject* cally_text_new      (ClutterActor *actor);

G_END_DECLS
