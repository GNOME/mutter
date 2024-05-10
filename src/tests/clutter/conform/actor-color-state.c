/*
 * Copyright (C) 2022  Intel Corporation.
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
 *
 * Author:
 *   Naveen Kumar <naveen1.kumar@intel.com>
 */

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

/* creating an actor will result in it being assigned a color state with the
 * color space sRGB */
static void
actor_color_state_default (void)
{
  ClutterActor *actor;
  ClutterColorState *color_state;
  ClutterColorspace colorspace;

  actor = clutter_actor_new ();

  color_state = clutter_actor_get_color_state (actor);
  colorspace = clutter_color_state_get_colorspace (color_state);

  g_assert_cmpuint (colorspace, ==, CLUTTER_COLORSPACE_DEFAULT);

  clutter_actor_destroy (actor);
}

/* creating an actor with a color state passed will result in that color
 * state being returned */
static void
actor_color_state_passed (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterActor *actor;
  ClutterColorState *color_state;
  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;

  color_state = clutter_color_state_new (context,
                                         CLUTTER_COLORSPACE_BT2020,
                                         CLUTTER_TRANSFER_FUNCTION_PQ);

  if (!color_state)
    g_critical ("Failed to create color state with provided colorspace.");

  actor = g_object_new (CLUTTER_TYPE_ACTOR,
                        "width", 50.0,
                        "height", 50.0,
                        "x", 0.0, "y", 0.0,
                        "color_state", color_state,
                        NULL);

  if (!actor)
    g_critical ("Failed to create actor with provided color state.");

  color_state = clutter_actor_get_color_state (actor);
  colorspace = clutter_color_state_get_colorspace (color_state);
  transfer_function = clutter_color_state_get_transfer_function (color_state);

  g_assert_cmpuint (colorspace, ==, CLUTTER_COLORSPACE_BT2020);
  g_assert_cmpuint (transfer_function, ==, CLUTTER_TRANSFER_FUNCTION_PQ);

  clutter_actor_destroy (actor);
}

/* changing an actor's color state makes that happen */
static void
actor_change_color_state (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterActor *actor;
  ClutterColorState *color_state;
  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;

  actor = clutter_actor_new ();

  color_state = clutter_color_state_new (context,
                                         CLUTTER_COLORSPACE_BT2020,
                                         CLUTTER_TRANSFER_FUNCTION_PQ);

  if (!color_state)
    g_critical ("Failed to create color state with provided colorspace.");

  clutter_actor_set_color_state (actor, color_state);

  color_state = clutter_actor_get_color_state (actor);
  colorspace = clutter_color_state_get_colorspace (color_state);
  transfer_function = clutter_color_state_get_transfer_function (color_state);

  g_assert_cmpuint (colorspace, ==, CLUTTER_COLORSPACE_BT2020);
  g_assert_cmpuint (transfer_function, ==, CLUTTER_TRANSFER_FUNCTION_PQ);

  clutter_actor_destroy (actor);
}

static void
actor_unset_color_state (void)
{
  ClutterActor *actor;
  ClutterColorState *color_state;
  ClutterColorspace colorspace;
  ClutterTransferFunction transfer_function;

  actor = clutter_actor_new ();

  clutter_actor_unset_color_state (actor);

  color_state = clutter_actor_get_color_state (actor);
  colorspace = clutter_color_state_get_colorspace (color_state);
  transfer_function = clutter_color_state_get_transfer_function (color_state);

  g_assert_cmpuint (colorspace, ==, CLUTTER_COLORSPACE_DEFAULT);
  g_assert_cmpuint (transfer_function, ==, CLUTTER_TRANSFER_FUNCTION_DEFAULT);

  clutter_actor_destroy (actor);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/color-state-default", actor_color_state_default)
  CLUTTER_TEST_UNIT ("/actor/color-state-passed", actor_color_state_passed)
  CLUTTER_TEST_UNIT ("/actor/change-color-state", actor_change_color_state)
  CLUTTER_TEST_UNIT ("/actor/unset-color-state", actor_unset_color_state)
)
