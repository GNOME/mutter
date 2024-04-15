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

#include "meta/display.h"

#include <glib.h>

#include "clutter/clutter.h"
#include "core/keybindings-private.h"
#include "core/meta-gesture-tracker-private.h"
#include "core/meta-pad-action-mapper.h"
#include "core/stack-tracker.h"
#include "core/startup-notification-private.h"
#include "meta/barrier.h"
#include "meta/boxes.h"
#include "meta/common.h"
#include "meta/meta-selection.h"
#include "meta/prefs.h"

typedef struct _MetaBell       MetaBell;
typedef struct _MetaStack      MetaStack;

typedef enum
{
  META_LIST_DEFAULT                   = 0,      /* normal windows */
  META_LIST_INCLUDE_OVERRIDE_REDIRECT = 1 << 0, /* normal and O-R */
  META_LIST_SORTED                    = 1 << 1, /* sort list by mru */
} MetaListWindowsFlags;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

typedef enum
{
  META_TILE_NONE,
  META_TILE_LEFT,
  META_TILE_RIGHT,
  META_TILE_MAXIMIZED
} MetaTileMode;

typedef void (* MetaDisplayWindowFunc) (MetaWindow *window,
                                        gpointer    user_data);

struct _MetaDisplay
{
  GObject parent_instance;

  MetaX11Display *x11_display;

  int clutter_event_filter;

  /* Our best guess as to the "currently" focused window (that is, the
   * window that we expect will be focused at the point when the X
   * server processes our next request), and the serial of the request
   * or event that caused this.
   */
  MetaWindow *focus_window;

  /* last timestamp passed to XSetInputFocus */
  guint32 last_focus_time;

  /* last user interaction time in any app */
  guint32 last_user_time;

  /* whether we're using mousenav (only relevant for sloppy&mouse focus modes;
   * !mouse_mode means "keynav mode")
   */
  guint mouse_mode : 1;

  /*< private-ish >*/
  GHashTable *stamps;
  GHashTable *wayland_windows;

  guint32 current_time;

  /* We maintain a sequence counter, incremented for each #MetaWindow
   * created.  This is exposed by meta_window_get_stable_sequence()
   * but is otherwise not used inside mutter.
   *
   * It can be useful to plugins which want to sort windows in a
   * stable fashion.
   */
  guint32 window_sequence_counter;

  /* Pings which we're waiting for a reply from */
  GSList     *pending_pings;

  /* Pending focus change */
  guint       focus_timeout_id;

  /* Pending autoraise */
  guint       autoraise_timeout_id;
  MetaWindow* autoraise_window;

  MetaKeyBindingManager key_binding_manager;

  /* Opening the display */
  unsigned int display_opening : 1;

  /* Closing down the display */
  int closing;

  /* Managed by compositor.c */
  MetaCompositor *compositor;

  MetaGestureTracker *gesture_tracker;
  ClutterEventSequence *pointer_emulating_sequence;

  ClutterActor *current_pad_osd;
  MetaPadActionMapper *pad_action_mapper;

  MetaStartupNotification *startup_notification;

  MetaCursor current_cursor;

  MetaStack *stack;
  MetaStackTracker *stack_tracker;

  GSList *startup_sequences;

  guint work_area_later;
  guint check_fullscreen_later;

  MetaBell *bell;
  MetaWorkspaceManager *workspace_manager;

  MetaSoundPlayer *sound_player;

  MetaSelectionSource *selection_source;
  GBytes *saved_clipboard;
  gchar *saved_clipboard_mimetype;
  MetaSelection *selection;
  GCancellable *saved_clipboard_cancellable;
};

struct _MetaDisplayClass
{
  GObjectClass parent_class;
};

#define XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) \
  ( (( (time1) < (time2) ) && ( (time2) - (time1) < ((guint32)-1)/2 )) ||     \
    (( (time1) > (time2) ) && ( (time1) - (time2) > ((guint32)-1)/2 ))        \
  )
/**
 * XSERVER_TIME_IS_BEFORE:
 *
 * See the docs for meta_display_xserver_time_is_before().
 */
#define XSERVER_TIME_IS_BEFORE(time1, time2)                          \
  ( (time1) == 0 ||                                                     \
    (XSERVER_TIME_IS_BEFORE_ASSUMING_REAL_TIMESTAMPS(time1, time2) && \
     (time2) != 0)                                                      \
  )

MetaDisplay * meta_display_new (MetaContext  *context,
                                GError      **error);

#ifdef HAVE_X11_CLIENT
void meta_display_manage_all_xwindows (MetaDisplay *display);
#endif

/* Utility function to compare the stacking of two windows */
int           meta_display_stack_cmp           (const void *a,
                                                const void *b);

/* Each MetaWindow is uniquely identified by a 64-bit "stamp"; unlike a
 * a MetaWindow *, a stamp will never be recycled
 */
MetaWindow* meta_display_lookup_stamp     (MetaDisplay *display,
                                           guint64      stamp);
void        meta_display_register_stamp   (MetaDisplay *display,
                                           guint64     *stampp,
                                           MetaWindow  *window);
void        meta_display_unregister_stamp (MetaDisplay *display,
                                           guint64      stamp);

/* A "stack id" is a XID or a stamp */
#define META_STACK_ID_IS_X11(id) ((id) < G_GUINT64_CONSTANT(0x100000000))

