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

#ifndef META_DISPLAY_PRIVATE_H
#define META_DISPLAY_PRIVATE_H

#ifndef PACKAGE
#error "config.h not included"
#endif

#include <glib.h>
#include <X11/Xlib.h>
#include <meta/common.h>
#include <meta/boxes.h>
#include <meta/display.h>
#include "keybindings-private.h"
#include "startup-notification-private.h"
#include "meta-gesture-tracker-private.h"
#include <meta/prefs.h>
#include <meta/barrier.h>
#include <clutter/clutter.h>

#ifdef HAVE_STARTUP_NOTIFICATION
#include <libsn/sn.h>
#endif

#include <X11/extensions/sync.h>

typedef struct _MetaStack      MetaStack;
typedef struct _MetaUISlave    MetaUISlave;

typedef struct _MetaGroupPropHooks  MetaGroupPropHooks;
typedef struct _MetaWindowPropHooks MetaWindowPropHooks;

typedef struct MetaEdgeResistanceData MetaEdgeResistanceData;

typedef enum {
  META_LIST_DEFAULT                   = 0,      /* normal windows */
  META_LIST_INCLUDE_OVERRIDE_REDIRECT = 1 << 0, /* normal and O-R */
  META_LIST_SORTED                    = 1 << 1, /* sort list by mru */
} MetaListWindowsFlags;

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

/* This is basically a bogus number, just has to be large enough
 * to handle the expected case of the alt+tab operation, where
 * we want to ignore serials from UnmapNotify on the tab popup,
 * and the LeaveNotify/EnterNotify from the pointer ungrab. It
 * also has to be big enough to hold ignored serials from the point
 * where we reshape the stage to the point where we get events back.
 */
#define N_IGNORED_CROSSING_SERIALS  10

typedef enum {
  META_TILE_NONE,
  META_TILE_LEFT,
  META_TILE_RIGHT,
  META_TILE_MAXIMIZED
} MetaTileMode;

typedef enum {
  /* Normal interaction where you're interacting with windows.
   * Events go to windows normally. */
  META_EVENT_ROUTE_NORMAL,

  /* In a window operation like moving or resizing. All events
   * goes to MetaWindow, but not to the actual client window. */
  META_EVENT_ROUTE_WINDOW_OP,

  /* In a compositor grab operation. All events go to the
   * compositor plugin. */
  META_EVENT_ROUTE_COMPOSITOR_GRAB,

  /* A Wayland application has a popup open. All events go to
   * the Wayland application. */
  META_EVENT_ROUTE_WAYLAND_POPUP,

  /* The user is clicking on a window button. */
  META_EVENT_ROUTE_FRAME_BUTTON,
} MetaEventRoute;

typedef gboolean (*MetaAlarmFilter) (MetaDisplay           *display,
                                     XSyncAlarmNotifyEvent *event,
                                     gpointer               data);

struct _MetaDisplay
{
  GObject parent_instance;

  MetaX11Display *x11_display;

  int clutter_event_filter;

  Window leader_window;
  Window timestamp_pinging_window;

  /* The window and serial of the most recent FocusIn event. */
  Window server_focus_window;
  gulong server_focus_serial;

  /* Our best guess as to the "currently" focused window (that is, the
   * window that we expect will be focused at the point when the X
   * server processes our next request), and the serial of the request
   * or event that caused this.
   */
  MetaWindow *focus_window;
  /* For windows we've focused that don't necessarily have an X window,
   * like the no_focus_window or the stage X window. */
  Window focus_xwindow;
  gulong focus_serial;

  /* last timestamp passed to XSetInputFocus */
  guint32 last_focus_time;

  /* last user interaction time in any app */
  guint32 last_user_time;

  /* whether we're using mousenav (only relevant for sloppy&mouse focus modes;
   * !mouse_mode means "keynav mode")
   */
  guint mouse_mode : 1;

  /* Helper var used when focus_new_windows setting is 'strict'; only
   * relevant in 'strict' mode and if the focus window is a terminal.
   * In that case, we don't allow new windows to take focus away from
   * a terminal, but if the user explicitly did something that should
   * allow a different window to gain focus (e.g. global keybinding or
   * clicking on a dock), then we will allow the transfer.
   */
  guint allow_terminal_deactivation : 1;

  /* If true, server->focus_serial refers to us changing the focus; in
   * this case, we can ignore focus events that have exactly focus_serial,
   * since we take care to make another request immediately afterwards.
   * But if focus is being changed by another client, we have to accept
   * multiple events with the same serial.
   */
  guint focused_by_us : 1;

  /*< private-ish >*/
  MetaScreen *screen;
  GHashTable *xids;
  GHashTable *stamps;
  GHashTable *wayland_windows;

