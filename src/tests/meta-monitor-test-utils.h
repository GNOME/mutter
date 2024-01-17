/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat, Inc.
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

#include "tests/meta-monitor-manager-test.h"
#include "tests/meta-test-utils.h"
#include "backends/meta-crtc.h"
#include "backends/meta-monitor.h"
#include "backends/meta-output.h"

#define MAX_N_MODES 25
#define MAX_N_OUTPUTS 10
#define MAX_N_CRTCS 10
#define MAX_N_MONITORS 10
#define MAX_N_LOGICAL_MONITORS 10
#define MAX_N_SCALES 20

/*
 * The following structures are used to define test cases.
 *
 * Each test case consists of a test case setup and a test case expectaction.
 * and a expected result, consisting
 * of an array of monitors, logical monitors and a screen size.
 *
 * TEST CASE SETUP:
 *
 * A test case setup consists of an array of modes, an array of outputs and an
 * array of CRTCs.
 *
 * A mode has a width and height in pixels, and a refresh rate in updates per
 * second.
 *
 * An output has an array of available modes, and a preferred mode. Modes are
 * defined as indices into the modes array of the test case setup.
 *
 * It also has CRTc and an array of possible CRTCs. Crtcs are defined as indices
 * into the CRTC array. The CRTC value -1 means no CRTC.
 *
 * It also has various meta data, such as physical dimension, tile info and
 * scale.
 *
 * A CRTC only has a current mode. A mode is defined as an index into the modes
 * array.
 *
 *
 * TEST CASE EXPECTS:
 *
 * A test case expects consists of an array of monitors, an array of logical
 * monitors, a output and crtc count, and a screen width.
 *
 * A monitor represents a physical monitor (such as an external monitor, or a
 * laptop panel etc). A monitor consists of an array of outputs, defined by
 * indices into the setup output array, an array of monitor modes, and the
 * current mode, defined by an index into the monitor modes array, and the
 * physical dimensions.
 *
 * A logical monitor represents a region of the total screen area. It contains
 * the expected layout and a scale.
 */

typedef enum _MonitorTestFlag
{
  MONITOR_TEST_FLAG_NONE,
  MONITOR_TEST_FLAG_NO_STORED
} MonitorTestFlag;

typedef struct _MonitorTestCaseMode
{
  int width;
  int height;
  float refresh_rate;
  MetaCrtcModeFlag flags;
} MonitorTestCaseMode;

typedef struct _MonitorTestCaseOutput
{
  int crtc;
  int modes[MAX_N_MODES];
  int n_modes;
  int preferred_mode;
  int possible_crtcs[MAX_N_CRTCS];
  int n_possible_crtcs;
  int width_mm;
  int height_mm;
  MetaTileInfo tile_info;
  float scale;
  gboolean is_laptop_panel;
  gboolean is_underscanning;
  unsigned int max_bpc;
  MetaOutputRGBRange rgb_range;
  const char *serial;
  MetaMonitorTransform panel_orientation_transform;
  gboolean hotplug_mode;
  int suggested_x;
  int suggested_y;
  gboolean has_edid_info;
  MetaEdidInfo edid_info;
} MonitorTestCaseOutput;

typedef struct _MonitorTestCaseCrtc
{
  int current_mode;
  gboolean disable_gamma_lut;
} MonitorTestCaseCrtc;

typedef struct _MonitorTestCaseSetup
{
  MonitorTestCaseMode modes[MAX_N_MODES];
  int n_modes;

  MonitorTestCaseOutput outputs[MAX_N_OUTPUTS];
  int n_outputs;

  MonitorTestCaseCrtc crtcs[MAX_N_CRTCS];
  int n_crtcs;
} MonitorTestCaseSetup;

typedef struct _MonitorTestCaseMonitorCrtcMode
{
  uint64_t output;
  int crtc_mode;
} MetaTestCaseMonitorCrtcMode;

typedef struct _MonitorTestCaseMonitorMode
{
  int width;
  int height;
  float refresh_rate;
  int n_scales;
  float scales[MAX_N_SCALES];
  MetaCrtcModeFlag flags;
  MetaTestCaseMonitorCrtcMode crtc_modes[MAX_N_CRTCS];
} MetaMonitorTestCaseMonitorMode;

typedef struct _MonitorTestCaseMonitor
{
  uint64_t outputs[MAX_N_OUTPUTS];
  int n_outputs;
  MetaMonitorTestCaseMonitorMode modes[MAX_N_MODES];
  int n_modes;
  int current_mode;
  int width_mm;
  int height_mm;
  gboolean is_underscanning;
  unsigned int max_bpc;
  MetaOutputRGBRange rgb_range;
} MonitorTestCaseMonitor;

typedef struct _MonitorTestCaseLogicalMonitor
{
  MtkRectangle layout;
  float scale;
  int monitors[MAX_N_MONITORS];
  int n_monitors;
  MetaMonitorTransform transform;
} MonitorTestCaseLogicalMonitor;

typedef struct _MonitorTestCaseCrtcExpect
{
  MetaMonitorTransform transform;
  int current_mode;
  float x;
  float y;
} MonitorTestCaseCrtcExpect;

typedef struct _MonitorTestCaseExpect
{
  MonitorTestCaseMonitor monitors[MAX_N_MONITORS];
  int n_monitors;
  MonitorTestCaseLogicalMonitor logical_monitors[MAX_N_LOGICAL_MONITORS];
  int n_logical_monitors;
  int primary_logical_monitor;
  int n_outputs;
  MonitorTestCaseCrtcExpect crtcs[MAX_N_CRTCS];
  int n_crtcs;
  int n_tiled_monitors;
  int screen_width;
  int screen_height;
} MonitorTestCaseExpect;

typedef struct _MonitorTestCase
{
  MonitorTestCaseSetup setup;
  MonitorTestCaseExpect expect;
} MonitorTestCase;

META_EXPORT
MetaGpu * meta_test_get_gpu (MetaBackend *backend);

META_EXPORT
void meta_set_custom_monitor_config (MetaContext *context,
                                     const char  *filename);

META_EXPORT
void meta_set_custom_monitor_system_config (MetaContext *context,
                                            const char  *filename);

META_EXPORT
char * meta_read_file (const char *file_path);

META_EXPORT
void meta_check_monitor_configuration (MetaContext           *context,
                                       MonitorTestCaseExpect *expect);

META_EXPORT
void meta_check_monitor_scales (MetaContext                 *context,
                                MonitorTestCaseExpect       *expect,
                                MetaMonitorScalesConstraint  scales_constraints);

META_EXPORT
MetaMonitorTestSetup * meta_create_monitor_test_setup (MetaBackend          *backend,
                                                       MonitorTestCaseSetup *setup,
                                                       MonitorTestFlag       flags);

META_EXPORT
const char * meta_orientation_to_string (MetaOrientation orientation);

META_EXPORT
void meta_wait_for_orientation (MetaOrientationManager *orientation_manager,
                                MetaOrientation         orientation,
                                unsigned int           *times_signalled_out);

META_EXPORT
void meta_wait_for_possible_orientation_change (MetaOrientationManager *orientation_manager,
                                                unsigned int           *times_signalled_out);
