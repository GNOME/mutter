/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * Based on GailUtil from GAIL
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "clutter/clutter-accessibility-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-stage-manager-accessible-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-private.h"

static AtkObject*root = NULL;

/* ------------------------------ ATK UTIL METHODS -------------------------- */

static AtkObject*
clutter_accessibility_get_root (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterStageManager *stage_manager =
    clutter_context_get_stage_manager (context);

  if (!root)
    root = clutter_stage_manager_accessible_new (stage_manager);

  return root;
}

static const gchar *
clutter_accessibility_get_toolkit_name (void)
{
  return "clutter";
}

static const gchar *
clutter_accessibility_get_toolkit_version (void)
{
  return VERSION;
}

void
_clutter_accessibility_override_atk_util (void)
{
  AtkUtilClass *atk_class = ATK_UTIL_CLASS (g_type_class_ref (ATK_TYPE_UTIL));

  if (atk_class->get_root)
    return;

  atk_class->get_root = clutter_accessibility_get_root;
  atk_class->get_toolkit_name = clutter_accessibility_get_toolkit_name;
  atk_class->get_toolkit_version = clutter_accessibility_get_toolkit_version;
}
