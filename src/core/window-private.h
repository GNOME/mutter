/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file window-private.h  Windows which Mutter manages
 *
 * Managing X windows.
 * This file contains methods on this class which are available to
 * routines in core but not outside it.  (See window.h for the routines
 * which the rest of the world is allowed to use.)
 */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
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

#include "backends/meta-logical-monitor-private.h"
#include "clutter/clutter.h"
#include "core/meta-window-config-private.h"
#include "core/stack.h"
#include "meta/meta-window-config.h"
#include "meta/compositor.h"
#include "meta/meta-close-dialog.h"
#include "meta/util.h"
#include "meta/window.h"
#include "meta/meta-window-config.h"
#include "wayland/meta-wayland-types.h"

typedef struct _MetaWindowQueue MetaWindowQueue;

#define META_WINDOW_TITLEBAR_HEIGHT 50

typedef enum
{
  META_CLIENT_TYPE_UNKNOWN = 0,
  META_CLIENT_TYPE_APPLICATION = 1,
  META_CLIENT_TYPE_PAGER = 2,
  META_CLIENT_TYPE_MAX_RECOGNIZED = 2
} MetaClientType;

#define META_N_QUEUE_TYPES 2

typedef enum
{
  META_MOVE_RESIZE_CONFIGURE_REQUEST = 1 << 0,
  META_MOVE_RESIZE_USER_ACTION = 1 << 1,
  META_MOVE_RESIZE_MOVE_ACTION = 1 << 2,
  META_MOVE_RESIZE_RESIZE_ACTION = 1 << 3,
  META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE = 1 << 4,
  META_MOVE_RESIZE_STATE_CHANGED = 1 << 5,
  META_MOVE_RESIZE_UNMAXIMIZE = 1 << 6,
  META_MOVE_RESIZE_UNFULLSCREEN = 1 << 7,
  META_MOVE_RESIZE_FORCE_MOVE = 1 << 8,
  META_MOVE_RESIZE_WAYLAND_STATE_CHANGED = 1 << 9,
  META_MOVE_RESIZE_FORCE_UPDATE_MONITOR = 1 << 10,
  META_MOVE_RESIZE_PLACEMENT_CHANGED = 1 << 11,
  META_MOVE_RESIZE_WAYLAND_CLIENT_RESIZE = 1 << 12,
  META_MOVE_RESIZE_CONSTRAIN = 1 << 13,
  META_MOVE_RESIZE_RECT_INVALID = 1 << 14,
  META_MOVE_RESIZE_WAYLAND_FORCE_CONFIGURE = 1 << 15,
} MetaMoveResizeFlags;

typedef enum _MetaPlaceFlag
{
  META_PLACE_FLAG_NONE = 0,
  META_PLACE_FLAG_FORCE_MOVE = 1 << 0,
  META_PLACE_FLAG_DENIED_FOCUS_AND_NOT_TRANSIENT = 1 << 1,
  META_PLACE_FLAG_CALCULATE = 1 << 2,
} MetaPlaceFlag;

typedef enum
{
  META_MOVE_RESIZE_RESULT_MOVED = 1 << 0,
  META_MOVE_RESIZE_RESULT_RESIZED = 1 << 1,
  META_MOVE_RESIZE_RESULT_STATE_CHANGED = 1 << 3,
  META_MOVE_RESIZE_RESULT_UPDATE_UNCONSTRAINED = 1 << 4,
} MetaMoveResizeResultFlags;

typedef enum
{
  META_PLACEMENT_GRAVITY_NONE   = 0,
  META_PLACEMENT_GRAVITY_TOP    = 1 << 0,
  META_PLACEMENT_GRAVITY_BOTTOM = 1 << 1,
  META_PLACEMENT_GRAVITY_LEFT   = 1 << 2,
  META_PLACEMENT_GRAVITY_RIGHT  = 1 << 3,
} MetaPlacementGravity;

