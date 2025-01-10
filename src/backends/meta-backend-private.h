/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include <glib-object.h>
#include <xkbcommon/xkbcommon.h>

#include "meta/meta-backend.h"
#include "meta/meta-idle-monitor.h"
#include "meta/meta-orientation-manager.h"
#include "backends/meta-backend-types.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-egl.h"
#include "backends/meta-input-mapper-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-pointer-constraint.h"
#include "backends/meta-renderer.h"
#include "backends/meta-settings-private.h"
#include "core/meta-context-private.h"
#include "core/util-private.h"

#define DEFAULT_XKB_RULES_FILE "evdev"
#define DEFAULT_XKB_MODEL "pc105+inet"

typedef enum
{
  META_SEQUENCE_NONE,
  META_SEQUENCE_ACCEPTED,
  META_SEQUENCE_REJECTED,
  META_SEQUENCE_PENDING_END
} MetaSequenceState;

struct _MetaBackendClass
{
  GObjectClass parent_class;

  ClutterBackend * (* create_clutter_backend) (MetaBackend    *backend,
                                               ClutterContext *context);

  void (* post_init) (MetaBackend *backend);

  MetaBackendCapabilities (* get_capabilities) (MetaBackend *backend);

  MetaMonitorManager * (* create_monitor_manager) (MetaBackend *backend,
                                                   GError     **error);
  MetaColorManager * (* create_color_manager) (MetaBackend *backend);
  MetaCursorRenderer * (* get_cursor_renderer) (MetaBackend        *backend,
                                                ClutterInputDevice *device);
  MetaCursorTracker * (* create_cursor_tracker) (MetaBackend *backend);
  MetaRenderer * (* create_renderer) (MetaBackend *backend,
                                      GError     **error);
  MetaInputSettings * (* get_input_settings) (MetaBackend *backend);

  ClutterSeat * (* create_default_seat) (MetaBackend  *backend,
                                         GError      **error);

  gboolean (* grab_device) (MetaBackend *backend,
                            int          device_id,
                            uint32_t     timestamp);
  gboolean (* ungrab_device) (MetaBackend *backend,
                              int          device_id,
                              uint32_t     timestamp);

  void (* freeze_keyboard) (MetaBackend *backend,
                            uint32_t     timestamp);

  void (* unfreeze_keyboard) (MetaBackend *backend,
                              uint32_t     timestamp);

  void (* ungrab_keyboard) (MetaBackend *backend,
                            uint32_t     timestamp);

  void (* finish_touch_sequence) (MetaBackend          *backend,
                                  ClutterEventSequence *sequence,
                                  MetaSequenceState     state);
  MetaLogicalMonitor * (* get_current_logical_monitor) (MetaBackend *backend);

  void (* set_keymap) (MetaBackend *backend,
                       const char  *layouts,
                       const char  *variants,
                       const char  *options,
                       const char  *model);

  gboolean (* is_lid_closed) (MetaBackend *backend);

  struct xkb_keymap * (* get_keymap) (MetaBackend *backend);

  xkb_layout_index_t (* get_keymap_layout_group) (MetaBackend *backend);

  void (* lock_layout_group) (MetaBackend *backend,
                              guint        idx);

  void (* update_stage) (MetaBackend *backend);
  void (* select_stage_events) (MetaBackend *backend);

  void (* set_pointer_constraint) (MetaBackend           *backend,
                                   MetaPointerConstraint *constraint);

  gboolean (* is_headless) (MetaBackend *backend);
};

void meta_backend_destroy (MetaBackend *backend);

META_EXPORT_TEST
ClutterBackend * meta_backend_get_clutter_backend (MetaBackend *backend);

META_EXPORT_TEST
ClutterContext * meta_backend_get_clutter_context (MetaBackend *backend);

META_EXPORT_TEST
ClutterSeat * meta_backend_get_default_seat (MetaBackend *backend);

MetaIdleMonitor * meta_backend_get_idle_monitor (MetaBackend        *backend,
                                                 ClutterInputDevice *device);

MetaIdleManager * meta_backend_get_idle_manager (MetaBackend *backend);

