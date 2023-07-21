/*
 * Copyright 2023 Red Hat
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

#include <glib.h>
#include <glib-object.h>
#include <meta/common.h>
#include <meta/meta-context.h>

#define META_TYPE_TEST_MONITOR (meta_test_monitor_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaTestMonitor, meta_test_monitor,
                      META, TEST_MONITOR, GObject)

META_EXPORT
MetaTestMonitor * meta_test_monitor_new (MetaContext  *context,
                                         int           width,
                                         int           height,
                                         float         refresh_rate,
                                         GError      **error);

META_EXPORT
void meta_test_monitor_destroy (MetaTestMonitor *test_monitor);
