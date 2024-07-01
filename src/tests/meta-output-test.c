/*
 * Copyright (C) 2016-2024 Red Hat, Inc.
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
 */

#include "config.h"

#include "tests/meta-output-test.h"

G_DEFINE_TYPE (MetaOutputTest, meta_output_test, META_TYPE_OUTPUT_NATIVE)

static void
on_backlight_changed (MetaOutput *output)
{
  const MetaOutputInfo *info = meta_output_get_info (output);
  int value = meta_output_get_backlight (output);

  g_assert_cmpint (info->backlight_min, <=, value);
  g_assert_cmpint (info->backlight_max, >=, value);
}

static void
meta_output_test_constructed (GObject *object)
{
  MetaOutput *output = META_OUTPUT (object);
  const MetaOutputInfo *info = meta_output_get_info (output);

  if (info->backlight_min != info->backlight_max)
    {
      int backlight_range = info->backlight_max - info->backlight_min;

      meta_output_set_backlight (output,
                                 info->backlight_min + backlight_range / 2);
      g_signal_connect (output, "backlight-changed",
                        G_CALLBACK (on_backlight_changed), NULL);
    }

  G_OBJECT_CLASS (meta_output_test_parent_class)->constructed (object);
}

static GBytes *
meta_output_test_read_edid (MetaOutputNative *output_native)
{
  return NULL;
}

static void
meta_output_test_class_init (MetaOutputTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaOutputNativeClass *output_native_class = META_OUTPUT_NATIVE_CLASS (klass);

  object_class->constructed = meta_output_test_constructed;

  output_native_class->read_edid = meta_output_test_read_edid;
}

static void
meta_output_test_init (MetaOutputTest *output_test)
{
  output_test->scale = 1;
}
