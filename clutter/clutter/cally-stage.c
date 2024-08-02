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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * CallyStage:
 *
 * Implementation of the ATK interfaces for a #ClutterStage
 *
 * #CallyStage implements the required ATK interfaces for [class@Clutter.Stage]
 *
 * Some implementation details: at this moment #CallyStage is used as
 * the most similar Window object in this toolkit (ie: emitting window
 * related signals), although the real purpose of [class@Clutter.Stage] is
 * being a canvas. Anyway, this is required for applications using
 * just clutter, or directly [class@Clutter.Stage]
 */
#include "config.h"

#include "clutter/cally-stage.h"
#include "clutter/cally-actor-private.h"
#include "clutter/clutter-stage.h"

/* AtkObject.h */
static void                  cally_stage_real_initialize (AtkObject *obj,
                                                          gpointer   data);
static AtkStateSet*          cally_stage_ref_state_set   (AtkObject *obj);

/* AtkWindow */
static void                  cally_stage_window_interface_init (AtkWindowIface *iface);


G_DEFINE_TYPE_WITH_CODE (CallyStage,
                         cally_stage,
                         CALLY_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_WINDOW,
                                                cally_stage_window_interface_init));

static void
cally_stage_class_init (CallyStageClass *klass)
{
  AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

  /* AtkObject */
  class->initialize = cally_stage_real_initialize;
  class->ref_state_set = cally_stage_ref_state_set;
}

static void
cally_stage_init (CallyStage *cally_stage)
{
}

static void
cally_stage_real_initialize (AtkObject *obj,
                             gpointer  data)
{
  g_return_if_fail (CALLY_IS_STAGE (obj));

  ATK_OBJECT_CLASS (cally_stage_parent_class)->initialize (obj, data);

  atk_object_set_role (obj, ATK_ROLE_WINDOW);
}

static AtkStateSet*
cally_stage_ref_state_set   (AtkObject *obj)
{
  CallyStage   *cally_stage = NULL;
  AtkStateSet  *state_set   = NULL;
  ClutterStage *stage       = NULL;

  g_return_val_if_fail (CALLY_IS_STAGE (obj), NULL);
  cally_stage = CALLY_STAGE (obj);

  state_set = ATK_OBJECT_CLASS (cally_stage_parent_class)->ref_state_set (obj);
  stage = CLUTTER_STAGE (CALLY_GET_CLUTTER_ACTOR (cally_stage));

  if (stage == NULL)
    return state_set;

  if (clutter_stage_is_active (stage))
    atk_state_set_add_state (state_set, ATK_STATE_ACTIVE);

  return state_set;
}

/* AtkWindow */
static void
cally_stage_window_interface_init (AtkWindowIface *iface)
{
  /* At this moment AtkWindow is just about signals */
}
