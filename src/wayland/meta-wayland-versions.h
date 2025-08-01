/*
 * Wayland Support
 *
 * Copyright (C) 2012,2013 Intel Corporation
 *               2013 Red Hat, Inc.
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

/* Protocol objects, will never change version */
/* #define META_WL_DISPLAY_VERSION  1 */
/* #define META_WL_REGISTRY_VERSION 1 */
#define META_WL_CALLBACK_VERSION 1

/* Not handled by mutter-wayland directly */
/* #define META_WL_SHM_VERSION        1 */
/* #define META_WL_SHM_POOL_VERSION   1 */
/* #define META_WL_DRM_VERSION        1 */
/* #define META_WL_BUFFER_VERSION     1 */

/* Global/master objects (version exported by wl_registry and negotiated through bind) */
#define META_WL_COMPOSITOR_VERSION          6
#define META_WL_DATA_DEVICE_MANAGER_VERSION 3
#define META_XDG_WM_BASE_VERSION            7
#define META_WL_SEAT_VERSION                8
#define META_WL_OUTPUT_VERSION              4
#define META_XSERVER_VERSION                1
#define META_GTK_SHELL1_VERSION             6
#define META_WL_SUBCOMPOSITOR_VERSION       1
#define META_ZWP_POINTER_GESTURES_V1_VERSION 3
#define META_ZXDG_EXPORTER_V1_VERSION       1
#define META_ZXDG_IMPORTER_V1_VERSION       1
#define META_ZXDG_EXPORTER_V2_VERSION       1
#define META_ZXDG_IMPORTER_V2_VERSION       1
#define META_ZWP_KEYBOARD_SHORTCUTS_INHIBIT_V1_VERSION 1
#define META_ZXDG_OUTPUT_V1_VERSION         3
#define META_ZWP_XWAYLAND_KEYBOARD_GRAB_V1_VERSION 1
#define META_ZWP_TEXT_INPUT_V3_VERSION      1
#define META_WP_VIEWPORTER_VERSION          1
#define META_ZWP_PRIMARY_SELECTION_V1_VERSION 1
#define META_WP_PRESENTATION_VERSION        2
#define META_XDG_ACTIVATION_V1_VERSION 1
#define META_ZWP_IDLE_INHIBIT_V1_VERSION    1
#define META_WP_SINGLE_PIXEL_BUFFER_V1_VERSION 1
#define META_MUTTER_X11_INTEROP_VERSION 1
#define META_WP_FRACTIONAL_SCALE_VERSION 1
#define META_WP_COLOR_MANAGEMENT_VERSION 1
#define META_XDG_DIALOG_VERSION 1
#define META_WP_DRM_LEASE_DEVICE_V1_VERSION 1
#define META_XDG_SESSION_MANAGER_V1_VERSION 1
#define META_WP_SYSTEM_BELL_V1_VERSION 1
#define META_XDG_TOPLEVEL_DRAG_VERSION 1
#define META_WP_COMMIT_TIMING_V1_VERSION 1
#define META_WP_FIFO_V1_VERSION 1
#define META_WP_CURSOR_SHAPE_VERSION 2
#define META_XDG_TOPLEVEL_TAG_V1_VERSION 1
#define META_WP_COLOR_REPRESENTATION_VERSION 1
#define META_WL_FIXES_VERSION 1
#define META_WP_POINTER_WARP_VERSION 1
