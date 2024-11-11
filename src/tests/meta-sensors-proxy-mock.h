/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2020 Canonical, Ltd.
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
 * Author: Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include "meta/meta-orientation-manager.h"

typedef GDBusProxy MetaSensorsProxyMock;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaSensorsProxyMock, g_object_unref)

META_EXPORT
MetaSensorsProxyMock * meta_sensors_proxy_mock_get (void);

META_EXPORT
void meta_sensors_proxy_mock_set_property (MetaSensorsProxyMock *proxy,
                                           const gchar          *property_name,
                                           GVariant             *value);

META_EXPORT
void meta_sensors_proxy_mock_set_orientation (MetaSensorsProxyMock *proxy,
                                              MetaOrientation       orientation);

META_EXPORT
void meta_sensors_proxy_mock_wait_accelerometer_claimed (MetaSensorsProxyMock *proxy,
                                                         gboolean              claimed);
