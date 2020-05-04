/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#include "tests/meta-backend-test.h"

#include "tests/meta-gpu-test.h"
#include "tests/meta-monitor-manager-test.h"

struct _MetaBackendTest
{
  MetaBackendX11Nested parent;

  MetaGpu *gpu;

  gboolean is_lid_closed;
};

G_DEFINE_TYPE (MetaBackendTest, meta_backend_test, META_TYPE_BACKEND_X11_NESTED)

void
meta_backend_test_set_is_lid_closed (MetaBackendTest *backend_test,
                                     gboolean         is_lid_closed)
{
  backend_test->is_lid_closed = is_lid_closed;
}

MetaGpu *
meta_backend_test_get_gpu (MetaBackendTest *backend_test)
{
  return backend_test->gpu;
}

static gboolean
meta_backend_test_is_lid_closed (MetaBackend *backend)
{
  MetaBackendTest *backend_test = META_BACKEND_TEST (backend);

  return backend_test->is_lid_closed;
}

static void
meta_backend_test_init_gpus (MetaBackendX11Nested *backend_x11_nested)
{
  MetaBackendTest *backend_test = META_BACKEND_TEST (backend_x11_nested);

  backend_test->gpu = g_object_new (META_TYPE_GPU_TEST,
                                    "backend", backend_test,
                                    NULL);
  meta_backend_add_gpu (META_BACKEND (backend_test), backend_test->gpu);
}

static void
meta_backend_test_init (MetaBackendTest *backend_test)
{
}

static MetaMonitorManager *
meta_backend_test_create_monitor_manager (MetaBackend *backend,
                                          GError     **error)
{
  return g_object_new (META_TYPE_MONITOR_MANAGER_TEST,
                       "backend", backend,
                       NULL);
}

ClutterInputDevice *
meta_backend_test_add_test_device (MetaBackendTest        *backend_test,
                                   const char             *name,
                                   ClutterInputDeviceType  device_type,
                                   int                     n_buttons)
{
  g_autoptr (GList) devices = NULL;
  MetaBackend *backend = META_BACKEND (backend_test);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterInputDevice *device;
  ClutterEvent *event;
  const char *product_id;
  bool has_cursor = TRUE;

  switch (device_type)
    {
    case CLUTTER_POINTER_DEVICE:
      product_id = "MetaTestPointer";
      break;
    case CLUTTER_KEYBOARD_DEVICE:
      product_id = "MetaTestKeyboard";
      has_cursor = FALSE;
      break;
    case CLUTTER_EXTENSION_DEVICE:
      product_id = "MetaTestExtension";
      has_cursor = FALSE;
      break;
    case CLUTTER_JOYSTICK_DEVICE:
      product_id = "MetaTestJoystick";
      break;
    case CLUTTER_TABLET_DEVICE:
      product_id = "MetaTestTablet";
      break;
    case CLUTTER_TOUCHPAD_DEVICE:
      product_id = "MetaTestTouchpad";
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      product_id = "MetaTestTouchscreen";
      break;
    case CLUTTER_PEN_DEVICE:
      product_id = "MetaTestPen";
      break;
    case CLUTTER_ERASER_DEVICE:
      product_id = "MetaTestEraser";
      break;
    case CLUTTER_CURSOR_DEVICE:
      product_id = "MetaTestCursor";
      break;
    case CLUTTER_PAD_DEVICE:
      product_id = "MetaTestPad";
      has_cursor = FALSE;
      break;

    default:
      g_assert_not_reached ();
    }

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE,
                         "name", name,
                         "device-type", CLUTTER_TOUCHSCREEN_DEVICE,
                         "seat", seat,
                         "has-cursor", has_cursor,
                         "backend", clutter_backend,
                         "vendor-id", "MetaTest",
                         "product-id", product_id,
                         "n-buttons", n_buttons,
                         NULL);

  event = clutter_event_new (CLUTTER_DEVICE_ADDED);
  clutter_event_set_device (event, device);
  clutter_event_set_stage (event, stage);
  clutter_event_put (event);
  clutter_event_free (event);

  return device;
}

void
meta_backend_test_remove_device (MetaBackendTest    *backend_test,
                                 ClutterInputDevice *device)
{
  MetaBackend *backend = META_BACKEND (backend_test);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterEvent *event;

  event = clutter_event_new (CLUTTER_DEVICE_REMOVED);
  clutter_event_set_device (event, device);
  clutter_event_set_stage (event, stage);
  clutter_event_put (event);
  clutter_event_free (event);
}

static void
meta_backend_test_class_init (MetaBackendTestClass *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);
  MetaBackendX11NestedClass *backend_x11_nested_class =
    META_BACKEND_X11_NESTED_CLASS (klass);

  backend_class->create_monitor_manager = meta_backend_test_create_monitor_manager;
  backend_class->is_lid_closed = meta_backend_test_is_lid_closed;

  backend_x11_nested_class->init_gpus = meta_backend_test_init_gpus;
}
