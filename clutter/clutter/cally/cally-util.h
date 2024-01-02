/* CALLY - The Clutter Accessibility Implementation Library
 *
 * Copyright (C) 2008 Igalia, S.L.
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
#include <atk/atk.h>

G_BEGIN_DECLS

#define CALLY_TYPE_UTIL            (cally_util_get_type ())

typedef struct _CallyUtil        CallyUtil;
typedef struct _CallyUtilClass   CallyUtilClass;

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (CallyUtil,
                          cally_util,
                          CALLY,
                          UTIL,
                          AtkUtil)

struct _CallyUtilClass
{
  /*< private >*/
  AtkUtilClass parent_class;
};

void _cally_util_override_atk_util (void);

gboolean cally_snoop_key_event (ClutterStage    *stage,
                                ClutterKeyEvent *key);

G_END_DECLS
