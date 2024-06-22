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

#include "backends/meta-color-manager.h"
#include "tests/meta-gpu-test.h"
#include "tests/meta-monitor-manager-test.h"

struct _MetaBackendTest
{
  MetaBackendNative parent;

  MetaGpu *gpu;

  gboolean is_lid_closed;
};

static GInitableIface *initable_parent_iface;

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaBackendTest, meta_backend_test, META_TYPE_BACKEND_NATIVE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

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

static MetaMonitorManager *
meta_backend_test_create_monitor_manager (MetaBackend *backend,
                                          GError     **error)
{
  return g_object_new (META_TYPE_MONITOR_MANAGER_TEST,
                       "backend", backend,
                       NULL);
}

static MetaColorManager *
meta_backend_test_create_color_manager (MetaBackend *backend)
{
  return g_object_new (META_TYPE_COLOR_MANAGER,
                       "backend", backend,
                       NULL);
}

static void
set_true_cb (gboolean *value)
{
  *value = TRUE;
}

ClutterVirtualInputDevice *
meta_backend_test_add_test_device (MetaBackendTest        *backend_test,
                                   ClutterInputDeviceType  device_type,
                                   int                     n_buttons)
{
  MetaBackend *backend = META_BACKEND (backend_test);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterVirtualInputDevice *virtual_device;
  gboolean was_updated = FALSE;

  g_signal_connect_swapped (seat, "device-added", G_CALLBACK (set_true_cb),
                            &was_updated);

  virtual_device = clutter_seat_create_virtual_device (seat, device_type);

  while (!was_updated)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handlers_disconnect_by_func (seat, set_true_cb, &was_updated);

  return virtual_device;
}

void
meta_backend_test_remove_test_device (MetaBackendTest           *backend_test,
                                      ClutterVirtualInputDevice *virtual_device)
{
  MetaBackend *backend = META_BACKEND (backend_test);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  gboolean was_updated = FALSE;

  g_signal_connect_swapped (seat, "device-removed", G_CALLBACK (set_true_cb),
                            &was_updated);

  g_object_run_dispose (G_OBJECT (virtual_device));

  while (!was_updated)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handlers_disconnect_by_func (seat, set_true_cb, &was_updated);
}

static gboolean
meta_backend_test_initable_init (GInitable     *initable,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  MetaBackendTest *backend_test = META_BACKEND_TEST (initable);

  backend_test->gpu = g_object_new (META_TYPE_GPU_TEST,
                                    "backend", backend_test,
                                    NULL);
  meta_backend_add_gpu (META_BACKEND (backend_test), backend_test->gpu);

  if (!initable_parent_iface->init (initable, cancellable, error))
    return FALSE;

  return TRUE;
}

static void
meta_backend_test_init (MetaBackendTest *backend_test)
{
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_backend_test_initable_init;
}

static void
meta_backend_test_class_init (MetaBackendTestClass *klass)
{
  MetaBackendClass *backend_class = META_BACKEND_CLASS (klass);

  backend_class->create_monitor_manager = meta_backend_test_create_monitor_manager;
  backend_class->create_color_manager = meta_backend_test_create_color_manager;
  backend_class->is_lid_closed = meta_backend_test_is_lid_closed;
}
