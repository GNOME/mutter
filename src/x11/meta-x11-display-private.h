/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X display handler */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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
#include <X11/Xlib.h>

#include "backends/meta-monitor-manager-private.h"
#include "core/display-private.h"
#include "meta/common.h"
#include "meta/meta-selection-source.h"
#include "meta/types.h"
#include "meta/meta-x11-display.h"
#include "meta-startup-notification-x11.h"
#include "meta-x11-stack-private.h"
#include "x11/meta-sync-counter.h"

typedef struct _MetaGroupPropHooks  MetaGroupPropHooks;
typedef struct _MetaWindowPropHooks MetaWindowPropHooks;

typedef gboolean (*MetaAlarmFilter) (MetaX11Display        *x11_display,
                                     XSyncAlarmNotifyEvent *event,
                                     gpointer               data);

typedef struct _MetaX11AlarmFilter
{
  MetaAlarmFilter filter;
  gpointer user_data;
} MetaX11AlarmFilter;

struct _MetaX11Display
{
  GObject parent;

  MetaDisplay *display;

  char *name;
  char *screen_name;

  Display *xdisplay;
  Window xroot;
  int default_depth;
  Visual *default_xvisual;

  guint32 timestamp;

  /* Pull in all the names of atoms as fields; we will intern them when the
   * class is constructed.
   */
#define item(x) Atom atom_##x;
#include "x11/atomnames.h"
#undef item

  Window leader_window;
  Window timestamp_pinging_window;

  /* The window and serial of the most recent FocusIn event. */
  Window server_focus_window;
  gulong server_focus_serial;

  /* For windows we've focused that don't necessarily have an X window,
   * like the no_focus_window or the stage X window. */
  Window focus_xwindow;
  gulong focus_serial;

  /* This window holds the focus when we don't want to focus
   * any actual clients
   */
  Window no_focus_window;

  /* Instead of unmapping withdrawn windows we can leave them mapped
   * and restack them below a guard window. When using a compositor
   * this allows us to provide live previews of unmapped windows */
  Window guard_window;

  Window wm_sn_selection_window;
  Atom wm_sn_atom;
  guint32 wm_sn_timestamp;

  guint display_close_idle;
  guint32 xselectionclear_timestamp;

  Window wm_cm_selection_window;

  Window composite_overlay_window;

  GHashTable *xids;
  GHashTable *alarms;

  GList *event_funcs;

  gboolean has_xinerama_indices;

  /* Managed by group.c */
  GHashTable *groups_by_leader;

  /* Managed by window-props.c */
  MetaWindowPropHooks *prop_hooks_table;
  GHashTable *prop_hooks;
  int n_prop_hooks;

  /* Managed by group-props.c */
  MetaGroupPropHooks *group_prop_hooks;

  int xkb_base_event_type;
  guint32 last_bell_time;

  GPtrArray *alarm_filters;

  GSubprocess *frames_client;
  GCancellable *frames_client_cancellable;

  GSource *event_source;

  struct {
    Window xwindow;
    guint timeout_id;
    MetaSelectionSource *owners[META_N_SELECTION_TYPES];
    GCancellable *cancellables[META_N_SELECTION_TYPES];

    GList *input_streams;
    GList *output_streams;
  } selection;

  /* If true, server->focus_serial refers to us changing the focus; in
   * this case, we can ignore focus events that have exactly focus_serial,
   * since we take care to make another request immediately afterwards.
   * But if focus is being changed by another client, we have to accept
   * multiple events with the same serial.
   */
  guint focused_by_us : 1;

  guint keys_grabbed : 1;

  guint closing : 1;

  int composite_event_base;
  int composite_error_base;
  int composite_major_version;
  int composite_minor_version;
  int damage_event_base;
  int damage_error_base;
  int xfixes_event_base;
  int xfixes_error_base;
  int xinput_error_base;
  int xinput_event_base;
  int xinput_opcode;
  int xsync_event_base;
  int xsync_error_base;
  int shape_event_base;
  int shape_error_base;
  unsigned int have_xsync : 1;
#define META_X11_DISPLAY_HAS_XSYNC(x11_display) ((x11_display)->have_xsync)
  unsigned int have_shape : 1;
#define META_X11_DISPLAY_HAS_SHAPE(x11_display) ((x11_display)->have_shape)
  unsigned int have_composite : 1;
  unsigned int have_damage : 1;
#define META_X11_DISPLAY_HAS_COMPOSITE(x11_display) ((x11_display)->have_composite)
#define META_X11_DISPLAY_HAS_DAMAGE(x11_display) ((x11_display)->have_damage)

  MetaX11StartupNotification *startup_notification;
  MetaX11Stack *x11_stack;

  XserverRegion empty_region;

  unsigned int reload_x11_cursor_later;
};

MetaX11Display *meta_x11_display_new (MetaDisplay *display, GError **error);

void meta_x11_display_restore_active_workspace (MetaX11Display *x11_display);

MetaWindow *meta_x11_display_lookup_x_window     (MetaX11Display *x11_display,
                                                  Window          xwindow);
void        meta_x11_display_register_x_window   (MetaX11Display *x11_display,
                                                  Window         *xwindowp,
                                                  MetaWindow     *window);
void        meta_x11_display_unregister_x_window (MetaX11Display *x11_display,
                                                  Window          xwindow);

MetaSyncCounter * meta_x11_display_lookup_sync_alarm (MetaX11Display *x11_display,
                                                      XSyncAlarm      alarm);

void        meta_x11_display_register_sync_alarm (MetaX11Display  *x11_display,
                                                  XSyncAlarm      *alarmp,
                                                  MetaSyncCounter *sync_counter);

void        meta_x11_display_unregister_sync_alarm (MetaX11Display *x11_display,
                                                    XSyncAlarm      alarm);

META_EXPORT
MetaX11AlarmFilter * meta_x11_display_add_alarm_filter (MetaX11Display  *x11_display,
                                                        MetaAlarmFilter  filter,
                                                        gpointer         user_data);

META_EXPORT
void meta_x11_display_remove_alarm_filter (MetaX11Display     *x11_display,
                                           MetaX11AlarmFilter *alarm_filter);

void meta_x11_display_create_guard_window (MetaX11Display *x11_display);

guint32 meta_x11_display_get_current_time_roundtrip (MetaX11Display *x11_display);

int meta_x11_display_logical_monitor_to_xinerama_index (MetaX11Display     *x11_display,
                                                        MetaLogicalMonitor *logical_monitor);

MetaLogicalMonitor *meta_x11_display_xinerama_index_to_logical_monitor (MetaX11Display *x11_display,
                                                                        int             xinerama_index);

void meta_x11_display_update_workspace_layout (MetaX11Display *x11_display);
void meta_x11_display_update_workspace_names  (MetaX11Display *x11_display);

void meta_x11_display_update_focus_window (MetaX11Display *x11_display,
                                           Window          xwindow,
                                           gulong          serial,
                                           gboolean        focused_by_us);

MetaDisplay * meta_x11_display_get_display (MetaX11Display *x11_display);

void meta_x11_display_run_event_funcs (MetaX11Display *x11_display,
                                       XEvent         *xevent);

int meta_x11_display_get_screen_number (MetaX11Display *x11_display);

int meta_x11_display_get_damage_event_base (MetaX11Display *x11_display);

gboolean meta_x11_display_xwindow_is_a_no_focus_window (MetaX11Display *x11_display,
                                                        Window xwindow);

void meta_x11_display_clear_stage_input_region (MetaX11Display *x11_display);