  /* serials of leave/unmap events that may
   * correspond to an enter event we should
   * ignore
   */
  unsigned long ignored_crossing_serials[N_IGNORED_CROSSING_SERIALS];

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

  /* Event routing */
  MetaEventRoute event_route;

  /* current window operation */
  MetaGrabOp  grab_op;
  MetaWindow *grab_window;
  int         grab_button;
  int         grab_anchor_root_x;
  int         grab_anchor_root_y;
  MetaRectangle grab_anchor_window_pos;
  MetaTileMode  grab_tile_mode;
  int           grab_tile_monitor_number;
  int         grab_latest_motion_x;
  int         grab_latest_motion_y;
  guint       grab_have_pointer : 1;
  guint       grab_have_keyboard : 1;
  guint       grab_frame_action : 1;
  MetaRectangle grab_initial_window_pos;
  int         grab_initial_x, grab_initial_y;  /* These are only relevant for */
  gboolean    grab_threshold_movement_reached; /* raise_on_click == FALSE.    */
  GTimeVal    grab_last_moveresize_time;
  MetaEdgeResistanceData *grab_edge_resistance_data;
  unsigned int grab_last_user_action_was_snap;

  /* we use property updates as sentinels for certain window focus events
   * to avoid some race conditions on EnterNotify events
   */
  int         sentinel_counter;

  int         xkb_base_event_type;
  guint32     last_bell_time;
  int	      grab_resize_timeout_id;

  MetaKeyBindingManager key_binding_manager;

  /* Monitor cache */
  unsigned int monitor_cache_invalidated : 1;

  /* Opening the display */
  unsigned int display_opening : 1;

  /* Closing down the display */
  int closing;

  /* Managed by group.c */
  GHashTable *groups_by_leader;

  /* Managed by window-props.c */
  MetaWindowPropHooks *prop_hooks_table;
  GHashTable *prop_hooks;
  int n_prop_hooks;

  /* Managed by group-props.c */
  MetaGroupPropHooks *group_prop_hooks;

  /* Managed by compositor.c */
  MetaCompositor *compositor;

  MetaGestureTracker *gesture_tracker;
  ClutterEventSequence *pointer_emulating_sequence;

  MetaAlarmFilter alarm_filter;
  gpointer alarm_filter_data;

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

  ClutterActor *current_pad_osd;

  MetaStartupNotification *startup_notification;