typedef enum
{
  META_PLACEMENT_ANCHOR_NONE   = 0,
  META_PLACEMENT_ANCHOR_TOP    = 1 << 0,
  META_PLACEMENT_ANCHOR_BOTTOM = 1 << 1,
  META_PLACEMENT_ANCHOR_LEFT   = 1 << 2,
  META_PLACEMENT_ANCHOR_RIGHT  = 1 << 3,
} MetaPlacementAnchor;

typedef enum
{
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_NONE     = 0,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_X  = 1 << 0,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_SLIDE_Y  = 1 << 1,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_X   = 1 << 2,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_FLIP_Y   = 1 << 3,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_RESIZE_X = 1 << 4,
  META_PLACEMENT_CONSTRAINT_ADJUSTMENT_RESIZE_Y = 1 << 5,
} MetaPlacementConstraintAdjustment;

typedef enum _MetaWindowUpdateMonitorFlags
{
  META_WINDOW_UPDATE_MONITOR_FLAGS_NONE = 0,
  META_WINDOW_UPDATE_MONITOR_FLAGS_USER_OP = 1 << 0,
  META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE = 1 << 1,
} MetaWindowUpdateMonitorFlags;

typedef enum _MetaWindowSuspendState
{
  META_WINDOW_SUSPEND_STATE_ACTIVE = 1,
  META_WINDOW_SUSPEND_STATE_HIDDEN,
  META_WINDOW_SUSPEND_STATE_SUSPENDED,
} MetaWindowSuspendState;

typedef enum _MetaWindowApplyFlags
{
  META_WINDOW_APPLY_FLAG_NONE = 0,
  META_WINDOW_APPLY_FLAG_ALWAYS_MOVE_RESIZE = 1 << 0,
} MetaWindowApplyFlags;

typedef struct _MetaPlacementRule
{
  MtkRectangle anchor_rect;
  MetaPlacementGravity gravity;
  MetaPlacementAnchor anchor;
  MetaPlacementConstraintAdjustment constraint_adjustment;
  int offset_x;
  int offset_y;
  int width;
  int height;

  gboolean is_reactive;

  MtkRectangle parent_rect;
} MetaPlacementRule;

typedef enum _MetaPlacementState
{
  META_PLACEMENT_STATE_UNCONSTRAINED,
  META_PLACEMENT_STATE_CONSTRAINED_PENDING,
  META_PLACEMENT_STATE_CONSTRAINED_CONFIGURED,
  META_PLACEMENT_STATE_CONSTRAINED_FINISHED,
  META_PLACEMENT_STATE_INVALIDATED,
} MetaPlacementState;

typedef enum
{
  META_EDGE_CONSTRAINT_NONE    = 0,
  META_EDGE_CONSTRAINT_WINDOW  = 1,
  META_EDGE_CONSTRAINT_MONITOR = 2,
} MetaEdgeConstraint;

typedef enum
{
  META_EDGE_RESISTANCE_DEFAULT     = 0,
  META_EDGE_RESISTANCE_SNAP        = 1 << 0,
  META_EDGE_RESISTANCE_KEYBOARD_OP = 1 << 1,
  META_EDGE_RESISTANCE_WINDOWS     = 1 << 2,
} MetaEdgeResistanceFlags;

typedef enum
{
  /* Equivalent to USPosition */
  META_SIZE_HINTS_USER_POSITION = (1L << 0),
  /* Equivalent to USSize */
  META_SIZE_HINTS_USER_SIZE = (1L << 1),
  /* Equivalent to PPosition */
  META_SIZE_HINTS_PROGRAM_POSITION = (1L << 2),
  /* Equivalent to PSize */
  META_SIZE_HINTS_PROGRAM_SIZE = (1L << 3),
  /* Equivalent to PMinSize */
  META_SIZE_HINTS_PROGRAM_MIN_SIZE = (1L << 4),
  /* Equivalent to PMaxSize */
  META_SIZE_HINTS_PROGRAM_MAX_SIZE = (1L << 5),
  /* Equivalent to PResizeInc */
  META_SIZE_HINTS_PROGRAM_RESIZE_INCREMENTS = (1L << 6),
  /* Equivalent to PAspect */
  META_SIZE_HINTS_PROGRAM_ASPECT = (1L << 7),
  /* Equivalent to PBaseSize */
  META_SIZE_HINTS_PROGRAM_BASE_SIZE = (1L << 8),
  /* Equivalent to PWinGravity */
  META_SIZE_HINTS_PROGRAM_WIN_GRAVITY = (1L << 9),
} MetaSizeHintsFlags;

