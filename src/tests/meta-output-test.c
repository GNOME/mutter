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

enum
{
  PROP_0,

  PROP_BACKLIGHT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

G_DEFINE_TYPE (MetaOutputTest, meta_output_test, META_TYPE_OUTPUT_NATIVE)

G_DEFINE_TYPE (MetaBacklightTest, meta_backlight_test, META_TYPE_BACKLIGHT)

static GBytes *
meta_output_test_read_edid (MetaOutputNative *output_native)
{
  return NULL;
}

static MetaBacklight *
meta_output_test_create_backlight (MetaOutput  *output,
                                   GError     **error)
{
  MetaOutputTest *output_test = META_OUTPUT_TEST (output);

  if (output_test->backlight)
    return g_object_ref (output_test->backlight);

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "no test backlight defined");
  return NULL;
}

static void
meta_output_test_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaOutputTest *output_test = META_OUTPUT_TEST (object);

  switch (prop_id)
    {
    case PROP_BACKLIGHT:
      g_set_object (&output_test->backlight, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_output_test_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaOutputTest *output_test = META_OUTPUT_TEST (object);

  switch (prop_id)
    {
    case PROP_BACKLIGHT:
      g_value_set_object (value, output_test->backlight);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_output_test_dispose (GObject *object)
{
  MetaOutputTest *output_test = META_OUTPUT_TEST (object);

  g_clear_object (&output_test->backlight);

  G_OBJECT_CLASS (meta_output_test_parent_class)->dispose (object);
}

static void
meta_output_test_class_init (MetaOutputTestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaOutputClass *output_class = META_OUTPUT_CLASS (klass);
  MetaOutputNativeClass *output_native_class = META_OUTPUT_NATIVE_CLASS (klass);

  object_class->set_property = meta_output_test_set_property;
  object_class->get_property = meta_output_test_get_property;
  object_class->dispose = meta_output_test_dispose;

  output_class->create_backlight = meta_output_test_create_backlight;
  output_native_class->read_edid = meta_output_test_read_edid;

  obj_props[PROP_BACKLIGHT] =
    g_param_spec_object ("backlight", NULL, NULL,
                         META_TYPE_BACKLIGHT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_output_test_init (MetaOutputTest *output_test)
{
}

void
meta_output_test_override_scale (MetaOutputTest *output_test,
                                 float           scale)
{
  output_test->override_scale = TRUE;
  output_test->scale = scale;
}

static void
meta_backlight_test_set_brightness (MetaBacklight       *backlight,
                                    int                  brightness_target,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  MetaBacklightTest *backlight_test = META_BACKLIGHT_TEST (backlight);
  g_autoptr (GTask) task = NULL;

  task = g_task_new (backlight_test, cancellable, callback, user_data);
  g_task_set_task_data (task, GINT_TO_POINTER (brightness_target), NULL);

  g_task_return_int (task, brightness_target);
}

static int
meta_backlight_test_set_brightness_finish (MetaBacklight  *backlight,
                                           GAsyncResult   *result,
                                           GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, backlight), -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_backlight_test_class_init (MetaBacklightTestClass *klass)
{
  MetaBacklightClass *backlight_class = META_BACKLIGHT_CLASS (klass);

  backlight_class->set_brightness = meta_backlight_test_set_brightness;
  backlight_class->set_brightness_finish = meta_backlight_test_set_brightness_finish;
}

static void
meta_backlight_test_init (MetaBacklightTest *backlight_test)
{
}
