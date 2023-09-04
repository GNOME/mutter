/*
 * Copyright (C) 2007,2008,2009,2010,2011  Intel Corporation.
 * Copyright (C) 2020 Red Hat Inc
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

#include <glib.h>

#include "clutter/clutter-macros.h"
#include "mtk/mtk.h"

typedef struct _ClutterDamageHistory ClutterDamageHistory;

CLUTTER_EXPORT
ClutterDamageHistory * clutter_damage_history_new (void);

CLUTTER_EXPORT
void clutter_damage_history_free (ClutterDamageHistory *history);

CLUTTER_EXPORT
gboolean clutter_damage_history_is_age_valid (ClutterDamageHistory *history,
                                              int                   age);

CLUTTER_EXPORT
void clutter_damage_history_record (ClutterDamageHistory *history,
                                    const MtkRegion      *damage);

CLUTTER_EXPORT
void clutter_damage_history_step (ClutterDamageHistory *history);

CLUTTER_EXPORT
const MtkRegion * clutter_damage_history_lookup (ClutterDamageHistory *history,
                                                 int                   age);