/* Windows that unmaximize to a size bigger than that fraction of the workarea
 * will be scaled down to that size (while maintaining aspect ratio).
 * Windows that cover an area greater then this size are automatically
 * maximized when initially placed.
 */
#define MAX_UNMAXIMIZED_WINDOW_AREA .8

/**
 * A copy of XSizeHints that is meant to stay ABI compatible
 * with XSizeHints for x11 code paths usages
 */
typedef struct _MetaSizeHints {
  long flags; /* MetaSizeHintsFlags but kept as long to be able to cast between XSizeHints and MetaSizeHints */
  int x, y;
  int width, height;
  int min_width, min_height;
  int max_width, max_height;
  int width_inc, height_inc;
  struct {
    int x;  /* numerator */
    int y;  /* denominator */
  } min_aspect, max_aspect;
  int base_width, base_height;
  int win_gravity;
} MetaSizeHints;

struct _MetaWindow
{
  GObject parent_instance;

  MetaDisplay *display;
  uint64_t id;
  guint64 stamp;
  MetaLogicalMonitor *monitor;
  MetaLogicalMonitor *highest_scale_monitor;
  MetaWorkspace *workspace;
  MetaWindowClientType client_type;
  int depth;
  char *desc; /* used in debug spew */
  char *title;

  MetaWindowType type;

  /* NOTE these five are not in UTF-8, we just treat them as random
   * binary data
   */
  char *res_class;
  char *res_name;
  char *role;

  char *tag;

  char *startup_id;
  char *mutter_hints;
  char *sandboxed_app_id;
  char *gtk_theme_variant;
  char *gtk_application_id;
  char *gtk_unique_bus_name;
  char *gtk_application_object_path;
  char *gtk_window_object_path;
  char *gtk_app_menu_object_path;
  char *gtk_menubar_object_path;

  MetaWindow *transient_for;

  /* Initial workspace property */
  int initial_workspace;

  /* Initial timestamp property */
  guint32 initial_timestamp;

  struct {
    MetaEdgeConstraint top;
    MetaEdgeConstraint right;
    MetaEdgeConstraint bottom;
    MetaEdgeConstraint left;
  } edge_constraints;

  MetaLogicalMonitorId *preferred_logical_monitor;

  /* Area to cover when in fullscreen mode.  If _NET_WM_FULLSCREEN_MONITORS has
   * been overridden (via a client message), the window will cover the union of
   * these monitors.  If not, this is the single monitor which the window's
   * origin is on. */
  struct {
    MetaLogicalMonitor *top;
    MetaLogicalMonitor *bottom;
    MetaLogicalMonitor *left;
    MetaLogicalMonitor *right;
  } fullscreen_monitors;

  /* _NET_WM_WINDOW_OPACITY rescaled to 0xFF */
  guint8 opacity;

  /* Note: can be NULL */
  GSList *struts;

  /* Number of UnmapNotify that are caused by us, if
   * we get UnmapNotify with none pending then the client
   * is withdrawing the window.
   */
  int unmaps_pending;

  /* Number of XReparentWindow requests that we have queued.
   */
  int reparents_pending;

  /* See docs for meta_window_get_stable_sequence() */
  guint32 stable_sequence;

  /* set to the most recent user-interaction event timestamp that we
     know about for this window */
  guint32 net_wm_user_time;

  MetaFrameBorder custom_frame_extents;

  /* The rectangles here are in "frame rect" coordinates. See the
   * comment at the top of meta_window_move_resize_internal() for more
   * information. */

  /* The current configuration of the window. */
  MetaWindowConfig *config;

  /* The geometry to restore when we unmaximize. */
  MtkRectangle saved_rect;

