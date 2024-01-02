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

#include <atk/atk.h>

#include "clutter/clutter.h"

G_BEGIN_DECLS

#define CALLY_TYPE_ROOT            (cally_root_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (CallyRoot,
                          cally_root,
                          CALLY,
                          ROOT,
                          AtkGObjectAccessible)

typedef struct _CallyRoot CallyRoot;
typedef struct _CallyRootClass CallyRootClass;

struct _CallyRootClass
{
  /*< private >*/
  AtkGObjectAccessibleClass parent_class;
};

CLUTTER_EXPORT
AtkObject *cally_root_new      (void);

G_END_DECLS