META_EXPORT_TEST
MetaWindow* meta_display_lookup_stack_id   (MetaDisplay *display,
                                            guint64      stack_id);

/* for debug logging only; returns a human-description of the stack
 * ID - a small number of buffers are recycled, so the result must
 * be used immediately or copied */
const char *meta_display_describe_stack_id (MetaDisplay *display,
                                            guint64      stack_id);

void        meta_display_register_wayland_window   (MetaDisplay *display,
                                                    MetaWindow  *window);
void        meta_display_unregister_wayland_window (MetaDisplay *display,
                                                    MetaWindow  *window);

void        meta_display_notify_window_created (MetaDisplay  *display,
                                                MetaWindow   *window);

META_EXPORT_TEST
GSList*     meta_display_list_windows        (MetaDisplay          *display,
                                              MetaListWindowsFlags  flags);

void     meta_display_grab_window_buttons    (MetaDisplay *display,
                                              MetaWindow  *window);
void     meta_display_ungrab_window_buttons  (MetaDisplay *display,
                                              MetaWindow  *window);

void meta_display_grab_focus_window_button   (MetaDisplay *display,
                                              MetaWindow  *window);
void meta_display_ungrab_focus_window_button (MetaDisplay *display,
                                              MetaWindow  *window);

void meta_display_ping_window      (MetaWindow  *window,
                                    guint32      serial);
void meta_display_pong_for_serial  (MetaDisplay *display,
                                    guint32      serial);

MetaGravity meta_resize_gravity_from_grab_op (MetaGrabOp op);

gboolean meta_grab_op_is_moving   (MetaGrabOp op);
gboolean meta_grab_op_is_resizing (MetaGrabOp op);
gboolean meta_grab_op_is_mouse    (MetaGrabOp op);
gboolean meta_grab_op_is_keyboard (MetaGrabOp op);

void meta_display_queue_autoraise_callback  (MetaDisplay *display,
                                             MetaWindow  *window);
void meta_display_remove_autoraise_callback (MetaDisplay *display);

void meta_display_overlay_key_activate (MetaDisplay *display);
void meta_display_accelerator_activate (MetaDisplay           *display,
                                        guint                  action,
                                        const ClutterKeyEvent *event);
gboolean meta_display_modifiers_accelerator_activate (MetaDisplay *display);

void meta_display_update_focus_window (MetaDisplay *display,
                                       MetaWindow  *window);

void meta_display_sanity_check_timestamps (MetaDisplay *display,
                                           guint32      timestamp);

void meta_display_remove_pending_pings_for_window (MetaDisplay *display,
                                                   MetaWindow  *window);

MetaGestureTracker * meta_display_get_gesture_tracker (MetaDisplay *display);

gboolean meta_display_show_restart_message (MetaDisplay *display,
                                            const char  *message);
gboolean meta_display_request_restart      (MetaDisplay *display);

gboolean meta_display_show_resize_popup (MetaDisplay  *display,
                                         gboolean      show,
                                         MtkRectangle *rect,
                                         int           display_w,
                                         int           display_h);

void meta_set_is_restart (gboolean whether);

void meta_display_cancel_touch (MetaDisplay *display);

gboolean meta_display_windows_are_interactable (MetaDisplay *display);

void meta_display_queue_focus (MetaDisplay *display,
                               MetaWindow  *window);

void meta_display_show_tablet_mapping_notification (MetaDisplay        *display,
                                                    ClutterInputDevice *pad,
                                                    const gchar        *pretty_name);

void meta_display_notify_pad_group_switch (MetaDisplay        *display,
                                           ClutterInputDevice *pad,
                                           const gchar        *pretty_name,
                                           guint               n_group,
                                           guint               n_mode,
                                           guint               n_modes);

void meta_display_restacked (MetaDisplay *display);

gboolean meta_display_apply_startup_properties (MetaDisplay *display,
                                                MetaWindow  *window);

void meta_display_queue_workarea_recalc  (MetaDisplay *display);
void meta_display_queue_check_fullscreen (MetaDisplay *display);

MetaWindow *meta_display_get_window_from_id (MetaDisplay *display,
                                             uint64_t     window_id);
uint64_t    meta_display_generate_window_id (MetaDisplay *display);

void meta_display_init_x11 (MetaDisplay         *display,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data);
gboolean meta_display_init_x11_finish (MetaDisplay   *display,
                                       GAsyncResult  *result,
                                       GError       **error);

void     meta_display_shutdown_x11 (MetaDisplay  *display);

void meta_display_queue_window (MetaDisplay   *display,
                                MetaWindow    *window,
                                MetaQueueType  queue_types);

void meta_display_unqueue_window (MetaDisplay   *display,
                                  MetaWindow    *window,
                                  MetaQueueType  queue_types);

void meta_display_flush_queued_window (MetaDisplay   *display,
                                       MetaWindow    *window,
                                       MetaQueueType  queue_types);

gboolean meta_display_process_captured_input (MetaDisplay        *display,
                                              const ClutterEvent *event);

void meta_display_cancel_input_capture (MetaDisplay *display);

void meta_display_handle_window_enter (MetaDisplay *display,
                                       MetaWindow  *window,
                                       uint32_t     timestamp_ms,
                                       int          root_x,
                                       int          root_y);

void meta_display_handle_window_leave (MetaDisplay *display,
                                       MetaWindow  *window);