  /* The geometry to restore when we unfullscreen. */
  MtkRectangle saved_rect_fullscreen;

  /* This is the geometry the window will have if no constraints have
   * applied. We use this whenever we are moving implicitly (for example,
   * if we move to avoid a panel, we can snap back to this position if
   * the panel moves again).
   */
  MtkRectangle unconstrained_rect;

  /* The rectangle of the "server-side" geometry of the buffer,
   * in root coordinates.
   *
   * For X11 windows, this matches XGetGeometry of the toplevel.
   *
   * For Wayland windows, the position matches the position of the
   * surface associated with shell surface (xdg_surface, etc.)
   * The size matches the size surface size as displayed in the stage.
   */
  MtkRectangle buffer_rect;

  /* Cached net_wm_icon_geometry */
  MtkRectangle icon_geometry;

  /* x/y/w/h here get filled with ConfigureRequest values */
  MetaSizeHints size_hints;

  /* Managed by stack.c */
  MetaStackLayer layer;
  int stack_position; /* see comment in stack.h */

  /* Managed by delete.c */
  MetaCloseDialog *close_dialog;

  GObject *compositor_private;

  /* Focused window that is (directly or indirectly) attached to this one */
  MetaWindow *attached_focus_window;

  struct {
    MetaPlacementRule *rule;
    MetaPlacementState state;

    struct {
      int x;
      int y;
      int rel_x;
      int rel_y;
    } pending;

    struct {
      int rel_x;
      int rel_y;
    } current;
  } placement;

  guint close_dialog_timeout_id;

  pid_t client_pid;

  gboolean has_valid_cgroup;
  GFile *cgroup_path;

  unsigned int events_during_ping;

  /* Whether this is an override redirect window or not */
  guint override_redirect : 1;

  /* Whether we have to minimize after placement */
  guint minimize_after_placement : 1;

  /* The last "full" maximized/unmaximized state. We need to keep track of
   * that to toggle between normal/tiled or maximized/tiled states. */
  guint saved_maximize : 1;

  /* Whether the window is marked as urgent */
  guint urgent : 1;

  /* Whether we're trying to constrain the window to be fully onscreen */
  guint require_fully_onscreen : 1;

  /* Whether we're trying to constrain the window to be on a single monitor */
  guint require_on_single_monitor : 1;

  /* Whether we're trying to constrain the window's titlebar to be onscreen */
  guint require_titlebar_visible : 1;

  /* Whether we're sticky in the multi-workspace sense
   * (vs. the not-scroll-with-viewport sense, we don't
   * have no stupid viewports)
   */
  guint on_all_workspaces : 1;

  /* This is true if the client requested sticky, and implies on_all_workspaces == TRUE,
   * however on_all_workspaces can be set TRUE for other internal reasons too, such as
   * being override_redirect or being on the non-primary monitor. */
  guint on_all_workspaces_requested : 1;

  /* Minimize is the state controlled by the minimize button */
  guint minimized : 1;

  /* Whether the window is mapped; actual server-side state
   * see also unmaps_pending
   */
  guint mapped : 1;

  /* Whether window has been hidden from view by lowering it to the bottom
   * of window stack.
   */
  guint hidden : 1;

  /* Whether the compositor thinks the window is visible.
   * This should match up with calls to meta_compositor_show_window /
   * meta_compositor_hide_window.
   */
  guint visible_to_compositor : 1;

  /* Whether the compositor knows about the window.
   * This should match up with calls to meta_compositor_add_window /
   * meta_compositor_remove_window.
   */
  guint known_to_compositor : 1;

  /* When we next show or hide the window, what effect we should
   * tell the compositor to perform.
   */
  guint pending_compositor_effect : 4; /* MetaCompEffect */

  /* Iconic is the state in WM_STATE; happens for workspaces/shading
   * in addition to minimize
   */
  guint iconic : 1;
  /* initially_iconic is the WM_HINTS setting when we first manage
   * the window. It's taken to mean initially minimized.
   */
  guint initially_iconic : 1;

  /* whether an initial workspace was explicitly set */
  guint initial_workspace_set : 1;

