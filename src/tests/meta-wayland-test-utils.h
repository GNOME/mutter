/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
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

#include "meta/meta-context.h"

typedef struct _MetaWaylandTestClient MetaWaylandTestClient;

MetaWaylandTestClient * meta_wayland_test_client_new (MetaContext *context,
                                                      const char  *test_client_name);

MetaWaylandTestClient * meta_wayland_test_client_new_with_args (MetaContext *context,
                                                                const char  *test_client_name,
                                                                ...) G_GNUC_NULL_TERMINATED;

void meta_wayland_test_client_finish (MetaWaylandTestClient *wayland_test_client);

void meta_wayland_test_client_terminate (MetaWaylandTestClient *wayland_test_client);

MetaWindow * meta_find_client_window (MetaContext *context,
                                      const char  *title);

MetaWindow * meta_wait_for_client_window (MetaContext *context,
                                          const char  *title);
