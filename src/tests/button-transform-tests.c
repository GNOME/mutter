/*
 * Copyright (C) 2023 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "button-transform-tests.h"

#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"

static void
meta_test_clutter_to_evdev (void)
{
  struct {
    uint32_t clutter_button;
    uint32_t evdev_button;
  } test_cases[] = {
    { .clutter_button = CLUTTER_BUTTON_PRIMARY, .evdev_button = BTN_LEFT },
    { .clutter_button = CLUTTER_BUTTON_MIDDLE, .evdev_button = BTN_MIDDLE },
    { .clutter_button = CLUTTER_BUTTON_SECONDARY, .evdev_button = BTN_RIGHT },
  };
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (test_cases); i++)
    {
      uint32_t clutter_button;
      uint32_t expected_evdev_button;
      uint32_t evdev_button;

      clutter_button = test_cases[i].clutter_button;
      expected_evdev_button = test_cases[i].evdev_button;

      evdev_button = meta_clutter_button_to_evdev (clutter_button);
      g_assert_cmpuint (evdev_button, ==, expected_evdev_button);
    }
}

static void
meta_test_evdev_to_clutter (void)
{
  struct {
    uint32_t evdev_button;
    uint32_t clutter_button;
  } test_cases[] = {
    { .evdev_button = BTN_LEFT, .clutter_button = CLUTTER_BUTTON_PRIMARY },
    { .evdev_button = BTN_MIDDLE, .clutter_button = CLUTTER_BUTTON_MIDDLE },
    { .evdev_button = BTN_RIGHT, .clutter_button = CLUTTER_BUTTON_SECONDARY },
  };
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (test_cases); i++)
    {
      uint32_t evdev_button;
      uint32_t expected_clutter_button;
      uint32_t clutter_button;

      evdev_button = test_cases[i].evdev_button;
      expected_clutter_button = test_cases[i].clutter_button;

      clutter_button = meta_evdev_button_to_clutter (evdev_button);
      g_assert_cmpuint (clutter_button, ==, expected_clutter_button);
    }
}

static void
meta_test_evdev_to_clutter_to_evdev (void)
{
  uint32_t test_cases[] = {
    BTN_LEFT,
    BTN_MIDDLE,
    BTN_RIGHT,
    BTN_SIDE,
    BTN_BACK,
    BTN_FORWARD,
  };
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (test_cases); i++)
    {
      uint32_t evdev_button;
      uint32_t expected_evdev_button;
      uint32_t clutter_button;

      evdev_button = test_cases[i];
      expected_evdev_button = evdev_button;

      clutter_button = meta_evdev_button_to_clutter (evdev_button);
      evdev_button = meta_clutter_button_to_evdev (clutter_button);
      g_assert_cmpuint (evdev_button, ==, expected_evdev_button);
    }
}

void
init_button_transform_tests (void)
{
  g_test_add_func ("/backends/button-transform/clutter-to-evdev",
                   meta_test_clutter_to_evdev);
  g_test_add_func ("/backends/button-transform/evdev-clutter",
                   meta_test_evdev_to_clutter);
  g_test_add_func ("/backends/button-transform/evdev-clutter-evdev",
                   meta_test_evdev_to_clutter_to_evdev);
}