  /* whether an initial timestamp was explicitly set */
  guint initial_timestamp_set : 1;

  /* whether net_wm_user_time has been set yet */
  guint net_wm_user_time_set : 1;

  /* whether net_wm_icon_geometry has been set */
  guint icon_geometry_set : 1;

  /* Globally active / No input */
  guint input : 1;

  /* MWM hints about features of window */
  guint mwm_decorated : 1;
  guint mwm_border_only : 1;
  guint mwm_has_close_func : 1;
  guint mwm_has_minimize_func : 1;
  guint mwm_has_maximize_func : 1;
  guint mwm_has_move_func : 1;
  guint mwm_has_resize_func : 1;

  /* Computed features of window */
  guint decorated : 1;
  guint border_only : 1;
  guint always_sticky : 1;
  guint has_close_func : 1;
  guint has_minimize_func : 1;
  guint has_maximize_func : 1;
  guint has_move_func : 1;
  guint has_resize_func : 1;
  guint has_fullscreen_func : 1;

  /* Computed whether to skip taskbar or not */
  guint skip_taskbar : 1;
  guint skip_pager : 1;
  guint skip_from_window_list : 1;

  /* TRUE if client set these */
  guint wm_state_above : 1;
  guint wm_state_below : 1;

  /* EWHH demands attention flag */
  guint wm_state_demands_attention : 1;

  /* TRUE iff window == window->display->focus_window */
  guint has_focus : 1;

  /* TRUE if window appears focused at the moment */
  guint appears_focused : 1;

  /* Have we placed this window according to the floating window placement
   * algorithm? */
  guint placed : 1;

  /* Have this window been positioned? */
  uint unconstrained_rect_valid : 1;

  /* Has this window not ever been shown yet? */
  guint showing_for_first_time : 1;

  /* Are we in meta_window_unmanage()? */
  guint unmanaging : 1;

  /* Are we in meta_window_new()? */
  guint constructing : 1;

  /* Set if the reason for unmanaging the window is that
   * it was withdrawn
   */
  guint withdrawn : 1;

  /* if TRUE, window is attached to its parent */
  guint attached : 1;

  /* whether or not the window is from a program running on another machine */
  guint is_remote : 1;

  /* whether focus should be restored on map */
  guint restore_focus_on_map : 1;

  /* Whether the window is alive */
  guint is_alive : 1;

  guint in_workspace_change : 1;
};

struct _MetaWindowClass
{
  GObjectClass parent_class;

  void (*manage)                 (MetaWindow *window);
  void (*unmanage)               (MetaWindow *window);
  void (*ping)                   (MetaWindow *window,
                                  guint32     serial);
  void (*delete)                 (MetaWindow *window,
                                  guint32     timestamp);
  void (*kill)                   (MetaWindow *window);
  void (*focus)                  (MetaWindow *window,
                                  guint32     timestamp);
  void (*grab_op_began)          (MetaWindow *window,
                                  MetaGrabOp  op);
  void (*grab_op_ended)          (MetaWindow *window,
                                  MetaGrabOp  op);
  void (*current_workspace_changed) (MetaWindow *window);
  void (*move_resize_internal)   (MetaWindow                *window,
                                  MtkRectangle               unconstrained_rect,
                                  MtkRectangle               constrained_rect,
                                  MtkRectangle               temporary_rect,
                                  int                        rel_x,
                                  int                        rel_y,
                                  MetaMoveResizeFlags        flags,
                                  MetaMoveResizeResultFlags *result);
  gboolean (*update_struts)      (MetaWindow *window);
  void (*get_default_skip_hints) (MetaWindow *window,
                                  gboolean   *skip_taskbar_out,
                                  gboolean   *skip_pager_out);

