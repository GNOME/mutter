/*
 * Copyright (C) 2016-2025 Red Hat, Inc.
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
#include "tests/meta-monitor-test-utils.h"

extern MetaContext *test_context;
extern MetaTestClient *wayland_monitor_test_client;
extern MetaTestClient *x11_monitor_test_client;
extern MonitorTestCase initial_test_case;

int meta_monitor_test_main (int     argc,
                            char   *argv[0],
                            void (* init_tests) (void));

void meta_add_monitor_test (const char *test_path,
                            GTestFunc   test_func);