  int xsync_event_base;
  int xsync_error_base;
  int shape_event_base;
  int shape_error_base;
  unsigned int have_xsync : 1;
#define META_DISPLAY_HAS_XSYNC(display) ((display)->have_xsync)
  unsigned int have_shape : 1;
#define META_DISPLAY_HAS_SHAPE(display) ((display)->have_shape)
  unsigned int have_composite : 1;
  unsigned int have_damage : 1;
#define META_DISPLAY_HAS_COMPOSITE(display) ((display)->have_composite)
#define META_DISPLAY_HAS_DAMAGE(display) ((display)->have_damage)
#ifdef HAVE_XI23
  gboolean have_xinput_23 : 1;
#define META_DISPLAY_HAS_XINPUT_23(display) ((display)->have_xinput_23)
#else
#define META_DISPLAY_HAS_XINPUT_23(display) FALSE
#endif /* HAVE_XI23 */
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

gboolean      meta_display_open                (void);
void          meta_display_close               (MetaDisplay *display,
                                                guint32      timestamp);

void          meta_display_unmanage_windows_for_screen (MetaDisplay *display,
                                                        MetaScreen  *screen,
                                                        guint32      timestamp);

/* Utility function to compare the stacking of two windows */
int           meta_display_stack_cmp           (const void *a,
                                                const void *b);

/* A given MetaWindow may have various X windows that "belong"
 * to it, such as the frame window.
 */
MetaWindow* meta_display_lookup_x_window     (MetaDisplay *display,
                                              Window       xwindow);
void        meta_display_register_x_window   (MetaDisplay *display,
                                              Window      *xwindowp,
                                              MetaWindow  *window);
void        meta_display_unregister_x_window (MetaDisplay *display,
                                              Window       xwindow);

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

MetaWindow* meta_display_lookup_sync_alarm     (MetaDisplay *display,
                                                XSyncAlarm   alarm);
void        meta_display_register_sync_alarm   (MetaDisplay *display,
                                                XSyncAlarm  *alarmp,
                                                MetaWindow  *window);
void        meta_display_unregister_sync_alarm (MetaDisplay *display,
                                                XSyncAlarm   alarm);

void        meta_display_notify_window_created (MetaDisplay  *display,
                                                MetaWindow   *window);

GSList*     meta_display_list_windows        (MetaDisplay          *display,
                                              MetaListWindowsFlags  flags);

MetaDisplay* meta_display_for_x_display  (Display     *xdisplay);
MetaDisplay* meta_get_display            (void);

void     meta_display_update_cursor (MetaDisplay *display);

void    meta_display_check_threshold_reached (MetaDisplay *display,
                                              int          x,
                                              int          y);
void     meta_display_grab_window_buttons    (MetaDisplay *display,
                                              Window       xwindow);
void     meta_display_ungrab_window_buttons  (MetaDisplay *display,
                                              Window       xwindow);

void meta_display_grab_focus_window_button   (MetaDisplay *display,
                                              MetaWindow  *window);
void meta_display_ungrab_focus_window_button (MetaDisplay *display,
                                              MetaWindow  *window);

/* Next function is defined in edge-resistance.c */
void meta_display_cleanup_edges              (MetaDisplay *display);

/* make a request to ensure the event serial has changed */
void     meta_display_increment_event_serial (MetaDisplay *display);

void     meta_display_update_active_window_hint (MetaDisplay *display);

/* utility goo */
const char* meta_event_mode_to_string   (int m);
const char* meta_event_detail_to_string (int d);

void meta_display_queue_retheme_all_windows (MetaDisplay *display);
void meta_display_retheme_all (void);

void meta_display_ping_window      (MetaWindow  *window,
                                    guint32      serial);
void meta_display_pong_for_serial  (MetaDisplay *display,
                                    guint32      serial);

int meta_resize_gravity_from_grab_op (MetaGrabOp op);

gboolean meta_grab_op_is_moving   (MetaGrabOp op);
gboolean meta_grab_op_is_resizing (MetaGrabOp op);
gboolean meta_grab_op_is_mouse    (MetaGrabOp op);
gboolean meta_grab_op_is_keyboard (MetaGrabOp op);

void meta_display_increment_focus_sentinel (MetaDisplay *display);
void meta_display_decrement_focus_sentinel (MetaDisplay *display);
gboolean meta_display_focus_sentinel_clear (MetaDisplay *display);

void meta_display_queue_autoraise_callback  (MetaDisplay *display,
                                             MetaWindow  *window);
void meta_display_remove_autoraise_callback (MetaDisplay *display);

void meta_display_overlay_key_activate (MetaDisplay *display);
void meta_display_accelerator_activate (MetaDisplay     *display,
                                        guint            action,
                                        ClutterKeyEvent *event);
gboolean meta_display_modifiers_accelerator_activate (MetaDisplay *display);

#ifdef HAVE_XI23
gboolean meta_display_process_barrier_xevent (MetaDisplay *display,
                                              XIEvent     *event);
#endif /* HAVE_XI23 */

void meta_display_set_input_focus_xwindow (MetaDisplay *display,
                                           MetaScreen  *screen,
                                           Window       window,
                                           guint32      timestamp);

void meta_display_sync_wayland_input_focus (MetaDisplay *display);
void meta_display_update_focus_window (MetaDisplay *display,
                                       MetaWindow  *window,
                                       Window       xwindow,
                                       gulong       serial,
                                       gboolean     focused_by_us);

void meta_display_sanity_check_timestamps (MetaDisplay *display,
                                           guint32      timestamp);
gboolean meta_display_timestamp_too_old (MetaDisplay *display,
                                         guint32     *timestamp);

void meta_display_remove_pending_pings_for_window (MetaDisplay *display,
                                                   MetaWindow  *window);

MetaGestureTracker * meta_display_get_gesture_tracker (MetaDisplay *display);

gboolean meta_display_show_restart_message (MetaDisplay *display,
                                            const char  *message);
gboolean meta_display_request_restart      (MetaDisplay *display);

gboolean meta_display_show_resize_popup (MetaDisplay *display,
                                         gboolean show,
                                         MetaRectangle *rect,
                                         int display_w,
                                         int display_h);

void meta_restart_init (void);
void meta_restart_finish (void);

void meta_display_cancel_touch (MetaDisplay *display);

gboolean meta_display_windows_are_interactable (MetaDisplay *display);

void meta_display_set_alarm_filter (MetaDisplay    *display,
                                    MetaAlarmFilter filter,
                                    gpointer        data);

void meta_display_show_tablet_mapping_notification (MetaDisplay        *display,
                                                    ClutterInputDevice *pad,
                                                    const gchar        *pretty_name);

void meta_display_notify_pad_group_switch (MetaDisplay        *display,
                                           ClutterInputDevice *pad,
                                           const gchar        *pretty_name,
                                           guint               n_group,
                                           guint               n_mode,
                                           guint               n_modes);

#endif