  pid_t (*get_client_pid)        (MetaWindow *window);
  void (*update_main_monitor)    (MetaWindow                   *window,
                                  MetaWindowUpdateMonitorFlags  flags);
  void (*main_monitor_changed)   (MetaWindow *window,
                                  const MetaLogicalMonitor *old);
  void (*adjust_fullscreen_monitor_rect) (MetaWindow    *window,
                                          MtkRectangle  *monitor_rect);
  void (*force_restore_shortcuts) (MetaWindow         *window,
                                   ClutterInputDevice *source);
  gboolean (*shortcuts_inhibited) (MetaWindow         *window,
                                   ClutterInputDevice *source);
  gboolean (*is_focusable)        (MetaWindow *window);
  gboolean (*is_stackable)        (MetaWindow *window);
  gboolean (*can_ping)            (MetaWindow *window);
  gboolean (*are_updates_frozen)  (MetaWindow *window);
  gboolean (*is_focus_async)      (MetaWindow *window);

  MetaStackLayer (*calculate_layer) (MetaWindow *window);

#ifdef HAVE_WAYLAND
  MetaWaylandSurface * (*get_wayland_surface) (MetaWindow *window);
#endif

  gboolean (*set_transient_for) (MetaWindow *window,
                                 MetaWindow *parent);

  void (*stage_to_protocol) (MetaWindow          *window,
                             int                  stage_x,
                             int                  stage_y,
                             int                 *protocol_x,
                             int                 *protocol_y,
                             MtkRoundingStrategy  rounding_strategy);
  void (*protocol_to_stage) (MetaWindow          *window,
                             int                  protocol_x,
                             int                  protocol_y,
                             int                 *stage_x,
                             int                 *stage_y,
                             MtkRoundingStrategy  rounding_strategy);

  MetaGravity (* get_gravity) (MetaWindow *window);

  void (* save_rect) (MetaWindow *window);
};

void        meta_window_unmanage           (MetaWindow  *window,
                                            guint32      timestamp);
void        meta_window_queue              (MetaWindow  *window,
                                            MetaQueueType queue_types);
META_EXPORT_TEST
void        meta_window_untile             (MetaWindow        *window);

void        meta_window_tile_internal      (MetaWindow        *window,
                                            MetaTileMode       mode,
                                            MtkRectangle      *saved_rect);
META_EXPORT_TEST
void        meta_window_tile               (MetaWindow        *window,
                                            MetaTileMode       mode);
void        meta_window_restore_tile       (MetaWindow        *window,
                                            MetaTileMode       mode,
                                            int                width,
                                            int                height);
void        meta_window_maximize_internal  (MetaWindow        *window,
                                            MetaMaximizeFlags  directions,
                                            MtkRectangle      *saved_rect);

void        meta_window_queue_auto_maximize (MetaWindow       *window);

void        meta_window_make_fullscreen_internal (MetaWindow    *window);
void        meta_window_update_fullscreen_monitors (MetaWindow         *window,
                                                    MetaLogicalMonitor *top,
                                                    MetaLogicalMonitor *bottom,
                                                    MetaLogicalMonitor *left,
                                                    MetaLogicalMonitor *right);

gboolean    meta_window_has_fullscreen_monitors (MetaWindow *window);

void        meta_window_adjust_fullscreen_monitor_rect (MetaWindow    *window,
                                                        MtkRectangle  *monitor_rect);

gboolean    meta_window_should_be_showing_on_workspace (MetaWindow    *window,
                                                        MetaWorkspace *workspace);

META_EXPORT_TEST
gboolean    meta_window_should_be_showing   (MetaWindow  *window);

META_EXPORT_TEST
gboolean    meta_window_should_show (MetaWindow  *window);

void        meta_window_update_struts      (MetaWindow  *window);

gboolean    meta_window_geometry_contains_rect (MetaWindow   *window,
                                                MtkRectangle *rect);

void        meta_window_update_appears_focused (MetaWindow *window);

void     meta_window_set_focused_internal (MetaWindow *window,
                                           gboolean    focused);

gboolean meta_window_is_focusable (MetaWindow *window);

gboolean meta_window_can_ping (MetaWindow *window);

MetaStackLayer meta_window_calculate_layer (MetaWindow *window);

#ifdef HAVE_WAYLAND
META_EXPORT_TEST
MetaWaylandSurface * meta_window_get_wayland_surface (MetaWindow *window);
#endif

