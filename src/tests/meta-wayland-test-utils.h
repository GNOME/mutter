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

#ifndef META_WAYLAND_TEST_UTILS_H
#define META_WAYLAND_TEST_UTILS_H

typedef struct _MetaWaylandTestClient MetaWaylandTestClient;

MetaWaylandTestClient * meta_wayland_test_client_new (const char *test_client_name);

void meta_wayland_test_client_finish (MetaWaylandTestClient *wayland_test_client);

#endif /* META_WAYLAND_TEST_UTILS_H */