META_EXPORT_TEST
MetaColorManager * meta_backend_get_color_manager (MetaBackend *backend);

META_EXPORT_TEST
MetaCursorTracker * meta_backend_get_cursor_tracker (MetaBackend *backend);
MetaCursorRenderer * meta_backend_get_cursor_renderer_for_device (MetaBackend        *backend,
                                                                  ClutterInputDevice *device);
META_EXPORT_TEST
MetaCursorRenderer * meta_backend_get_cursor_renderer (MetaBackend *backend);
META_EXPORT_TEST
MetaRenderer * meta_backend_get_renderer (MetaBackend *backend);
MetaEgl * meta_backend_get_egl (MetaBackend *backend);

MetaDbusSessionWatcher * meta_backend_get_dbus_session_watcher (MetaBackend *backend);

#ifdef HAVE_REMOTE_DESKTOP
MetaRemoteDesktop * meta_backend_get_remote_desktop (MetaBackend *backend);

MetaScreenCast * meta_backend_get_screen_cast (MetaBackend *backend);
#endif

MetaInputCapture * meta_backend_get_input_capture (MetaBackend *backend);

gboolean meta_backend_grab_device (MetaBackend *backend,
                                   int          device_id,
                                   uint32_t     timestamp);
gboolean meta_backend_ungrab_device (MetaBackend *backend,
                                     int          device_id,
                                     uint32_t     timestamp);

void meta_backend_finish_touch_sequence (MetaBackend          *backend,
                                         ClutterEventSequence *sequence,
                                         MetaSequenceState     state);

META_EXPORT_TEST
MetaLogicalMonitor * meta_backend_get_current_logical_monitor (MetaBackend *backend);

struct xkb_keymap * meta_backend_get_keymap (MetaBackend *backend);

xkb_layout_index_t meta_backend_get_keymap_layout_group (MetaBackend *backend);

gboolean meta_backend_is_lid_closed (MetaBackend *backend);

void meta_backend_set_client_pointer_constraint (MetaBackend *backend,
                                                 MetaPointerConstraint *constraint);

void meta_backend_monitors_changed (MetaBackend *backend);

gboolean meta_backend_is_stage_views_scaled (MetaBackend *backend);

MetaInputMapper *meta_backend_get_input_mapper (MetaBackend *backend);
MetaInputSettings *meta_backend_get_input_settings (MetaBackend *backend);

void meta_backend_notify_keymap_changed (MetaBackend *backend);

void meta_backend_notify_keymap_layout_group_changed (MetaBackend *backend,
                                                      unsigned int locked_group);

META_EXPORT_TEST
void meta_backend_add_gpu (MetaBackend *backend,
                           MetaGpu     *gpu);

META_EXPORT_TEST
GList * meta_backend_get_gpus (MetaBackend *backend);

#ifdef HAVE_LIBWACOM
WacomDeviceDatabase * meta_backend_get_wacom_database (MetaBackend *backend);
#endif

void meta_backend_add_hw_cursor_inhibitor (MetaBackend           *backend,
                                           MetaHwCursorInhibitor *inhibitor);

void meta_backend_remove_hw_cursor_inhibitor (MetaBackend           *backend,
                                              MetaHwCursorInhibitor *inhibitor);

void meta_backend_inhibit_hw_cursor (MetaBackend *backend);

void meta_backend_uninhibit_hw_cursor (MetaBackend *backend);

META_EXPORT_TEST
gboolean meta_backend_is_hw_cursors_inhibited (MetaBackend *backend);

void meta_backend_update_from_event (MetaBackend  *backend,
                                     ClutterEvent *event);

char * meta_backend_get_vendor_name (MetaBackend *backend,
                                     const char  *pnp_id);

META_EXPORT_TEST
uint32_t meta_clutter_button_to_evdev (uint32_t clutter_button);

META_EXPORT_TEST
uint32_t meta_clutter_tool_button_to_evdev (uint32_t clutter_button);

META_EXPORT_TEST
uint32_t meta_evdev_button_to_clutter (uint32_t evdev_button);

META_EXPORT_TEST
uint32_t meta_evdev_tool_button_to_clutter (uint32_t evdev_button);
