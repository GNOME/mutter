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
#include "config.h"

#include "cally/cally.h"

#include "cally/cally-actor.h"
#include "cally/cally-stage.h"
#include "cally/cally-text.h"
#include "cally/cally-clone.h"

#include "cally/cally-factory.h"
#include "cally/cally-util.h"

#include "clutter/clutter.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"

/* factories initialization*/
CALLY_ACCESSIBLE_FACTORY (CALLY_TYPE_ACTOR, cally_actor, cally_actor_new)
CALLY_ACCESSIBLE_FACTORY (CALLY_TYPE_STAGE, cally_stage, cally_stage_new)
CALLY_ACCESSIBLE_FACTORY (CALLY_TYPE_TEXT, cally_text, cally_text_new)
CALLY_ACCESSIBLE_FACTORY (CALLY_TYPE_CLONE, cally_clone, cally_clone_new)

/**
 * cally_accessibility_init:
 *
 * Initializes the accessibility support.
 *
 * Return value: %TRUE if accessibility support has been correctly
 * initialized.
 */
gboolean
cally_accessibility_init (void)
{
  /* setting the factories */
  CALLY_ACTOR_SET_FACTORY (CLUTTER_TYPE_ACTOR, cally_actor);
  CALLY_ACTOR_SET_FACTORY (CLUTTER_TYPE_STAGE, cally_stage);
  CALLY_ACTOR_SET_FACTORY (CLUTTER_TYPE_TEXT, cally_text);
  CALLY_ACTOR_SET_FACTORY (CLUTTER_TYPE_CLONE, cally_clone);

  /* Initialize the CallyUtility class */
  _cally_util_override_atk_util ();

  CLUTTER_NOTE (MISC, "Clutter Accessibility initialized");

  return TRUE;
}

/**
 * cally_get_cally_initialized:
 *
 * Returns if the accessibility support using cally is enabled.
 *
 * Return value: %TRUE if accessibility support has been correctly
 * initialized.
 */
gboolean cally_get_cally_initialized (void)
{
  return !g_strcmp0 (atk_get_toolkit_name (), "clutter");
}
