/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#pragma once

#include "wayland/meta-wayland.h"

#define META_TYPE_WAYLAND_TEST_DRIVER (meta_wayland_test_driver_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandTestDriver, meta_wayland_test_driver,
                      META, WAYLAND_TEST_DRIVER,
                      GObject)

MetaWaylandTestDriver * meta_wayland_test_driver_new (MetaWaylandCompositor *compositor);

void meta_wayland_test_driver_emit_sync_event (MetaWaylandTestDriver *test_driver,
                                               uint32_t               serial);

void meta_wayland_test_driver_set_property (MetaWaylandTestDriver *test_driver,
                                            const char            *name,
                                            const char            *value);

void meta_wayland_test_driver_set_property_int (MetaWaylandTestDriver *test_driver,
                                                const char            *name,
                                                int32_t                value);

void meta_wayland_test_driver_wait_for_sync_point (MetaWaylandTestDriver *test_driver,
                                                   unsigned int           sync_point);

void meta_wayland_test_driver_terminate (MetaWaylandTestDriver *test_driver);