void     meta_window_current_workspace_changed (MetaWindow *window);

void meta_window_show_menu (MetaWindow         *window,
                            MetaWindowMenuType  menu,
                            int                 x,
                            int                 y);

void meta_window_get_work_area_for_logical_monitor (MetaWindow         *window,
                                                    MetaLogicalMonitor *logical_monitor,
                                                    MtkRectangle       *area);

int meta_window_get_current_tile_monitor_number (MetaWindow *window);
void meta_window_get_tile_area                  (MetaWindow    *window,
                                                 MetaTileMode   mode,
                                                 MtkRectangle  *tile_area);

void meta_window_free_delete_dialog (MetaWindow *window);

MetaStackLayer meta_window_get_default_layer (MetaWindow *window);
void meta_window_update_layer (MetaWindow *window);

void meta_window_recalc_features    (MetaWindow *window);

void meta_window_frame_size_changed (MetaWindow *window);

gboolean meta_window_is_in_stack (MetaWindow *window);

void meta_window_stack_just_below (MetaWindow *window,
                                   MetaWindow *below_this_one);

void meta_window_stack_just_above (MetaWindow *window,
                                   MetaWindow *above_this_one);

int meta_window_stack_position_compare (gconstpointer window_a,
                                        gconstpointer window_b);

void meta_window_set_user_time (MetaWindow *window,
                                guint32     timestamp);

void meta_window_update_for_monitors_changed (MetaWindow *window);
void meta_window_on_all_workspaces_changed (MetaWindow *window);

gboolean meta_window_can_tile_side_by_side   (MetaWindow *window,
                                              int         monitor_number);

void meta_window_compute_tile_match (MetaWindow *window);

gboolean meta_window_updates_are_frozen (MetaWindow *window);

META_EXPORT_TEST
void meta_window_set_title                (MetaWindow *window,
                                           const char *title);
void meta_window_set_wm_class             (MetaWindow *window,
                                           const char *wm_class,
                                           const char *wm_instance);
void meta_window_set_gtk_dbus_properties  (MetaWindow *window,
                                           const char *application_id,
                                           const char *unique_bus_name,
                                           const char *appmenu_path,
                                           const char *menubar_path,
                                           const char *application_object_path,
                                           const char *window_object_path);

gboolean meta_window_has_transient_type   (MetaWindow *window);
gboolean meta_window_has_modals           (MetaWindow *window);

void meta_window_set_transient_for        (MetaWindow *window,
                                           MetaWindow *parent);

void meta_window_set_opacity              (MetaWindow *window,
                                           guint8      opacity);

gboolean meta_window_handle_ungrabbed_event (MetaWindow         *window,
                                             const ClutterEvent *event);

void meta_window_get_client_area_rect (MetaWindow   *window,
                                       MtkRectangle *rect);

void meta_window_activate_full (MetaWindow     *window,
                                guint32         timestamp,
                                MetaClientType  source_indication,
                                MetaWorkspace  *workspace);

META_EXPORT_TEST
MetaLogicalMonitor * meta_window_find_monitor_from_frame_rect (MetaWindow *window);

MetaLogicalMonitor * meta_window_find_monitor_from_id (MetaWindow *window);

META_EXPORT_TEST
MetaLogicalMonitor * meta_window_get_main_logical_monitor (MetaWindow *window);
MetaLogicalMonitor * meta_window_get_highest_scale_monitor (MetaWindow *window);
void meta_window_update_monitor (MetaWindow                   *window,
                                 MetaWindowUpdateMonitorFlags  flags);

void meta_window_set_urgent (MetaWindow *window,
                             gboolean    urgent);

void meta_window_move_resize (MetaWindow          *window,
                              MetaMoveResizeFlags  flags,
                              MtkRectangle         frame_rect);

void meta_window_move_resize_internal (MetaWindow          *window,
                                       MetaMoveResizeFlags  flags,
                                       MetaPlaceFlag        place_flags,
                                       MtkRectangle         frame_rect,
                                       MtkRectangle        *result_rect);

