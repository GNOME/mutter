/*
 * Copyright (C) 2025 Red Hat
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

#include "backends/meta-backlight-ref-white-private.h"

#include <glib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "backends/meta-monitor-private.h"

struct _MetaBacklightRefWhite
{
  MetaBacklight parent;

  MetaMonitor *monitor;
  float original_ref_white;
  guint change_ref_white_handle_id;
};

G_DEFINE_FINAL_TYPE (MetaBacklightRefWhite,
                     meta_backlight_ref_white,
                     META_TYPE_BACKLIGHT)

static void
set_factor (MetaBacklightRefWhite *backlight_ref_white,
            float                  factor)
{
  MetaBacklight *backlight = META_BACKLIGHT (backlight_ref_white);
  MetaBackend *backend = meta_backlight_get_backend (backlight);
  MetaColorManager *color_manager = meta_backend_get_color_manager (backend);
  MetaMonitor *monitor = backlight_ref_white->monitor;
  MetaColorDevice *color_device =
    meta_color_manager_get_color_device (color_manager, monitor);

  meta_color_device_set_reference_luminance_factor (color_device, factor);
}

static float
get_factor (MetaBacklightRefWhite *backlight_ref_white)
{
  MetaBacklight *backlight = META_BACKLIGHT (backlight_ref_white);
  MetaBackend *backend = meta_backlight_get_backend (backlight);
  MetaColorManager *color_manager = meta_backend_get_color_manager (backend);
  MetaMonitor *monitor = backlight_ref_white->monitor;
  MetaColorDevice *color_device =
    meta_color_manager_get_color_device (color_manager, monitor);

  return meta_color_device_get_reference_luminance_factor (color_device);
}

static gboolean
on_change_ref_white (gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  MetaBacklightRefWhite *backlight_ref_white = g_task_get_source_object (task);
  int brightness_target = GPOINTER_TO_INT (g_task_get_task_data (task));

  backlight_ref_white->change_ref_white_handle_id = 0;

  set_factor (backlight_ref_white, brightness_target / 100.f);

  g_task_return_int (task, brightness_target);

  return G_SOURCE_REMOVE;
}

static void
meta_backlight_ref_white_set_brightness (MetaBacklight       *backlight,
                                         int                  brightness_target,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  MetaBacklightRefWhite *backlight_ref_white =
    META_BACKLIGHT_REF_WHITE (backlight);
  g_autoptr (GTask) task = NULL;

  task = g_task_new (backlight_ref_white, cancellable, callback, user_data);
  g_task_set_task_data (task, GINT_TO_POINTER (brightness_target), NULL);

  /* the parent class ensures we only ever have one ongoing task */
  g_assert (backlight_ref_white->change_ref_white_handle_id == 0);

  /* We have to do this in an idle because backlights can be changed from
   * the shell in a frame clock dispatch, and changing the ColorDevice
   * reference white invalidates the onscreen. */
  backlight_ref_white->change_ref_white_handle_id =
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                     on_change_ref_white,
                     g_steal_pointer (&task),
                     g_object_unref);
}

static void
meta_backlight_ref_white_dispose (GObject *object)
{
  MetaBacklightRefWhite *backlight_ref_white =
    META_BACKLIGHT_REF_WHITE (object);

  g_clear_handle_id (&backlight_ref_white->change_ref_white_handle_id,
                     g_source_remove);

  G_OBJECT_CLASS (meta_backlight_ref_white_parent_class)->dispose (object);
}

static int
meta_backlight_ref_white_set_brightness_finish (MetaBacklight  *backlight,
                                                GAsyncResult   *result,
                                                GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, backlight), -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_backlight_ref_white_class_init (MetaBacklightRefWhiteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaBacklightClass *backlight_class = META_BACKLIGHT_CLASS (klass);

  object_class->dispose = meta_backlight_ref_white_dispose;

  backlight_class->set_brightness = meta_backlight_ref_white_set_brightness;
  backlight_class->set_brightness_finish =
    meta_backlight_ref_white_set_brightness_finish;
}

static void
meta_backlight_ref_white_init (MetaBacklightRefWhite *backlight)
{
}

MetaBacklightRefWhite *
meta_backlight_ref_white_new (MetaBackend *backend,
                              MetaMonitor *monitor,
                              float        original_ref_white)
{
  g_autoptr (MetaBacklightRefWhite) backlight = NULL;

  backlight = g_object_new (META_TYPE_BACKLIGHT_REF_WHITE,
                            "backend", backend,
                            "name", meta_monitor_get_connector (monitor),
                            "brightness-min", 10,
                            "brightness-max", 210,
                            NULL);
  backlight->monitor = monitor;
  backlight->original_ref_white = original_ref_white;

  meta_backlight_update_brightness_target (META_BACKLIGHT (backlight),
                                           (int)(get_factor (backlight) * 100));

  return g_steal_pointer (&backlight);
}

float
meta_backlight_ref_white_get_original_ref_white (MetaBacklightRefWhite *backlight)
{
  return backlight->original_ref_white;
}
