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

#ifndef MONITOR_UNIT_TESTS_H
#define MONITOR_UNIT_TESTS_H

#include "core/util-private.h"
#include "tests/monitor-test-utils.h"

typedef struct _MonitorTestCase MonitorTestCase;

void init_monitor_tests (void);

void pre_run_monitor_tests (MetaContext *context);

void finish_monitor_tests (void);

MonitorTestCase * test_get_initial_monitor_test_case (void);

#endif /* MONITOR_UNIT_TESTS_H */
