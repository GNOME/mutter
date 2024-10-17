/* Clutter.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * ClutterStageAccessible:
 *
 * Implementation of the ATK interfaces for a #ClutterStage
 *
 * #ClutterStageAccessible implements the required ATK interfaces for [class@Clutter.Stage]
 *
 * Some implementation details: at this moment #ClutterStageAccessible is used as
 * the most similar Window object in this toolkit (ie: emitting window
 * related signals), although the real purpose of [class@Clutter.Stage] is
 * being a canvas. Anyway, this is required for applications using
 * just clutter, or directly [class@Clutter.Stage]
 */
#include "config.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-stage-accessible-private.h"
#include "clutter/clutter-stage.h"

/* AtkWindow */
static void clutter_stage_accessible_window_interface_init (AtkWindowIface *iface);

struct _ClutterStageAccessible
{
  ClutterActorAccessible parent;
};
G_DEFINE_FINAL_TYPE_WITH_CODE (ClutterStageAccessible,
                               clutter_stage_accessible,
                               CLUTTER_TYPE_ACTOR_ACCESSIBLE,
                               G_IMPLEMENT_INTERFACE (ATK_TYPE_WINDOW,
                                                      clutter_stage_accessible_window_interface_init));

static void
clutter_stage_accessible_init (ClutterStageAccessible *stage_accessible)
{
}

/* AtkWindow */
static void
clutter_stage_accessible_window_interface_init (AtkWindowIface *iface)
{
  /* At this moment AtkWindow is just about signals */
}

static void
clutter_stage_accessible_class_init (ClutterStageAccessibleClass *klass)
{
}