void meta_window_grab_op_began (MetaWindow *window, MetaGrabOp op);
void meta_window_grab_op_ended (MetaWindow *window, MetaGrabOp op);

void meta_window_set_alive (MetaWindow *window, gboolean is_alive);
gboolean meta_window_get_alive (MetaWindow *window);

void meta_window_show_close_dialog (MetaWindow *window);
void meta_window_hide_close_dialog (MetaWindow *window);
void meta_window_ensure_close_dialog_timeout (MetaWindow *window);

void meta_window_emit_size_changed (MetaWindow *window);

void meta_window_emit_configure (MetaWindow       *window,
                                 MetaWindowConfig *window_config);

MetaPlacementRule *meta_window_get_placement_rule (MetaWindow *window);

void meta_window_force_placement (MetaWindow    *window,
                                  MetaPlaceFlag  flags);

void meta_window_force_restore_shortcuts (MetaWindow         *window,
                                          ClutterInputDevice *source);

gboolean meta_window_shortcuts_inhibited (MetaWindow         *window,
                                          ClutterInputDevice *source);
gboolean meta_window_is_stackable (MetaWindow *window);
gboolean meta_window_is_focus_async (MetaWindow *window);

GFile *meta_window_get_unit_cgroup (MetaWindow *window);
gboolean meta_window_unit_cgroup_equal (MetaWindow *window1,
                                        MetaWindow *window2);

void meta_window_check_alive_on_event (MetaWindow *window,
                                       uint32_t    timestamp);

void meta_window_update_visibility (MetaWindow  *window);

void meta_window_clear_queued (MetaWindow *window);

void meta_window_idle_move_resize (MetaWindow *window);

gboolean meta_window_calculate_bounds (MetaWindow *window,
                                       int        *bounds_width,
                                       int        *bounds_height);

void meta_window_maybe_apply_size_hints (MetaWindow   *window,
                                         MtkRectangle *target_rect);

void meta_window_inhibit_suspend_state (MetaWindow *window);

void meta_window_uninhibit_suspend_state (MetaWindow *window);

gboolean meta_window_is_suspended (MetaWindow *window);

META_EXPORT_TEST
int meta_get_window_suspend_timeout_s (void);

gboolean
meta_window_should_attach_to_parent (MetaWindow *window);

/**
 * meta_window_set_normal_hints:
 * @window:   The window to set the size hints on.
 * @hints:    Either some size hints, or NULL for default.
 *
 * Sets the size hints for a window.  This happens when a
 * WM_NORMAL_HINTS property is set on a window, but it is public
 * because the size hints are set to defaults when a window is
 * created.  See
 * http://tronche.com/gui/x/icccm/sec-4.html#WM_NORMAL_HINTS
 * for the X details.
 */
void meta_window_set_normal_hints (MetaWindow    *window,
                                   MetaSizeHints *hints);

void meta_window_stage_to_protocol_point (MetaWindow *window,
                                          int         stage_x,
                                          int         stage_y,
                                          int        *protocol_x,
                                          int        *protocol_y);

void meta_window_protocol_to_stage_point (MetaWindow          *window,
                                          int                  protocol_x,
                                          int                  protocol_y,
                                          int                 *stage_x,
                                          int                 *stage_y,
                                          MtkRoundingStrategy  rounding_strategy);

gboolean meta_window_is_tiled_side_by_side (MetaWindow *window);

gboolean meta_window_is_tiled_left (MetaWindow *window);

gboolean meta_window_is_tiled_right (MetaWindow *window);

void meta_window_update_tile_fraction (MetaWindow *window,
                                       int         new_w,
                                       int         new_h);

void meta_window_apply_config (MetaWindow           *window,
                               MetaWindowConfig     *config,
                               MetaWindowApplyFlags  flags);

MetaGravity meta_window_get_gravity (MetaWindow *window);

void meta_window_set_tag (MetaWindow *window,
                          const char *tag);

META_EXPORT_TEST
GPtrArray * meta_window_get_transient_children (MetaWindow *window);
