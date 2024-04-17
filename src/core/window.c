/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington, Anders Carlsson
 * Copyright (C) 2002, 2003 Red Hat, Inc.
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

/**
 * MetaWindow:
 *
 * A display-agnostic abstraction for a window.
 *
 * #MetaWindow is the core abstraction in Mutter of a window. It has the
 * properties you'd expect, such as a title, whether it's fullscreen,
 * has decorations, etc.
 *
 * Since a lot of different kinds of windows exist, each window also a
 * [enum@Meta.WindowType] which denotes which kind of window we're exactly dealing
 * with. For example, one expects slightly different behaviour from a dialog
 * than a "normal" window. The type of a window can be queried with
 * [method@Meta.Window.get_window_type].
 *
 * Common API for windows include:
 *
 * - Minimizing: [method@Meta.Window.minimize] / [method@Meta.Window.unminimize]
 * - Maximizing: [method@Meta.Window.maximize] / [method@Meta.Window.unmaximize]
 * - Fullscreen: [method@Meta.Window.make_fullscreen] / [method@Meta.Window.unmake_fullscreen]
 *               / [method@Meta.Window.is_fullscreen]
 *
 * Each #MetaWindow is part of either one or all [class@Meta.Workspace]s of the
 * desktop. You can activate a window on a certain workspace using
 * [method@Meta.Window.activate_with_workspace], and query on which workspace it is
 * located using [method@Meta.Window.located_on_workspace]. The workspace it is part
 * of can be obtained using [method@Meta.Window.get_workspace].
 *
 * Each display protocol should make a subclass to be compatible with that
 * protocols' specifics, for example #MetaWindowX11 and #MetaWindowWayland.
 * This is independent of the protocol that the client uses, which is modeled
 * using the [enum@Meta.WindowClientType] enum.
 *
 * To integrate within the Clutter scene graph, which deals with the actual
 * rendering, each #MetaWindow will be part of a [class@Meta.WindowActor].
 */

#include "config.h"

#include "core/window-private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "cogl/cogl.h"
#include "compositor/compositor-private.h"
#include "core/boxes-private.h"
#include "core/constraints.h"
#include "core/frame.h"
#include "core/keybindings-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/place.h"
#include "core/stack.h"
#include "core/util-private.h"
#include "core/workspace-private.h"
#include "meta/compositor-mutter.h"
#include "meta/meta-cursor-tracker.h"
#include "meta/meta-enum-types.h"
#include "meta/prefs.h"

#ifdef HAVE_X11_CLIENT
#include "meta/group.h"
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-props.h"
#include "x11/window-x11-private.h"
#include "x11/window-x11.h"
#include "x11/xprops.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-window-wayland.h"
#endif

#ifdef HAVE_XWAYLAND
#include "wayland/meta-window-xwayland.h"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-login.h>
#endif

#include "meta-private-enum-types.h"

/* Windows that unmaximize to a size bigger than that fraction of the workarea
 * will be scaled down to that size (while maintaining aspect ratio).
 * Windows that cover an area greater then this size are automaximized on map.
 */
#define MAX_UNMAXIMIZED_WINDOW_AREA .8

#define SNAP_SECURITY_LABEL_PREFIX "snap."

#define SUSPEND_HIDDEN_TIMEOUT_S 3

/* Each window has a "stamp" which is a non-recycled 64-bit ID. They
 * start after the end of the XID space so that, for stacking
 * we can keep a guint64 that represents one or the other
 */
static guint64 next_window_stamp = G_GUINT64_CONSTANT(0x100000000);

static void     invalidate_work_areas     (MetaWindow     *window);
static void     set_wm_state              (MetaWindow     *window);
static void     set_net_wm_state          (MetaWindow     *window);
static void     meta_window_set_above     (MetaWindow     *window,
                                           gboolean        new_value);

static void     meta_window_constructed   (GObject *object);
static void     meta_window_show          (MetaWindow     *window);
static void     meta_window_hide          (MetaWindow     *window);

static void     meta_window_save_rect         (MetaWindow    *window);

static void     ensure_mru_position_after (MetaWindow *window,
                                           MetaWindow *after_this_one);

static void meta_window_unqueue (MetaWindow    *window,
                                 MetaQueueType  queuebits);

static gboolean should_be_on_all_workspaces (MetaWindow *window);

static void meta_window_flush_calc_showing   (MetaWindow *window);

static gboolean queue_calc_showing_func (MetaWindow *window,
                                         void       *data);

static void meta_window_move_between_rects (MetaWindow          *window,
                                            MetaMoveResizeFlags  move_resize_flags,
                                            const MtkRectangle  *old_area,
                                            const MtkRectangle  *new_area);

static void unmaximize_window_before_freeing (MetaWindow        *window);
static void unminimize_window_and_all_transient_parents (MetaWindow *window);

static void meta_window_propagate_focus_appearance (MetaWindow *window,
                                                    gboolean    focused);
static void set_workspace_state (MetaWindow    *window,
                                 gboolean       on_all_workspaces,
                                 MetaWorkspace *workspace);

static MetaWindow * meta_window_find_tile_match (MetaWindow   *window,
                                                 MetaTileMode  mode);
static void update_edge_constraints (MetaWindow *window);

static void set_hidden_suspended_state (MetaWindow *window);

static void initable_iface_init (GInitableIface *initable_iface);

typedef struct _MetaWindowPrivate
{
  MetaQueueType queued_types;

  MetaWindowSuspendState suspend_state;
  int suspend_state_inhibitors;
  guint suspend_timoeut_id;
} MetaWindowPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaWindow, meta_window, G_TYPE_OBJECT, 
                                  G_ADD_PRIVATE (MetaWindow)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init))

enum
{
  PROP_0,

  PROP_TITLE,
  PROP_DECORATED,
  PROP_FULLSCREEN,
  PROP_MAXIMIZED_HORIZONTALLY,
  PROP_MAXIMIZED_VERTICALLY,
  PROP_MINIMIZED,
  PROP_WINDOW_TYPE,
  PROP_USER_TIME,
  PROP_DEMANDS_ATTENTION,
  PROP_URGENT,
  PROP_SKIP_TASKBAR,
  PROP_MUTTER_HINTS,
  PROP_APPEARS_FOCUSED,
  PROP_RESIZEABLE,
  PROP_ABOVE,
  PROP_WM_CLASS,
  PROP_GTK_APPLICATION_ID,
  PROP_GTK_UNIQUE_BUS_NAME,
  PROP_GTK_APPLICATION_OBJECT_PATH,
  PROP_GTK_WINDOW_OBJECT_PATH,
  PROP_GTK_APP_MENU_OBJECT_PATH,
  PROP_GTK_MENUBAR_OBJECT_PATH,
  PROP_ON_ALL_WORKSPACES,
  PROP_IS_ALIVE,
  PROP_DISPLAY,
  PROP_EFFECT,
  PROP_SUSPEND_STATE,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum
{
  WORKSPACE_CHANGED,
  FOCUS,
  RAISED,
  UNMANAGING,
  UNMANAGED,
  SIZE_CHANGED,
  POSITION_CHANGED,
  SHOWN,
  HIGHEST_SCALE_MONITOR_CHANGED,

  LAST_SIGNAL
};

static guint window_signals[LAST_SIGNAL] = { 0 };

static MetaBackend *
backend_from_window (MetaWindow *window)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);

  return meta_context_get_backend (context);
}

static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  MetaWindow *window = data;

  if (pref == META_PREF_WORKSPACES_ONLY_ON_PRIMARY)
    {
      meta_window_on_all_workspaces_changed (window);
    }
  else if (pref == META_PREF_ATTACH_MODAL_DIALOGS &&
           window->type == META_WINDOW_MODAL_DIALOG)
    {
      window->attached = meta_window_should_attach_to_parent (window);
      meta_window_recalc_features (window);
      meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
    }
  else if (pref == META_PREF_FOCUS_MODE)
    {
      meta_window_update_appears_focused (window);
    }
}

static void
meta_window_real_grab_op_began (MetaWindow *window,
                                MetaGrabOp  op)
{
}

static void
meta_window_real_grab_op_ended (MetaWindow *window,
                                MetaGrabOp  op)
{
}

static void
meta_window_real_current_workspace_changed (MetaWindow *window)
{
}

static gboolean
meta_window_real_update_struts (MetaWindow *window)
{
  return FALSE;
}

static void
meta_window_real_get_default_skip_hints (MetaWindow *window,
                                         gboolean   *skip_taskbar_out,
                                         gboolean   *skip_pager_out)
{
  *skip_taskbar_out = FALSE;
  *skip_pager_out = FALSE;
}

static pid_t
meta_window_real_get_client_pid (MetaWindow *window)
{
  return 0;
}

static void
meta_window_finalize (GObject *object)
{
  MetaWindow *window = META_WINDOW (object);

  g_clear_pointer (&window->frame_bounds, mtk_region_unref);

  if (window->transient_for)
    g_object_unref (window->transient_for);

  if (window->cgroup_path)
    g_object_unref (window->cgroup_path);

  g_free (window->startup_id);
  g_free (window->role);
  g_free (window->res_class);
  g_free (window->res_name);
  g_free (window->title);
  g_free (window->desc);
  g_free (window->sandboxed_app_id);
  g_free (window->gtk_theme_variant);
  g_free (window->gtk_application_id);
  g_free (window->gtk_unique_bus_name);
  g_free (window->gtk_application_object_path);
  g_free (window->gtk_window_object_path);
  g_free (window->gtk_app_menu_object_path);
  g_free (window->gtk_menubar_object_path);
  g_free (window->placement.rule);

  G_OBJECT_CLASS (meta_window_parent_class)->finalize (object);
}

static void
meta_window_get_property(GObject         *object,
                         guint            prop_id,
                         GValue          *value,
                         GParamSpec      *pspec)
{
  MetaWindow *win = META_WINDOW (object);
  MetaWindowPrivate *priv = meta_window_get_instance_private (win);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, win->title);
      break;
    case PROP_DECORATED:
      g_value_set_boolean (value, win->decorated);
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, win->fullscreen);
      break;
    case PROP_MAXIMIZED_HORIZONTALLY:
      g_value_set_boolean (value, win->maximized_horizontally);
      break;
    case PROP_MAXIMIZED_VERTICALLY:
      g_value_set_boolean (value, win->maximized_vertically);
      break;
    case PROP_MINIMIZED:
      g_value_set_boolean (value, win->minimized);
      break;
    case PROP_WINDOW_TYPE:
      g_value_set_enum (value, win->type);
      break;
    case PROP_USER_TIME:
      g_value_set_uint (value, win->net_wm_user_time);
      break;
    case PROP_DEMANDS_ATTENTION:
      g_value_set_boolean (value, win->wm_state_demands_attention);
      break;
    case PROP_URGENT:
      g_value_set_boolean (value, win->urgent);
      break;
    case PROP_SKIP_TASKBAR:
      g_value_set_boolean (value, win->skip_taskbar);
      break;
    case PROP_MUTTER_HINTS:
      g_value_set_string (value, win->mutter_hints);
      break;
    case PROP_APPEARS_FOCUSED:
      g_value_set_boolean (value, win->appears_focused);
      break;
    case PROP_WM_CLASS:
      g_value_set_string (value, win->res_class);
      break;
    case PROP_RESIZEABLE:
      g_value_set_boolean (value, win->has_resize_func);
      break;
    case PROP_ABOVE:
      g_value_set_boolean (value, win->wm_state_above);
      break;
    case PROP_GTK_APPLICATION_ID:
      g_value_set_string (value, win->gtk_application_id);
      break;
    case PROP_GTK_UNIQUE_BUS_NAME:
      g_value_set_string (value, win->gtk_unique_bus_name);
      break;
    case PROP_GTK_APPLICATION_OBJECT_PATH:
      g_value_set_string (value, win->gtk_application_object_path);
      break;
    case PROP_GTK_WINDOW_OBJECT_PATH:
      g_value_set_string (value, win->gtk_window_object_path);
      break;
    case PROP_GTK_APP_MENU_OBJECT_PATH:
      g_value_set_string (value, win->gtk_app_menu_object_path);
      break;
    case PROP_GTK_MENUBAR_OBJECT_PATH:
      g_value_set_string (value, win->gtk_menubar_object_path);
      break;
    case PROP_ON_ALL_WORKSPACES:
      g_value_set_boolean (value, win->on_all_workspaces);
      break;
    case PROP_IS_ALIVE:
      g_value_set_boolean (value, win->is_alive);
      break;
    case PROP_DISPLAY:
      g_value_set_object (value, win->display);
      break;
    case PROP_EFFECT:
      g_value_set_int (value, win->pending_compositor_effect);
      break;
    case PROP_SUSPEND_STATE:
      g_value_set_enum (value, priv->suspend_state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_set_property(GObject         *object,
                         guint            prop_id,
                         const GValue    *value,
                         GParamSpec      *pspec)
{
  MetaWindow *win = META_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      win->display = g_value_get_object (value);
      break;
    case PROP_EFFECT:
      win->pending_compositor_effect = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_class_init (MetaWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_window_constructed;
  object_class->finalize = meta_window_finalize;

  object_class->get_property = meta_window_get_property;
  object_class->set_property = meta_window_set_property;

  klass->grab_op_began = meta_window_real_grab_op_began;
  klass->grab_op_ended = meta_window_real_grab_op_ended;
  klass->current_workspace_changed = meta_window_real_current_workspace_changed;
  klass->update_struts = meta_window_real_update_struts;
  klass->get_default_skip_hints = meta_window_real_get_default_skip_hints;
  klass->get_client_pid = meta_window_real_get_client_pid;

  obj_props[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DECORATED] =
    g_param_spec_boolean ("decorated", NULL, NULL,
                          TRUE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_FULLSCREEN] =
    g_param_spec_boolean ("fullscreen", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MAXIMIZED_HORIZONTALLY] =
    g_param_spec_boolean ("maximized-horizontally", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MAXIMIZED_VERTICALLY] =
    g_param_spec_boolean ("maximized-vertically", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MINIMIZED] =
    g_param_spec_boolean ("minimized", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_WINDOW_TYPE] =
    g_param_spec_enum ("window-type", NULL, NULL,
                       META_TYPE_WINDOW_TYPE,
                       META_WINDOW_NORMAL,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_USER_TIME] =
    g_param_spec_uint ("user-time", NULL, NULL,
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DEMANDS_ATTENTION] =
    g_param_spec_boolean ("demands-attention", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_URGENT] =
    g_param_spec_boolean ("urgent", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SKIP_TASKBAR] =
    g_param_spec_boolean ("skip-taskbar", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_MUTTER_HINTS] =
    g_param_spec_string ("mutter-hints", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_APPEARS_FOCUSED] =
    g_param_spec_boolean ("appears-focused", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_RESIZEABLE] =
    g_param_spec_boolean ("resizeable", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ABOVE] =
    g_param_spec_boolean ("above", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_WM_CLASS] =
    g_param_spec_string ("wm-class", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_APPLICATION_ID] =
    g_param_spec_string ("gtk-application-id", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_UNIQUE_BUS_NAME] =
    g_param_spec_string ("gtk-unique-bus-name", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_APPLICATION_OBJECT_PATH] =
    g_param_spec_string ("gtk-application-object-path", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_WINDOW_OBJECT_PATH] =
    g_param_spec_string ("gtk-window-object-path", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_APP_MENU_OBJECT_PATH] =
    g_param_spec_string ("gtk-app-menu-object-path", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GTK_MENUBAR_OBJECT_PATH] =
    g_param_spec_string ("gtk-menubar-object-path", NULL, NULL,
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ON_ALL_WORKSPACES] =
    g_param_spec_boolean ("on-all-workspaces", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_IS_ALIVE] =
    g_param_spec_boolean ("is-alive", NULL, NULL,
                          TRUE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display", NULL, NULL,
                         META_TYPE_DISPLAY,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  obj_props[PROP_EFFECT] =
    g_param_spec_int ("effect", NULL, NULL,
                      META_COMP_EFFECT_CREATE,
                      META_COMP_EFFECT_NONE,
                      META_COMP_EFFECT_NONE,
                      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  /**
   * MetaWindow::suspend-state: (skip)
   */
  obj_props[PROP_SUSPEND_STATE] =
    g_param_spec_enum ("suspend-state", NULL, NULL,
                       META_TYPE_WINDOW_SUSPEND_STATE,
                       META_WINDOW_SUSPEND_STATE_SUSPENDED,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  window_signals[WORKSPACE_CHANGED] =
    g_signal_new ("workspace-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  window_signals[FOCUS] =
    g_signal_new ("focus",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  window_signals[RAISED] =
    g_signal_new ("raised",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  window_signals[UNMANAGING] =
    g_signal_new ("unmanaging",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  window_signals[UNMANAGED] =
    g_signal_new ("unmanaged",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindow::position-changed:
   * @window: a #MetaWindow
   *
   * This is emitted when the position of a window might
   * have changed.
   *
   * Specifically, this is emitted when the position of
   * the toplevel window has changed, or when the position
   * of the client window has changed.
   */
  window_signals[POSITION_CHANGED] =
    g_signal_new ("position-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindow::shown:
   * @window: a #MetaWindow
   *
   * This is emitted after a window has been shown.
   */
  window_signals[SHOWN] =
    g_signal_new ("shown",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindow::size-changed:
   * @window: a #MetaWindow
   *
   * This is emitted when the size of a window might
   * have changed.
   *
   * Specifically, this is emitted when the size of
   * the toplevel window has changed, or when the
   * size of the client window has changed.
   */
  window_signals[SIZE_CHANGED] =
    g_signal_new ("size-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * MetaWindow::highest-scale-monitor-changed:
   * @window: a #MetaWindow
   *
   * This is emitted when the monitor with the highest scale
   * intersecting the window changes.
   */
  window_signals[HIGHEST_SCALE_MONITOR_CHANGED] =
    g_signal_new ("highest-scale-monitor-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_window_init (MetaWindow *window)
{
  window->stamp = next_window_stamp++;
  meta_prefs_add_listener (prefs_changed_callback, window);
  window->is_alive = TRUE;
}

static gboolean
is_desktop_or_dock_foreach (MetaWindow *window,
                            void       *data)
{
  gboolean *result = data;

  *result =
    window->type == META_WINDOW_DESKTOP ||
    window->type == META_WINDOW_DOCK ||
    window->skip_from_window_list;
  if (*result)
    return FALSE; /* stop as soon as we find one */
  else
    return TRUE;
}

/* window is the window that's newly mapped provoking
 * the possible change
 */
static void
maybe_leave_show_desktop_mode (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  gboolean is_desktop_or_dock;

  if (!workspace_manager->active_workspace->showing_desktop)
    return;

  /* If the window is a transient for the dock or desktop, don't
   * leave show desktop mode when the window opens. That's
   * so you can e.g. hide all windows, manipulate a file on
   * the desktop via a dialog, then unshow windows again.
   */
  is_desktop_or_dock = FALSE;
  is_desktop_or_dock_foreach (window,
                              &is_desktop_or_dock);

  meta_window_foreach_ancestor (window, is_desktop_or_dock_foreach,
                                &is_desktop_or_dock);

  if (!is_desktop_or_dock)
    {
      meta_workspace_manager_minimize_all_on_active_workspace_except (workspace_manager,
                                                                      window);
      meta_workspace_manager_unshow_desktop (workspace_manager);
    }
}

gboolean
meta_window_should_attach_to_parent (MetaWindow *window)
{
  MetaWindow *parent;

  if (!meta_prefs_get_attach_modal_dialogs () ||
      window->type != META_WINDOW_MODAL_DIALOG)
    return FALSE;

  parent = meta_window_get_transient_for (window);
  if (!parent)
    return FALSE;

  switch (parent->type)
    {
    case META_WINDOW_NORMAL:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
      return TRUE;

    default:
      return FALSE;
    }
}

static gboolean
client_window_should_be_mapped (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      MetaWaylandSurface *surface = meta_window_get_wayland_surface (window);
      if (!meta_wayland_surface_get_buffer (surface))
        return FALSE;
    }
#endif

  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11 &&
      window->decorated && !window->frame)
    return FALSE;

  return TRUE;
}

static void
sync_client_window_mapped (MetaWindow *window)
{
  gboolean should_be_mapped = client_window_should_be_mapped (window);

  g_return_if_fail (!window->override_redirect);

  if (window->mapped == should_be_mapped)
    return;

  window->mapped = should_be_mapped;

  if (window->mapped)
    META_WINDOW_GET_CLASS (window)->map (window);
  else
    META_WINDOW_GET_CLASS (window)->unmap (window);
}

static gboolean
meta_window_update_flatpak_id (MetaWindow *window,
                               uint32_t    pid)
{
  g_autoptr(GKeyFile) key_file = NULL;
  g_autofree char *info_filename = NULL;

  g_return_val_if_fail (pid != 0, FALSE);
  g_return_val_if_fail (window->sandboxed_app_id == NULL, FALSE);

  key_file = g_key_file_new ();
  info_filename = g_strdup_printf ("/proc/%u/root/.flatpak-info", pid);

  if (!g_key_file_load_from_file (key_file, info_filename, G_KEY_FILE_NONE, NULL))
    return FALSE;

  window->sandboxed_app_id = g_key_file_get_string (key_file, "Application", "name", NULL);

  return TRUE;
}

static gboolean
meta_window_update_snap_id (MetaWindow *window,
                            uint32_t    pid)
{
  g_autofree char *security_label_filename = NULL;
  g_autofree char *security_label_contents = NULL;
  gsize i, security_label_contents_size = 0;
  char *contents_start;
  char *contents_end;
  char *sandboxed_app_id;

  g_return_val_if_fail (pid != 0, FALSE);
  g_return_val_if_fail (window->sandboxed_app_id == NULL, FALSE);

  security_label_filename = g_strdup_printf ("/proc/%u/attr/current", pid);

  if (!g_file_get_contents (security_label_filename,
                            &security_label_contents,
                            &security_label_contents_size,
                            NULL))
    return FALSE;

  if (!g_str_has_prefix (security_label_contents, SNAP_SECURITY_LABEL_PREFIX))
    return FALSE;

  /* We need to translate the security profile into the desktop-id.
   * The profile is in the form of 'snap.name-space.binary-name (current)'
   * while the desktop id will be name-space_binary-name.
   */
  security_label_contents_size -= sizeof (SNAP_SECURITY_LABEL_PREFIX) - 1;
  contents_start = security_label_contents + sizeof (SNAP_SECURITY_LABEL_PREFIX) - 1;
  contents_end = strchr (contents_start, ' ');

  if (contents_end)
    security_label_contents_size = contents_end - contents_start;

  for (i = 0; i < security_label_contents_size; ++i)
    {
      if (contents_start[i] == '.')
        contents_start[i] = '_';
    }

  sandboxed_app_id = g_malloc0 (security_label_contents_size + 1);
  memcpy (sandboxed_app_id, contents_start, security_label_contents_size);

  window->sandboxed_app_id = sandboxed_app_id;

  return TRUE;
}

static void
meta_window_update_sandboxed_app_id (MetaWindow *window)
{
  pid_t pid;

  g_clear_pointer (&window->sandboxed_app_id, g_free);

  pid = meta_window_get_pid (window);

  if (pid < 1)
    return;

  if (meta_window_update_flatpak_id (window, pid))
    return;

  if (meta_window_update_snap_id (window, pid))
    return;
}

static void
meta_window_update_desc (MetaWindow *window)
{
  g_clear_pointer (&window->desc, g_free);

#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    window->desc = g_strdup_printf ("0x%lx", meta_window_x11_get_xwindow (window));
  else
#endif
    {
      guint64 small_stamp = window->stamp - G_GUINT64_CONSTANT (0x100000000);

      window->desc = g_strdup_printf ("W%" G_GUINT64_FORMAT, small_stamp);
    }
}

static void
meta_window_main_monitor_changed (MetaWindow               *window,
                                  const MetaLogicalMonitor *old)
{
  META_WINDOW_GET_CLASS (window)->main_monitor_changed (window, old);

  if (old)
    g_signal_emit_by_name (window->display, "window-left-monitor",
                           old->number, window);
  if (window->monitor)
    g_signal_emit_by_name (window->display, "window-entered-monitor",
                           window->monitor->number, window);
}

MetaLogicalMonitor *
meta_window_find_monitor_from_frame_rect (MetaWindow *window)
{
  MetaBackend *backend = backend_from_window (window);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MtkRectangle window_rect;

  meta_window_get_frame_rect (window, &window_rect);
  return meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager,
                                                             &window_rect);
}

static MetaLogicalMonitor *
meta_window_find_highest_scale_monitor_from_frame_rect (MetaWindow *window)
{
  MetaBackend *backend = backend_from_window (window);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MtkRectangle window_rect;

  meta_window_get_frame_rect (window, &window_rect);
  return meta_monitor_manager_get_highest_scale_monitor_from_rect (monitor_manager,
                                                                   &window_rect);
}

static void
meta_window_manage (MetaWindow *window)
{
  COGL_TRACE_BEGIN_SCOPED (MetaWindowManage,
                           "Meta::Window::manage()");

  META_WINDOW_GET_CLASS (window)->manage (window);
}

static void
meta_window_constructed (GObject *object)
{
  MetaWindow *window = META_WINDOW (object);
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);
  MetaDisplay *display = window->display;
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;

  COGL_TRACE_BEGIN_SCOPED (MetaWindowSharedInit,
                           "Meta::Window::constructed()");

  window->constructing = TRUE;

  meta_display_register_stamp (display, &window->stamp, window);

  window->workspace = NULL;

  meta_window_update_sandboxed_app_id (window);
  meta_window_update_desc (window);

  /* avoid tons of stack updates */
  meta_stack_freeze (display->stack);

  /* initialize the remaining size_hints as if size_hints.flags were zero */
#ifdef HAVE_X11_CLIENT
  meta_set_normal_hints (window, NULL);
#endif

  /* And this is our unmaximized size */
  window->saved_rect = window->rect;
  window->saved_rect_fullscreen = window->rect;
  window->unconstrained_rect = window->rect;

  window->title = NULL;

  window->frame = NULL;
  window->has_focus = FALSE;
  window->attached_focus_window = NULL;

  window->maximized_horizontally = FALSE;
  window->maximized_vertically = FALSE;
  window->maximize_horizontally_after_placement = FALSE;
  window->maximize_vertically_after_placement = FALSE;
  window->minimize_after_placement = FALSE;
  window->fullscreen = FALSE;
  window->require_fully_onscreen = TRUE;
  window->require_on_single_monitor = TRUE;
  window->require_titlebar_visible = TRUE;
  window->on_all_workspaces = FALSE;
  window->on_all_workspaces_requested = FALSE;
  window->tile_mode = META_TILE_NONE;
  window->tile_monitor_number = -1;
  window->tile_hfraction = -1.;
  window->initially_iconic = FALSE;
  window->minimized = FALSE;
  window->iconic = FALSE;
  window->known_to_compositor = FALSE;
  window->visible_to_compositor = FALSE;
  /* if already mapped, no need to worry about focus-on-first-time-showing */
  window->showing_for_first_time = !window->mapped;
  /* if already mapped we don't want to do the placement thing;
   * override-redirect windows are placed by the app */
  window->placed = ((window->mapped && !window->hidden) || window->override_redirect);
  window->denied_focus_and_not_transient = FALSE;
  window->unmanaging = FALSE;
  window->withdrawn = FALSE;
  window->initial_workspace_set = FALSE;
  window->initial_timestamp_set = FALSE;
  window->net_wm_user_time_set = FALSE;
  window->input = TRUE;
  window->calc_placement = FALSE;
  window->have_focus_click_grab = FALSE;

  window->unmaps_pending = 0;
  window->reparents_pending = 0;

  window->mwm_decorated = TRUE;
  window->mwm_border_only = FALSE;
  window->mwm_has_close_func = TRUE;
  window->mwm_has_minimize_func = TRUE;
  window->mwm_has_maximize_func = TRUE;
  window->mwm_has_move_func = TRUE;
  window->mwm_has_resize_func = TRUE;

  window->has_close_func = TRUE;
  window->has_minimize_func = TRUE;
  window->has_maximize_func = TRUE;
  window->has_move_func = TRUE;
  window->has_resize_func = TRUE;

  window->has_fullscreen_func = TRUE;

  window->always_sticky = FALSE;

  window->skip_taskbar = FALSE;
  window->skip_pager = FALSE;
  window->skip_from_window_list = FALSE;
  window->wm_state_above = FALSE;
  window->wm_state_below = FALSE;
  window->wm_state_demands_attention = FALSE;

  window->res_class = NULL;
  window->res_name = NULL;
  window->role = NULL;
  window->is_remote = FALSE;
  window->startup_id = NULL;

  window->client_pid = 0;

  window->has_valid_cgroup = TRUE;
  window->cgroup_path = NULL;

  window->type = META_WINDOW_NORMAL;

  window->struts = NULL;

  window->layer = META_LAYER_LAST; /* invalid value */
  window->stack_position = -1;
  window->initial_workspace = 0; /* not used */
  window->initial_timestamp = 0; /* not used */

  window->compositor_private = NULL;

  if (window->rect.width > 0 && window->rect.height > 0)
    {
      window->monitor = meta_window_find_monitor_from_frame_rect (window);
      window->highest_scale_monitor =
        meta_window_find_highest_scale_monitor_from_frame_rect (window);
    }
  else
    {
      window->monitor = meta_backend_get_current_logical_monitor (backend);
      window->highest_scale_monitor = window->monitor;
    }

  if (window->monitor)
    window->preferred_output_winsys_id = window->monitor->winsys_id;
  else
    window->preferred_output_winsys_id = UINT_MAX;

  window->tile_match = NULL;

  /* Assign this #MetaWindow a sequence number which can be used
   * for sorting.
   */
  window->stable_sequence = ++display->window_sequence_counter;

  window->opacity = 0xFF;

  if (window->override_redirect)
    {
      window->decorated = FALSE;
      window->always_sticky = TRUE;
      window->has_close_func = FALSE;
      window->has_move_func = FALSE;
      window->has_resize_func = FALSE;
    }

  window->id = meta_display_generate_window_id (display);

  meta_window_manage (window);

  if (window->initially_iconic)
    {
      /* WM_HINTS said minimized */
      window->minimized = TRUE;
      meta_verbose ("Window %s asked to start out minimized", window->desc);
    }

  /* Apply any window attributes such as initial workspace
   * based on startup notification
   */
  meta_display_apply_startup_properties (display, window);

  /* Try to get a "launch timestamp" for the window.  If the window is
   * a transient, we'd like to be able to get a last-usage timestamp
   * from the parent window.  If the window has no parent, there isn't
   * much we can do...except record the current time so that any children
   * can use this time as a fallback.
   */
  if (!window->override_redirect && !window->net_wm_user_time_set) {
    /* First, maybe the app was launched with startup notification using an
     * obsolete version of the spec; use that timestamp if it exists.
     */
    if (window->initial_timestamp_set)
      /* NOTE: Do NOT toggle net_wm_user_time_set to true; this is just
       * being recorded as a fallback for potential transients
       */
      window->net_wm_user_time = window->initial_timestamp;
    else if (window->transient_for != NULL)
      meta_window_set_user_time (window, window->transient_for->net_wm_user_time);
    else
      /* NOTE: Do NOT toggle net_wm_user_time_set to true; this is just
       * being recorded as a fallback for potential transients
       */
      window->net_wm_user_time =
        meta_display_get_current_time_roundtrip (display);
  }

  window->attached = meta_window_should_attach_to_parent (window);
  if (window->attached)
    meta_window_recalc_features (window);

  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK)
    {
      /* Change the default, but don't enforce this if the user
       * focuses the dock/desktop and unsticks it using key shortcuts.
       * Need to set this before adding to the workspaces so the MRU
       * lists will be updated.
       */
      window->on_all_workspaces_requested = TRUE;
    }

  window->on_all_workspaces = should_be_on_all_workspaces (window);

  /* For the workspace, first honor hints,
   * if that fails put transients with parents,
   * otherwise put window on active space
   */

  if (window->initial_workspace_set)
    {
      gboolean on_all_workspaces = window->on_all_workspaces;
      MetaWorkspace *workspace = NULL;

      if (window->initial_workspace == (int) 0xFFFFFFFF)
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Window %s is initially on all spaces",
                      window->desc);

	  /* need to set on_all_workspaces first so that it will be
	   * added to all the MRU lists
	   */
          window->on_all_workspaces_requested = TRUE;

          on_all_workspaces = TRUE;
        }
      else if (!on_all_workspaces)
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Window %s is initially on space %d",
                      window->desc, window->initial_workspace);

          workspace = meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                                     window->initial_workspace);
        }

      /* Ignore when a window requests to be placed on a non-existent workspace
       */
      if (on_all_workspaces || workspace != NULL)
        set_workspace_state (window, on_all_workspaces, workspace);
    }

  /* override-redirect windows are subtly different from other windows
   * with window->on_all_workspaces == TRUE. Other windows are part of
   * some workspace (so they can return to that if the flag is turned off),
   * but appear on other workspaces. override-redirect windows are part
   * of no workspace.
   */
  if (!window->override_redirect && window->workspace == NULL)
    {
      if (window->transient_for != NULL)
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Putting window %s on same workspace as parent %s",
                      window->desc, window->transient_for->desc);

          g_warn_if_fail (!window->transient_for->override_redirect);
          set_workspace_state (window,
                               window->transient_for->on_all_workspaces,
                               window->transient_for->workspace);
        }
      else if (window->on_all_workspaces)
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Putting window %s on all workspaces",
                      window->desc);

          set_workspace_state (window, TRUE, NULL);
        }
      else
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Putting window %s on active workspace",
                      window->desc);

          set_workspace_state (window, FALSE, workspace_manager->active_workspace);
        }

      meta_window_update_struts (window);
    }

  meta_window_main_monitor_changed (window, NULL);

  /* Must add window to stack before doing move/resize, since the
   * window might have fullscreen size (i.e. should have been
   * fullscreen'd; acrobat is one such braindead case; it withdraws
   * and remaps its window whenever trying to become fullscreen...)
   * and thus constraints may try to auto-fullscreen it which also
   * means restacking it.
   */
  if (meta_window_is_stackable (window))
    meta_stack_add (display->stack, window);
  else if (window->override_redirect)
    window->layer = META_LAYER_OVERRIDE_REDIRECT; /* otherwise set by MetaStack */

  if (!window->override_redirect)
    {
      /* FIXME we have a tendency to set this then immediately
       * change it again.
       */
      set_wm_state (window);
      set_net_wm_state (window);
    }

  meta_compositor_add_window (display->compositor, window);
  window->known_to_compositor = TRUE;

  /* Sync stack changes */
  meta_stack_thaw (display->stack);

  /* Usually the we'll have queued a stack sync anyways, because we've
   * added a new frame window or restacked. But if an undecorated
   * window is mapped, already stacked in the right place, then we
   * might need to do this explicitly.
   */
  meta_stack_tracker_queue_sync_stack (display->stack_tracker);

  /* disable show desktop mode unless we're a desktop component */
  maybe_leave_show_desktop_mode (window);

  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  /* See bug 303284; a transient of the given window can already exist, in which
   * case we think it should probably be shown.
   */
  meta_window_foreach_transient (window,
                                 queue_calc_showing_func,
                                 NULL);
  /* See bug 334899; the window may have minimized ancestors
   * which need to be shown.
   *
   * However, we shouldn't unminimize windows here when opening
   * a new display because that breaks passing _NET_WM_STATE_HIDDEN
   * between window managers when replacing them; see bug 358042.
   *
   * And we shouldn't unminimize windows if they were initially
   * iconic.
   */
  if (!window->override_redirect &&
      !display->display_opening &&
      !window->initially_iconic)
    unminimize_window_and_all_transient_parents (window);

  /* There is a slim chance we'll hit time out before a extremely slow client
   * managed to become active, but unlikely enough. */
  priv->suspend_state = META_WINDOW_SUSPEND_STATE_HIDDEN;
  set_hidden_suspended_state (window);

  window->constructing = FALSE;
}

static gboolean
meta_window_initable_init (GInitable     *initable,
                           GCancellable  *cancellable,
                           GError       **error)
{
  MetaWindow *window = META_WINDOW (initable);
  MetaDisplay *display = window->display;

  meta_display_notify_window_created (display, window);

  if (window->wm_state_demands_attention)
    g_signal_emit_by_name (display, "window-demands-attention", window);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_window_initable_init;
}

static gboolean
detach_foreach_func (MetaWindow *window,
                     void       *data)
{
  GList **children = data;
  MetaWindow *parent;

  if (window->attached)
    {
      /* Only return the immediate children of the window being unmanaged */
      parent = meta_window_get_transient_for (window);
      if (parent->unmanaging)
        *children = g_list_prepend (*children, window);
    }

  return TRUE;
}

void
meta_window_unmanage (MetaWindow  *window,
                      guint32      timestamp)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  GList *tmp;

  meta_verbose ("Unmanaging %s", window->desc);
  window->unmanaging = TRUE;

  g_clear_handle_id (&priv->suspend_timoeut_id, g_source_remove);
  g_clear_handle_id (&window->close_dialog_timeout_id, g_source_remove);

  g_signal_emit (window, window_signals[UNMANAGING], 0);

  meta_window_free_delete_dialog (window);

  if (window->visible_to_compositor)
    {
      window->visible_to_compositor = FALSE;
      meta_compositor_hide_window (window->display->compositor, window,
                                   META_COMP_EFFECT_DESTROY);
    }

  meta_compositor_remove_window (window->display->compositor, window);
  window->known_to_compositor = FALSE;

  meta_display_unregister_stamp (window->display, window->stamp);

  if (meta_prefs_get_attach_modal_dialogs ())
    {
      GList *attached_children = NULL, *iter;

      /* Detach any attached dialogs by unmapping and letting them
       * be remapped after @window is destroyed.
       */
      meta_window_foreach_transient (window,
                                     detach_foreach_func,
                                     &attached_children);
      for (iter = attached_children; iter; iter = iter->next)
        meta_window_unmanage (iter->data, timestamp);
      g_list_free (attached_children);
    }

  /* Make sure to only show window on all workspaces if requested, to
   * not confuse other window managers that may take over
   */
  if (meta_prefs_get_workspaces_only_on_primary ())
    meta_window_on_all_workspaces_changed (window);

#ifdef HAVE_X11_CLIENT
  if (window->fullscreen)
    {
      MetaGroup *group = NULL;
      /* If the window is fullscreen, it may be forcing
       * other windows in its group to a higher layer
       */

      meta_stack_freeze (window->display->stack);
      if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
        {
          group = meta_window_x11_get_group (window);
          if (group)
            meta_group_update_layers (group);
        }
      meta_stack_thaw (window->display->stack);
    }
#endif

  meta_display_remove_pending_pings_for_window (window->display, window);

  /* safe to do this early as group.c won't re-add to the
   * group if window->unmanaging */
#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    meta_window_x11_shutdown_group (window);
#endif

  /* If we have the focus, focus some other window.
   * This is done first, so that if the unmap causes
   * an EnterNotify the EnterNotify will have final say
   * on what gets focused, maintaining sloppy focus
   * invariants.
   */
  if (window->appears_focused)
    meta_window_propagate_focus_appearance (window, FALSE);
  if (window->has_focus)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing default window since we're unmanaging %s",
                  window->desc);
      meta_workspace_focus_default_window (workspace_manager->active_workspace,
                                           window,
                                           timestamp);
    }
  else
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Unmanaging window %s which doesn't currently have focus",
                  window->desc);
    }

  g_assert (window->display->focus_window != window);

  if (window->struts)
    {
      g_slist_free_full (window->struts, g_free);
      window->struts = NULL;

      meta_topic (META_DEBUG_WORKAREA,
                  "Unmanaging window %s which has struts, so invalidating work areas",
                  window->desc);
      invalidate_work_areas (window);
    }

  if (window->maximized_horizontally || window->maximized_vertically)
    unmaximize_window_before_freeing (window);

  meta_window_unqueue (window,
                       (META_QUEUE_CALC_SHOWING |
                        META_QUEUE_MOVE_RESIZE));

  set_workspace_state (window, FALSE, NULL);

  g_assert (window->workspace == NULL);

#ifndef G_DISABLE_CHECKS
  tmp = workspace_manager->workspaces;
  while (tmp != NULL)
    {
      MetaWorkspace *workspace = tmp->data;

      g_assert (g_list_find (workspace->windows, window) == NULL);
      g_assert (g_list_find (workspace->mru_list, window) == NULL);

      tmp = tmp->next;
    }
#endif

  if (window->monitor)
    {
      const MetaLogicalMonitor *old = window->monitor;

      window->monitor = NULL;
      meta_window_main_monitor_changed (window, old);
    }

  if (meta_window_is_in_stack (window))
    meta_stack_remove (window->display->stack, window);

  /* If an undecorated window is being withdrawn, that will change the
   * stack as presented to the compositing manager, without actually
   * changing the stacking order of X windows.
   */
  meta_stack_tracker_queue_sync_stack (window->display->stack_tracker);

  if (window->display->autoraise_window == window)
    meta_display_remove_autoraise_callback (window->display);

  META_WINDOW_GET_CLASS (window)->unmanage (window);

  meta_prefs_remove_listener (prefs_changed_callback, window);
  meta_display_queue_check_fullscreen (window->display);

  g_signal_emit (window, window_signals[UNMANAGED], 0);

  g_object_unref (window);
}

static void
set_wm_state (MetaWindow *window)
{
#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    meta_window_x11_set_wm_state (window);
#endif
}

static void
set_net_wm_state (MetaWindow *window)
{
#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    meta_window_x11_set_net_wm_state (window);
#endif
}

static void
set_allowed_actions_hint (MetaWindow *window)
{
#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    meta_window_x11_set_allowed_actions_hint (window);
#endif
}

/**
 * meta_window_located_on_workspace:
 * @window: a #MetaWindow
 * @workspace: a #MetaWorkspace
 *
 * Returns: whether @window is displayed on @workspace, or whether it
 * will be displayed on all workspaces.
 */
gboolean
meta_window_located_on_workspace (MetaWindow    *window,
                                  MetaWorkspace *workspace)
{
  return (window->on_all_workspaces) || (window->workspace == workspace);
}

static gboolean
is_minimized_foreach (MetaWindow *window,
                      void       *data)
{
  gboolean *result = data;

  *result = window->minimized;
  if (*result)
    return FALSE; /* stop as soon as we find one */
  else
    return TRUE;
}

static gboolean
ancestor_is_minimized (MetaWindow *window)
{
  gboolean is_minimized;

  is_minimized = FALSE;

  meta_window_foreach_ancestor (window, is_minimized_foreach, &is_minimized);

  return is_minimized;
}

/**
 * meta_window_showing_on_its_workspace:
 * @window: A #MetaWindow
 *
 * Returns: %TRUE if window would be visible, if its workspace was current
 */
gboolean
meta_window_showing_on_its_workspace (MetaWindow *window)
{
  gboolean showing;
  gboolean is_desktop_or_dock;
  MetaWorkspace* workspace_of_window;

  showing = TRUE;

  /* 1. See if we're minimized */
  if (window->minimized)
    showing = FALSE;

  /* 2. See if we're in "show desktop" mode */
  is_desktop_or_dock = FALSE;
  is_desktop_or_dock_foreach (window,
                              &is_desktop_or_dock);

  meta_window_foreach_ancestor (window, is_desktop_or_dock_foreach,
                                &is_desktop_or_dock);

  workspace_of_window = meta_window_get_workspace (window);

  if (showing &&
      workspace_of_window && workspace_of_window->showing_desktop &&
      !is_desktop_or_dock)
    {
      meta_verbose ("We're showing the desktop on the workspace(s) that window %s is on",
                    window->desc);
      showing = FALSE;
    }

  /* 3. See if an ancestor is minimized (note that
   *    ancestor's "mapped" field may not be up to date
   *    since it's being computed in this same idle queue)
   */

  if (showing)
    {
      if (ancestor_is_minimized (window))
        showing = FALSE;
    }

  return showing;
}

static gboolean
window_has_buffer (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
      MetaWaylandSurface *surface = meta_window_get_wayland_surface (window);
      if (!surface || !meta_wayland_surface_get_buffer (surface))
        return FALSE;
    }
#endif

  return TRUE;
}

static gboolean
meta_window_is_showable (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND &&
      !window_has_buffer (window))
    return FALSE;
#endif

  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11 &&
      window->decorated && !window->frame)
    return FALSE;

  return TRUE;
}

/**
 * meta_window_should_show_on_workspace:
 *
 * Tells whether a window should be showing on the passed workspace, without
 * taking into account whether it can immediately be shown. Whether it can be
 * shown or not depends on what windowing system it was created from.
 *
 * Returns: %TRUE if the window should show.
 */
static gboolean
meta_window_should_show_on_workspace (MetaWindow    *window,
                                      MetaWorkspace *workspace)
{
  return (meta_window_located_on_workspace (window, workspace) &&
          meta_window_showing_on_its_workspace (window));
}

/**
 * meta_window_should_show:
 *
 * Tells whether a window should be showing on the current workspace, without
 * taking into account whether it can immediately be shown. Whether it can be
 * shown or not depends on what windowing system it was created from.
 *
 * Returns: %TRUE if the window should show.
 */
gboolean
meta_window_should_show (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MetaWorkspace *active_workspace = workspace_manager->active_workspace;

  return meta_window_should_show_on_workspace (window, active_workspace);
}

/**
 * meta_window_should_be_showing_on_workspace:
 *
 * Tells whether a window should be showing on the passed workspace, while
 * taking whether it can be immediately be shown. Whether it can be shown or
 * not depends on what windowing system it was created from.
 *
 * Returns: %TRUE if the window should and can be shown.
 */
gboolean
meta_window_should_be_showing_on_workspace (MetaWindow    *window,
                                            MetaWorkspace *workspace)
{
  if (!meta_window_is_showable (window))
    return FALSE;

  return meta_window_should_show_on_workspace (window, workspace);
}

/**
 * meta_window_should_be_showing:
 *
 * Tells whether a window should be showing on the current workspace, while
 * taking whether it can be immediately be shown. Whether it can be shown or
 * not depends on what windowing system it was created from.
 *
 * Returns: %TRUE if the window should and can be shown.
 */
gboolean
meta_window_should_be_showing (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MetaWorkspace *active_workspace = workspace_manager->active_workspace;

  return meta_window_should_be_showing_on_workspace (window, active_workspace);
}

static void
implement_showing (MetaWindow *window,
                   gboolean    showing)
{
  /* Actually show/hide the window */
  meta_verbose ("Implement showing = %d for window %s",
                showing, window->desc);

  /* Some windows are not stackable until being showed, so add those now. */
  if (meta_window_is_stackable (window) && !meta_window_is_in_stack (window))
    meta_stack_add (window->display->stack, window);

  if (!showing)
    {
      /* When we manage a new window, we normally delay placing it
       * until it is is first shown, but if we're previewing hidden
       * windows we might want to know where they are on the screen,
       * so we should place the window even if we're hiding it rather
       * than showing it.
       * Force placing windows only when they should be already mapped,
       * see #751887
       */
      if (!window->placed && window_has_buffer (window))
        meta_window_force_placement (window, FALSE);

      meta_window_hide (window);

      if (!window->override_redirect)
        sync_client_window_mapped (window);
    }
  else
    {
      if (!window->override_redirect)
        sync_client_window_mapped (window);

      meta_window_show (window);
    }
}

void
meta_window_update_visibility (MetaWindow  *window)
{
  implement_showing (window, meta_window_should_be_showing (window));
}

void
meta_window_clear_queued (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  priv->queued_types &= ~META_QUEUE_CALC_SHOWING;
}

static void
meta_window_unqueue (MetaWindow    *window,
                     MetaQueueType  queue_types)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  queue_types &= priv->queued_types;

  if (!queue_types)
    return;

  meta_display_unqueue_window (window->display, window, queue_types);
  priv->queued_types &= ~queue_types;
}

static void
meta_window_flush_calc_showing (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  if (!(priv->queued_types & META_QUEUE_CALC_SHOWING))
    return;

  meta_display_flush_queued_window (window->display, window,
                                    META_QUEUE_CALC_SHOWING);

  priv->queued_types &= ~META_QUEUE_CALC_SHOWING;
}

void
meta_window_queue (MetaWindow   *window,
                   MetaQueueType queue_types)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  g_return_if_fail (!window->override_redirect ||
                    (queue_types & META_QUEUE_MOVE_RESIZE) == 0);

  if (window->unmanaging)
    return;

  queue_types &= ~priv->queued_types;
  if (!queue_types)
    return;

  priv->queued_types |= queue_types;
  meta_display_queue_window (window->display, window, queue_types);
}

static gboolean
intervening_user_event_occurred (MetaWindow *window)
{
  guint32 compare;
  MetaWindow *focus_window;

  focus_window = window->display->focus_window;

  meta_topic (META_DEBUG_STARTUP,
              "COMPARISON:\n"
              "  net_wm_user_time_set : %d\n"
              "  net_wm_user_time     : %u\n"
              "  initial_timestamp_set: %d\n"
              "  initial_timestamp    : %u",
              window->net_wm_user_time_set,
              window->net_wm_user_time,
              window->initial_timestamp_set,
              window->initial_timestamp);
  if (focus_window != NULL)
    {
      meta_topic (META_DEBUG_STARTUP,
                  "COMPARISON (continued):\n"
                  "  focus_window             : %s\n"
                  "  fw->net_wm_user_time_set : %d\n"
                  "  fw->net_wm_user_time     : %u",
                  focus_window->desc,
                  focus_window->net_wm_user_time_set,
                  focus_window->net_wm_user_time);
    }

  /* We expect the most common case for not focusing a new window
   * to be when a hint to not focus it has been set.  Since we can
   * deal with that case rapidly, we use special case it--this is
   * merely a preliminary optimization.  :)
   */
  if ( ((window->net_wm_user_time_set == TRUE) &&
       (window->net_wm_user_time == 0))
      ||
       ((window->initial_timestamp_set == TRUE) &&
       (window->initial_timestamp == 0)))
    {
      meta_topic (META_DEBUG_STARTUP,
                  "window %s explicitly requested no focus",
                  window->desc);
      return TRUE;
    }

  if (!(window->net_wm_user_time_set) && !(window->initial_timestamp_set))
    {
      meta_topic (META_DEBUG_STARTUP,
                  "no information about window %s found",
                  window->desc);
      return FALSE;
    }

  if (focus_window != NULL &&
      !focus_window->net_wm_user_time_set)
    {
      meta_topic (META_DEBUG_STARTUP,
                  "focus window, %s, doesn't have a user time set yet!",
                  window->desc);
      return FALSE;
    }

  /* To determine the "launch" time of an application,
   * startup-notification can set the TIMESTAMP and the
   * application (usually via its toolkit such as gtk or qt) can
   * set the _NET_WM_USER_TIME.  If both are set, we need to be
   * using the newer of the two values.
   *
   * See http://bugzilla.gnome.org/show_bug.cgi?id=573922
   */
  compare = 0;
  if (window->net_wm_user_time_set &&
      window->initial_timestamp_set)
    compare =
      XSERVER_TIME_IS_BEFORE (window->net_wm_user_time,
                              window->initial_timestamp) ?
      window->initial_timestamp : window->net_wm_user_time;
  else if (window->net_wm_user_time_set)
    compare = window->net_wm_user_time;
  else if (window->initial_timestamp_set)
    compare = window->initial_timestamp;

  if ((focus_window != NULL) &&
      XSERVER_TIME_IS_BEFORE (compare, focus_window->net_wm_user_time))
    {
      meta_topic (META_DEBUG_STARTUP,
                  "window %s focus prevented by other activity; %u < %u",
                  window->desc,
                  compare,
                  focus_window->net_wm_user_time);
      return TRUE;
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "new window %s with no intervening events",
                  window->desc);
      return FALSE;
    }
}

/* This function determines what state the window should have assuming that it
 * and the focus_window have no relation
 */
static void
window_state_on_map (MetaWindow *window,
                     gboolean *takes_focus,
                     gboolean *places_on_top)
{
  gboolean intervening_events;

  intervening_events = intervening_user_event_occurred (window);

  *takes_focus = !intervening_events;
  *places_on_top = *takes_focus;

  /* don't initially focus windows that are intended to not accept
   * focus
   */
  if (!meta_window_is_focusable (window))
    {
      *takes_focus = FALSE;
      return;
    }

  /* When strict focus mode is enabled, prevent new windows from taking
   * focus unless they are ancestors to the transient.
   */
  if (*takes_focus &&
      meta_prefs_get_focus_new_windows () == G_DESKTOP_FOCUS_NEW_WINDOWS_STRICT &&
      !meta_window_is_ancestor_of_transient (window->display->focus_window,
                                             window))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "new window is not an ancestor to transient; not taking focus.");
      *takes_focus = FALSE;
      *places_on_top = FALSE;
    }

  switch (window->type)
    {
    case META_WINDOW_UTILITY:
    case META_WINDOW_TOOLBAR:
      *takes_focus = FALSE;
      *places_on_top = FALSE;
      break;
    case META_WINDOW_DOCK:
    case META_WINDOW_DESKTOP:
    case META_WINDOW_SPLASHSCREEN:
    case META_WINDOW_MENU:
    /* override redirect types: */
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_DND:
    case META_WINDOW_OVERRIDE_OTHER:
      /* don't focus any of these; places_on_top may be irrelevant for some of
       * these (e.g. dock)--but you never know--the focus window might also be
       * of the same type in some weird situation...
       */
      *takes_focus = FALSE;
      break;
    case META_WINDOW_NORMAL:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
      /* The default is correct for these */
      break;
    }
}

static gboolean
windows_overlap (const MetaWindow *w1, const MetaWindow *w2)
{
  MtkRectangle w1rect, w2rect;
  meta_window_get_frame_rect (w1, &w1rect);
  meta_window_get_frame_rect (w2, &w2rect);
  return mtk_rectangle_overlap (&w1rect, &w2rect);
}

/* Returns whether a new window would be covered by any
 * existing window on the same workspace that is set
 * to be "above" ("always on top").  A window that is not
 * set "above" would be underneath the new window anyway.
 *
 * We take "covered" to mean even partially covered, but
 * some people might prefer entirely covered.  I think it
 * is more useful to behave this way if any part of the
 * window is covered, because a partial coverage could be
 * (say) ninety per cent and almost indistinguishable from total.
 */
static gboolean
window_would_be_covered (const MetaWindow *newbie)
{
  MetaWorkspace *workspace = meta_window_get_workspace ((MetaWindow *)newbie);
  GList *tmp, *windows;

  windows = meta_workspace_list_windows (workspace);

  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;

      if (w->wm_state_above && w != newbie)
        {
          /* We have found a window that is "above". Perhaps it overlaps. */
          if (windows_overlap (w, newbie))
            {
              g_list_free (windows); /* clean up... */
              return TRUE; /* yes, it does */
            }
        }

      tmp = tmp->next;
    }

  g_list_free (windows);
  return FALSE; /* none found */
}

void
meta_window_force_placement (MetaWindow *window,
                             gboolean    force_move)
{
  MetaMoveResizeFlags flags;

  if (window->placed)
    return;

  /* We have to recalc the placement here since other windows may
   * have been mapped/placed since we last did constrain_position
   */

  /* calc_placement is an efficiency hack to avoid
   * multiple placement calculations before we finally
   * show the window.
   */
  window->calc_placement = TRUE;

  flags = (META_MOVE_RESIZE_MOVE_ACTION |
           META_MOVE_RESIZE_RESIZE_ACTION |
           META_MOVE_RESIZE_CONSTRAIN);
  if (force_move)
    flags |= META_MOVE_RESIZE_FORCE_MOVE;

  meta_window_move_resize_internal (window,
                                    flags,
                                    META_GRAVITY_NORTH_WEST,
                                    window->unconstrained_rect);
  window->calc_placement = FALSE;

  /* don't ever do the initial position constraint thing again.
   * This is toggled here so that initially-iconified windows
   * still get placed when they are ultimately shown.
   */
  window->placed = TRUE;

  /* Don't want to accidentally reuse the fact that we had been denied
   * focus in any future constraints unless we're denied focus again.
   */
  window->denied_focus_and_not_transient = FALSE;
}

static gboolean
enter_suspend_state_cb (gpointer user_data)
{
  MetaWindow *window = META_WINDOW (user_data);
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  priv->suspend_timoeut_id = 0;

  g_return_val_if_fail (priv->suspend_state == META_WINDOW_SUSPEND_STATE_HIDDEN,
                        G_SOURCE_REMOVE);

  priv->suspend_state = META_WINDOW_SUSPEND_STATE_SUSPENDED;
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_SUSPEND_STATE]);

  return G_SOURCE_REMOVE;
}

static void
set_hidden_suspended_state (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  priv->suspend_state = META_WINDOW_SUSPEND_STATE_HIDDEN;
  g_return_if_fail (!priv->suspend_timoeut_id);
  priv->suspend_timoeut_id =
    g_timeout_add_seconds (SUSPEND_HIDDEN_TIMEOUT_S,
                           enter_suspend_state_cb,
                           window);
}

static void
update_suspend_state (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  if (window->unmanaging)
    return;

  if (!window->hidden &&
      priv->suspend_state_inhibitors > 0)
    {
      priv->suspend_state = META_WINDOW_SUSPEND_STATE_ACTIVE;
      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_SUSPEND_STATE]);
      g_clear_handle_id (&priv->suspend_timoeut_id, g_source_remove);
    }
  else if (priv->suspend_state == META_WINDOW_SUSPEND_STATE_ACTIVE)
    {
      set_hidden_suspended_state (window);
      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_SUSPEND_STATE]);
    }
}

void
meta_window_inhibit_suspend_state (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  priv->suspend_state_inhibitors++;
  if (priv->suspend_state_inhibitors == 1)
    update_suspend_state (window);
}

void
meta_window_uninhibit_suspend_state (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  g_return_if_fail (priv->suspend_state_inhibitors > 0);

  priv->suspend_state_inhibitors--;
  if (priv->suspend_state_inhibitors == 0)
    update_suspend_state (window);
}

gboolean
meta_window_is_suspended (MetaWindow *window)
{
  MetaWindowPrivate *priv = meta_window_get_instance_private (window);

  switch (priv->suspend_state)
    {
    case META_WINDOW_SUSPEND_STATE_ACTIVE:
    case META_WINDOW_SUSPEND_STATE_HIDDEN:
      return FALSE;
    case META_WINDOW_SUSPEND_STATE_SUSPENDED:
      return TRUE;
    }

  g_assert_not_reached ();
}

static void
meta_window_show (MetaWindow *window)
{
  gboolean did_show;
  gboolean takes_focus_on_map;
  gboolean place_on_top_on_map;
  gboolean needs_stacking_adjustment;
  MetaWindow *focus_window;
  gboolean notify_demands_attention = FALSE;

  meta_topic (META_DEBUG_WINDOW_STATE,
              "Showing window %s, iconic: %d placed: %d",
              window->desc, window->iconic, window->placed);

  focus_window = window->display->focus_window;  /* May be NULL! */
  did_show = FALSE;
  window_state_on_map (window, &takes_focus_on_map, &place_on_top_on_map);
  needs_stacking_adjustment = FALSE;

  meta_topic (META_DEBUG_WINDOW_STATE,
              "Window %s %s focus on map, and %s place on top on map.",
              window->desc,
              takes_focus_on_map ? "does" : "does not",
              place_on_top_on_map ? "does" : "does not");

  /* Now, in some rare cases we should *not* put a new window on top.
   * These cases include certain types of windows showing for the first
   * time, and any window which would be covered because of another window
   * being set "above" ("always on top").
   *
   * FIXME: Although "place_on_top_on_map" and "takes_focus_on_map" are
   * generally based on the window type, there is a special case when the
   * focus window is a terminal for them both to be false; this should
   * probably rather be a term in the "if" condition below.
   */

  if ( focus_window != NULL && window->showing_for_first_time &&
      ( (!place_on_top_on_map && !takes_focus_on_map) ||
      window_would_be_covered (window) )
    ) {
      if (!meta_window_is_ancestor_of_transient (focus_window, window))
        {
          needs_stacking_adjustment = TRUE;
          if (!window->placed)
            window->denied_focus_and_not_transient = TRUE;
        }
    }

  if (!window->placed)
    {
      if (window->monitor &&
          meta_prefs_get_auto_maximize() &&
          window->showing_for_first_time &&
          window->has_maximize_func)
        {
          MtkRectangle work_area;
          meta_window_get_work_area_current_monitor (window, &work_area);
          /* Automaximize windows that map with a size > MAX_UNMAXIMIZED_WINDOW_AREA of the work area */
          if (window->rect.width * window->rect.height > work_area.width * work_area.height * MAX_UNMAXIMIZED_WINDOW_AREA)
            {
              window->maximize_horizontally_after_placement = TRUE;
              window->maximize_vertically_after_placement = TRUE;
            }
        }
      meta_window_force_placement (window, FALSE);
    }

  if (needs_stacking_adjustment)
    {
      gboolean overlap;

      /* This window isn't getting focus on map.  We may need to do some
       * special handing with it in regards to
       *   - the stacking of the window
       *   - the MRU position of the window
       *   - the demands attention setting of the window
       *
       * Firstly, set the flag so we don't give the window focus anyway
       * and confuse people.
       */

      takes_focus_on_map = FALSE;

      overlap = windows_overlap (window, focus_window);

      /* We want alt tab to go to the denied-focus window */
      ensure_mru_position_after (window, focus_window);

      /* We don't want the denied-focus window to obscure the focus
       * window, and if we're in both click-to-focus mode and
       * raise-on-click mode then we want to maintain the invariant
       * that MRU order == stacking order.  The need for this if
       * comes from the fact that in sloppy/mouse focus the focus
       * window may not overlap other windows and also can be
       * considered "below" them; this combination means that
       * placing the denied-focus window "below" the focus window
       * in the stack when it doesn't overlap it confusingly places
       * that new window below a lot of other windows.
       */
      if (overlap ||
          (meta_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK &&
           meta_prefs_get_raise_on_click ()))
        meta_window_stack_just_below (window, focus_window);

      /* If the window will be obscured by the focus window, then the
       * user might not notice the window appearing so set the
       * demands attention hint.
       *
       * We set the hint ourselves rather than calling
       * meta_window_set_demands_attention() because that would cause
       * a recalculation of overlap, and a call to set_net_wm_state()
       * which we are going to call ourselves here a few lines down.
       */
      if (overlap)
        {
          if (!window->wm_state_demands_attention)
            {
              window->wm_state_demands_attention = TRUE;
              notify_demands_attention = TRUE;
            }
        }
    }

  if (window->hidden)
    {
      meta_stack_freeze (window->display->stack);
      window->hidden = FALSE;
      meta_stack_thaw (window->display->stack);
      did_show = TRUE;
    }

  if (window->iconic)
    {
      window->iconic = FALSE;
      set_wm_state (window);
    }

  if (!window->visible_to_compositor && window_has_buffer (window))
    {
      MetaCompEffect effect = META_COMP_EFFECT_NONE;

      window->visible_to_compositor = TRUE;

      switch (window->pending_compositor_effect)
        {
        case META_COMP_EFFECT_CREATE:
        case META_COMP_EFFECT_UNMINIMIZE:
          effect = window->pending_compositor_effect;
          break;
        case META_COMP_EFFECT_NONE:
        case META_COMP_EFFECT_DESTROY:
        case META_COMP_EFFECT_MINIMIZE:
          break;
        }

      meta_compositor_show_window (window->display->compositor,
                                   window, effect);
      window->pending_compositor_effect = META_COMP_EFFECT_NONE;
    }

  /* We don't want to worry about all cases from inside
   * implement_showing(); we only want to worry about focus if this
   * window has not been shown before.
   */
  if (window->showing_for_first_time)
    {
      window->showing_for_first_time = FALSE;
      if (takes_focus_on_map)
        {
          guint32     timestamp;

          timestamp = meta_display_get_current_time_roundtrip (window->display);

          if (meta_display_windows_are_interactable (window->display))
            meta_window_focus (window, timestamp);
          else
            meta_display_queue_focus (window->display, window);
        }
    }

  set_net_wm_state (window);

  if (did_show && window->struts)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Mapped window %s with struts, so invalidating work areas",
                  window->desc);
      invalidate_work_areas (window);
    }

  if (did_show)
    meta_display_queue_check_fullscreen (window->display);

  /*
   * Now that we have shown the window, we no longer want to consider the
   * initial timestamp in any subsequent deliberations whether to focus this
   * window or not, so clear the flag.
   *
   * See http://bugzilla.gnome.org/show_bug.cgi?id=573922
   */
  window->initial_timestamp_set = FALSE;

  if (notify_demands_attention)
    {
      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_DEMANDS_ATTENTION]);
      g_signal_emit_by_name (window->display, "window-demands-attention",
                             window);
    }

  update_suspend_state (window);

  if (did_show)
    g_signal_emit (window, window_signals[SHOWN], 0);
}

static void
meta_window_hide (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  gboolean did_hide;

  meta_topic (META_DEBUG_WINDOW_STATE,
              "Hiding window %s", window->desc);

  if (window->visible_to_compositor)
    {
      MetaCompEffect effect = META_COMP_EFFECT_NONE;

      window->visible_to_compositor = FALSE;

      switch (window->pending_compositor_effect)
        {
        case META_COMP_EFFECT_CREATE:
        case META_COMP_EFFECT_UNMINIMIZE:
        case META_COMP_EFFECT_NONE:
          break;
        case META_COMP_EFFECT_DESTROY:
        case META_COMP_EFFECT_MINIMIZE:
          effect = window->pending_compositor_effect;
          break;
        }

      meta_compositor_hide_window (window->display->compositor, window, effect);
      window->pending_compositor_effect = META_COMP_EFFECT_NONE;
    }

  did_hide = FALSE;

  if (!window->hidden)
    {
      meta_stack_freeze (window->display->stack);
      window->hidden = TRUE;
      meta_stack_thaw (window->display->stack);

      did_hide = TRUE;
    }

  if (!window->iconic)
    {
      window->iconic = TRUE;
      set_wm_state (window);
    }

  set_net_wm_state (window);

  if (did_hide && window->struts)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Unmapped window %s with struts, so invalidating work areas",
                  window->desc);
      invalidate_work_areas (window);
    }

  if (window->has_focus)
    {
      MetaWindow *not_this_one = NULL;
      MetaWorkspace *my_workspace = meta_window_get_workspace (window);
      guint32 timestamp = meta_display_get_current_time_roundtrip (window->display);

      /*
       * If this window is modal, passing the not_this_one window to
       * _focus_default_window() makes the focus to be given to this window's
       * ancestor. This can only be the case if the window is on the currently
       * active workspace; when it is not, we need to pass in NULL, so as to
       * focus the default window for the active workspace (this scenario
       * arises when we are switching workspaces).
       * We also pass in NULL if we are in the process of hiding all non-desktop
       * windows to avoid unexpected changes to the stacking order.
       */
      if (my_workspace == workspace_manager->active_workspace &&
          !my_workspace->showing_desktop)
        not_this_one = window;

      meta_workspace_focus_default_window (workspace_manager->active_workspace,
                                           not_this_one,
                                           timestamp);
    }

  if (did_hide)
    meta_display_queue_check_fullscreen (window->display);

  update_suspend_state (window);
}

static gboolean
queue_calc_showing_func (MetaWindow *window,
                         void       *data)
{
  meta_window_queue(window, META_QUEUE_CALC_SHOWING);
  return TRUE;
}

void
meta_window_minimize (MetaWindow  *window)
{
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (!window->override_redirect);

  if (!window->has_minimize_func)
    {
      g_warning ("Window %s cannot be minimized, but something tried "
                 "anyways. Not having it!", window->desc);
      return;
    }

  if (!window->minimized)
    {
      window->minimized = TRUE;
      window->pending_compositor_effect = META_COMP_EFFECT_MINIMIZE;
      meta_window_queue(window, META_QUEUE_CALC_SHOWING);

      meta_window_foreach_transient (window,
                                     queue_calc_showing_func,
                                     NULL);

      if (window->has_focus)
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Focusing default window due to minimization of focus window %s",
                      window->desc);
        }
      else
        {
          meta_topic (META_DEBUG_FOCUS,
                      "Minimizing window %s which doesn't have the focus",
                      window->desc);
        }

      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MINIMIZED]);
    }
}

void
meta_window_unminimize (MetaWindow  *window)
{
  g_return_if_fail (!window->override_redirect);

  if (window->minimized)
    {
      window->minimized = FALSE;
      window->pending_compositor_effect = META_COMP_EFFECT_UNMINIMIZE;
      meta_window_queue(window, META_QUEUE_CALC_SHOWING);

      meta_window_foreach_transient (window,
                                     queue_calc_showing_func,
                                     NULL);

      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MINIMIZED]);
    }
}

static void
ensure_size_hints_satisfied (MtkRectangle        *rect,
                             const MetaSizeHints *size_hints)
{
  int minw, minh, maxw, maxh;   /* min/max width/height                      */
  int basew, baseh, winc, hinc; /* base width/height, width/height increment */
  int extra_width, extra_height;

  minw  = size_hints->min_width;  minh  = size_hints->min_height;
  maxw  = size_hints->max_width;  maxh  = size_hints->max_height;
  basew = size_hints->base_width; baseh = size_hints->base_height;
  winc  = size_hints->width_inc;  hinc  = size_hints->height_inc;

  /* First, enforce min/max size constraints */
  rect->width  = CLAMP (rect->width,  minw, maxw);
  rect->height = CLAMP (rect->height, minh, maxh);

  /* Now, verify size increment constraints are satisfied, or make them be */
  extra_width  = (rect->width  - basew) % winc;
  extra_height = (rect->height - baseh) % hinc;

  rect->width  -= extra_width;
  rect->height -= extra_height;

  /* Adjusting width/height down, as done above, may violate minimum size
   * constraints, so one last fix.
   */
  if (rect->width  < minw)
    rect->width  += ((minw - rect->width)/winc  + 1)*winc;
  if (rect->height < minh)
    rect->height += ((minh - rect->height)/hinc + 1)*hinc;
}

static void
meta_window_save_rect (MetaWindow *window)
{
  if (!(META_WINDOW_MAXIMIZED (window) || META_WINDOW_TILED_SIDE_BY_SIDE (window) || window->fullscreen))
    {
      /* save size/pos as appropriate args for move_resize */
      if (!window->maximized_horizontally)
        {
          window->saved_rect.x      = window->rect.x;
          window->saved_rect.width  = window->rect.width;
        }
      if (!window->maximized_vertically)
        {
          window->saved_rect.y      = window->rect.y;
          window->saved_rect.height = window->rect.height;
        }
    }
}

void
meta_window_maximize_internal (MetaWindow        *window,
                               MetaMaximizeFlags  directions,
                               MtkRectangle      *saved_rect)
{
  /* At least one of the two directions ought to be set */
  gboolean maximize_horizontally, maximize_vertically;
  maximize_horizontally = directions & META_MAXIMIZE_HORIZONTAL;
  maximize_vertically   = directions & META_MAXIMIZE_VERTICAL;
  g_assert (maximize_horizontally || maximize_vertically);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Maximizing %s%s",
              window->desc,
              maximize_horizontally && maximize_vertically ? "" :
                maximize_horizontally ? " horizontally" :
                  maximize_vertically ? " vertically" : "BUGGGGG");

  if (saved_rect != NULL)
    window->saved_rect = *saved_rect;
  else
    meta_window_save_rect (window);

  if (maximize_horizontally && maximize_vertically)
    window->saved_maximize = TRUE;

  window->maximized_horizontally =
    window->maximized_horizontally || maximize_horizontally;
  window->maximized_vertically =
    window->maximized_vertically   || maximize_vertically;

  /* Update the edge constraints */
  update_edge_constraints (window);

  meta_window_recalc_features (window);
  set_net_wm_state (window);

  if (window->monitor && window->monitor->in_fullscreen)
    meta_display_queue_check_fullscreen (window->display);

  g_object_freeze_notify (G_OBJECT (window));
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MAXIMIZED_HORIZONTALLY]);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MAXIMIZED_VERTICALLY]);
  g_object_thaw_notify (G_OBJECT (window));
}

void
meta_window_maximize (MetaWindow        *window,
                      MetaMaximizeFlags  directions)
{
  MtkRectangle *saved_rect = NULL;
  gboolean maximize_horizontally, maximize_vertically;

  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (!window->override_redirect);

  /* At least one of the two directions ought to be set */
  maximize_horizontally = directions & META_MAXIMIZE_HORIZONTAL;
  maximize_vertically   = directions & META_MAXIMIZE_VERTICAL;
  g_assert (maximize_horizontally || maximize_vertically);

  /* Only do something if the window isn't already maximized in the
   * given direction(s).
   */
  if ((maximize_horizontally && !window->maximized_horizontally) ||
      (maximize_vertically   && !window->maximized_vertically))
    {
      /* if the window hasn't been placed yet, we'll maximize it then
       */
      if (!window->placed)
        {
          window->maximize_horizontally_after_placement =
            window->maximize_horizontally_after_placement ||
            maximize_horizontally;
          window->maximize_vertically_after_placement =
            window->maximize_vertically_after_placement ||
            maximize_vertically;
          return;
        }

      if (window->tile_mode != META_TILE_NONE)
        {
          saved_rect = &window->saved_rect;

          window->maximized_vertically = FALSE;
          window->tile_mode = META_TILE_NONE;
        }

      meta_window_maximize_internal (window,
                                     directions,
                                     saved_rect);

      MtkRectangle old_frame_rect, old_buffer_rect;

      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      meta_compositor_size_change_window (window->display->compositor, window,
                                          META_SIZE_CHANGE_MAXIMIZE,
                                          &old_frame_rect, &old_buffer_rect);

      meta_window_move_resize_internal (window,
                                        (META_MOVE_RESIZE_MOVE_ACTION |
                                         META_MOVE_RESIZE_RESIZE_ACTION |
                                         META_MOVE_RESIZE_STATE_CHANGED |
                                         META_MOVE_RESIZE_CONSTRAIN),
                                        META_GRAVITY_NORTH_WEST,
                                        window->unconstrained_rect);
    }
}

/**
 * meta_window_get_maximized:
 * @window: a #MetaWindow
 *
 * Gets the current maximization state of the window, as combination
 * of the %META_MAXIMIZE_HORIZONTAL and %META_MAXIMIZE_VERTICAL flags;
 *
 * Return value: current maximization state
 */
MetaMaximizeFlags
meta_window_get_maximized (MetaWindow *window)
{
  return ((window->maximized_horizontally ? META_MAXIMIZE_HORIZONTAL : 0) |
          (window->maximized_vertically ? META_MAXIMIZE_VERTICAL : 0));
}

/**
 * meta_window_is_fullscreen:
 * @window: a #MetaWindow
 *
 * Return value: %TRUE if the window is currently fullscreen
 */
gboolean
meta_window_is_fullscreen (MetaWindow *window)
{
  return window->fullscreen;
}

/**
 * meta_window_is_screen_sized:
 * @window: A #MetaWindow
 *
 * Return value: %TRUE if the window is occupies the
 *               the whole screen (all monitors).
 */
gboolean
meta_window_is_screen_sized (MetaWindow *window)
{
  MtkRectangle window_rect;
  int screen_width, screen_height;

  meta_display_get_size (window->display, &screen_width, &screen_height);
  meta_window_get_frame_rect (window, &window_rect);

  if (window_rect.x == 0 && window_rect.y == 0 &&
      window_rect.width == screen_width && window_rect.height == screen_height)
    return TRUE;

  return FALSE;
}

/**
 * meta_window_is_monitor_sized:
 * @window: a #MetaWindow
 *
 * Return value: %TRUE if the window is occupies an entire monitor or
 *               the whole screen.
 */
gboolean
meta_window_is_monitor_sized (MetaWindow *window)
{
  if (!window->monitor)
    return FALSE;

  if (window->fullscreen)
    return TRUE;

  if (meta_window_is_screen_sized (window))
    return TRUE;

  if (window->override_redirect)
    {
      MtkRectangle window_rect, monitor_rect;

      meta_window_get_frame_rect (window, &window_rect);
      meta_display_get_monitor_geometry (window->display, window->monitor->number, &monitor_rect);

      if (mtk_rectangle_equal (&window_rect, &monitor_rect))
        return TRUE;
    }

  return FALSE;
}

/**
 * meta_window_is_on_primary_monitor:
 * @window: a #MetaWindow
 *
 * Return value: %TRUE if the window is on the primary monitor
 */
gboolean
meta_window_is_on_primary_monitor (MetaWindow *window)
{
  g_return_val_if_fail (window->monitor, FALSE);

  return window->monitor->is_primary;
}

static void
meta_window_get_tile_fraction (MetaWindow   *window,
                               MetaTileMode  tile_mode,
                               double       *fraction)
{
  MetaWindow *tile_match;

  /* Make sure the tile match is up-to-date and matches the
   * passed in mode rather than the current state
   */
  tile_match = meta_window_find_tile_match (window, tile_mode);

  if (tile_mode == META_TILE_NONE)
    *fraction = -1.;
  else if (tile_mode == META_TILE_MAXIMIZED)
    *fraction = 1.;
  else if (tile_match)
    *fraction = 1. - tile_match->tile_hfraction;
  else if (META_WINDOW_TILED_SIDE_BY_SIDE (window))
    {
      if (window->tile_mode != tile_mode)
        *fraction = 1. - window->tile_hfraction;
      else
        *fraction = window->tile_hfraction;
    }
  else
    *fraction = .5;
}

static void
meta_window_update_tile_fraction (MetaWindow *window,
                                  int         new_w,
                                  int         new_h)
{
  MetaWindow *tile_match = window->tile_match;
  MtkRectangle work_area;
  MetaWindowDrag *window_drag;

  if (!META_WINDOW_TILED_SIDE_BY_SIDE (window))
    return;

  meta_window_get_work_area_for_monitor (window,
                                         window->tile_monitor_number,
                                         &work_area);
  window->tile_hfraction = (double)new_w / work_area.width;

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);

  if (tile_match &&
      window_drag &&
      meta_window_drag_get_window (window_drag) == window)
    meta_window_tile (tile_match, tile_match->tile_mode);
}

static void
update_edge_constraints (MetaWindow *window)
{
  switch (window->tile_mode)
    {
    case META_TILE_NONE:
      window->edge_constraints.top = META_EDGE_CONSTRAINT_NONE;
      window->edge_constraints.right = META_EDGE_CONSTRAINT_NONE;
      window->edge_constraints.bottom = META_EDGE_CONSTRAINT_NONE;
      window->edge_constraints.left = META_EDGE_CONSTRAINT_NONE;
      break;

    case META_TILE_MAXIMIZED:
      window->edge_constraints.top = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.right = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.bottom = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.left = META_EDGE_CONSTRAINT_MONITOR;
      break;

    case META_TILE_LEFT:
      window->edge_constraints.top = META_EDGE_CONSTRAINT_MONITOR;

      if (window->tile_match)
        window->edge_constraints.right = META_EDGE_CONSTRAINT_WINDOW;
      else
        window->edge_constraints.right = META_EDGE_CONSTRAINT_NONE;

      window->edge_constraints.bottom = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.left = META_EDGE_CONSTRAINT_MONITOR;
      break;

    case META_TILE_RIGHT:
      window->edge_constraints.top = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.right = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.bottom = META_EDGE_CONSTRAINT_MONITOR;

      if (window->tile_match)
        window->edge_constraints.left = META_EDGE_CONSTRAINT_WINDOW;
      else
        window->edge_constraints.left = META_EDGE_CONSTRAINT_NONE;
      break;
    }

  /* h/vmaximize also modify the edge constraints */
  if (window->maximized_vertically)
    {
      window->edge_constraints.top = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.bottom = META_EDGE_CONSTRAINT_MONITOR;
    }

  if (window->maximized_horizontally)
    {
      window->edge_constraints.right = META_EDGE_CONSTRAINT_MONITOR;
      window->edge_constraints.left = META_EDGE_CONSTRAINT_MONITOR;
    }
}

void
meta_window_untile (MetaWindow *window)
{
  g_return_if_fail (META_IS_WINDOW (window));

  window->tile_monitor_number =
    window->saved_maximize ? window->monitor->number
                           : -1;
  window->tile_mode =
    window->saved_maximize ? META_TILE_MAXIMIZED
                           : META_TILE_NONE;

  if (window->saved_maximize)
    meta_window_maximize (window, META_MAXIMIZE_BOTH);
  else
    meta_window_unmaximize (window, META_MAXIMIZE_BOTH);
}

void
meta_window_tile (MetaWindow   *window,
                  MetaTileMode  tile_mode)
{
  MetaMaximizeFlags directions;
  MetaWindowDrag *window_drag;

  g_return_if_fail (META_IS_WINDOW (window));

  meta_window_get_tile_fraction (window, tile_mode, &window->tile_hfraction);
  window->tile_mode = tile_mode;

  /* Don't do anything if no tiling is requested */
  if (window->tile_mode == META_TILE_NONE)
    {
      window->tile_monitor_number = -1;
      return;
    }
  else if (window->tile_monitor_number < 0)
    {
      window->tile_monitor_number = window->monitor->number;
    }

  if (window->tile_mode == META_TILE_MAXIMIZED)
    directions = META_MAXIMIZE_BOTH;
  else
    directions = META_MAXIMIZE_VERTICAL;

  meta_window_maximize_internal (window, directions, NULL);

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);

  if (!window->tile_match ||
      !window_drag ||
      window->tile_match != meta_window_drag_get_window (window_drag))
    {
      MtkRectangle old_frame_rect, old_buffer_rect;

      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      meta_compositor_size_change_window (window->display->compositor, window,
                                          META_SIZE_CHANGE_MAXIMIZE,
                                          &old_frame_rect, &old_buffer_rect);
    }

  meta_window_move_resize_internal (window,
                                    (META_MOVE_RESIZE_MOVE_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_STATE_CHANGED |
                                     META_MOVE_RESIZE_CONSTRAIN),
                                    META_GRAVITY_NORTH_WEST,
                                    window->unconstrained_rect);
}

void
meta_window_restore_tile (MetaWindow   *window,
                          MetaTileMode  mode,
                          int           width,
                          int           height)
{
  meta_window_update_tile_fraction (window, width, height);
  meta_window_tile (window, mode);
}

static gboolean
meta_window_can_tile_maximized (MetaWindow *window)
{
  return window->has_maximize_func;
}

gboolean
meta_window_can_tile_side_by_side (MetaWindow *window,
                                   int         monitor_number)
{
  MtkRectangle tile_area;
  MtkRectangle client_rect;

  if (!meta_window_can_tile_maximized (window))
    return FALSE;

  meta_window_get_work_area_for_monitor (window, monitor_number, &tile_area);

  /* Do not allow tiling in portrait orientation */
  if (tile_area.height > tile_area.width)
    return FALSE;

  tile_area.width /= 2;

  meta_window_frame_rect_to_client_rect (window, &tile_area, &client_rect);

  return client_rect.width >= window->size_hints.min_width &&
         client_rect.height >= window->size_hints.min_height;
}

static void
unmaximize_window_before_freeing (MetaWindow        *window)
{
  meta_topic (META_DEBUG_WINDOW_OPS,
              "Unmaximizing %s just before freeing",
              window->desc);

  window->maximized_horizontally = FALSE;
  window->maximized_vertically = FALSE;

  if (window->withdrawn)                /* See bug #137185 */
    {
      window->rect = window->saved_rect;
      set_net_wm_state (window);
    }
#ifdef HAVE_WAYLAND
  else if (!meta_is_wayland_compositor ())
    {
      /* Do NOT update net_wm_state: this screen is closing,
       * it likely will be managed by another window manager
       * that will need the current _NET_WM_STATE atoms.
       * Moreover, it will need to know the unmaximized geometry,
       * therefore move_resize the window to saved_rect here
       * before closing it. */
      meta_window_move_resize_frame (window,
                                     FALSE,
                                     window->saved_rect.x,
                                     window->saved_rect.y,
                                     window->saved_rect.width,
                                     window->saved_rect.height);
    }
#endif
}

void
meta_window_maybe_apply_size_hints (MetaWindow   *window,
                                    MtkRectangle *target_rect)
{
  meta_window_frame_rect_to_client_rect (window, target_rect, target_rect);
  ensure_size_hints_satisfied (target_rect, &window->size_hints);
  meta_window_client_rect_to_frame_rect (window, target_rect, target_rect);
}

void
meta_window_unmaximize (MetaWindow        *window,
                        MetaMaximizeFlags  directions)
{
  gboolean unmaximize_horizontally, unmaximize_vertically;

  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (!window->override_redirect);

  /* At least one of the two directions ought to be set */
  unmaximize_horizontally = directions & META_MAXIMIZE_HORIZONTAL;
  unmaximize_vertically   = directions & META_MAXIMIZE_VERTICAL;
  g_assert (unmaximize_horizontally || unmaximize_vertically);

  if (unmaximize_horizontally && unmaximize_vertically)
    window->saved_maximize = FALSE;

  /* Only do something if the window isn't already maximized in the
   * given direction(s).
   */
  if ((unmaximize_horizontally && window->maximized_horizontally) ||
      (unmaximize_vertically   && window->maximized_vertically))
    {
      MtkRectangle *desired_rect;
      MtkRectangle target_rect;
      MtkRectangle work_area;
      MtkRectangle old_frame_rect, old_buffer_rect;
      gboolean has_target_size;

      meta_window_get_work_area_current_monitor (window, &work_area);
      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      if (unmaximize_vertically)
        window->tile_mode = META_TILE_NONE;

      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Unmaximizing %s%s",
                  window->desc,
                  unmaximize_horizontally && unmaximize_vertically ? "" :
                    unmaximize_horizontally ? " horizontally" :
                      unmaximize_vertically ? " vertically" : "BUGGGGG");

      window->maximized_horizontally =
        window->maximized_horizontally && !unmaximize_horizontally;
      window->maximized_vertically =
        window->maximized_vertically   && !unmaximize_vertically;

      /* Update the edge constraints */
      update_edge_constraints (window);

      /* recalc_features() will eventually clear the cached frame
       * extents, but we need the correct frame extents in the code below,
       * so invalidate the old frame extents manually up front.
       */
      meta_window_frame_size_changed (window);

      desired_rect = &window->saved_rect;

      /* Unmaximize to the saved_rect position in the direction(s)
       * being unmaximized.
       */
      target_rect = old_frame_rect;

      /* Avoid unmaximizing to "almost maximized" size when the previous size
       * is greater then 80% of the work area use MAX_UNMAXIMIZED_WINDOW_AREA of the work area as upper limit
       * while maintaining the aspect ratio.
       */
      if (unmaximize_horizontally && unmaximize_vertically &&
          desired_rect->width * desired_rect->height > work_area.width * work_area.height * MAX_UNMAXIMIZED_WINDOW_AREA)
        {
          if (desired_rect->width > desired_rect->height)
            {
              float aspect = (float)desired_rect->height / (float)desired_rect->width;
              desired_rect->width = MAX (work_area.width * sqrt (MAX_UNMAXIMIZED_WINDOW_AREA), window->size_hints.min_width);
              desired_rect->height = MAX (desired_rect->width * aspect, window->size_hints.min_height);
            }
          else
            {
              float aspect = (float)desired_rect->width / (float)desired_rect->height;
              desired_rect->height = MAX (work_area.height * sqrt (MAX_UNMAXIMIZED_WINDOW_AREA), window->size_hints.min_height);
              desired_rect->width = MAX (desired_rect->height * aspect, window->size_hints.min_width);
            }
        }

      if (unmaximize_horizontally)
        {
          target_rect.x     = desired_rect->x;
          target_rect.width = desired_rect->width;
        }
      if (unmaximize_vertically)
        {
          target_rect.y      = desired_rect->y;
          target_rect.height = desired_rect->height;
        }

      /* Window's size hints may have changed while maximized, making
       * saved_rect invalid.  #329152
       * Do not enforce limits, if no previous 'saved_rect' has been stored.
       */
      has_target_size = (target_rect.width > 0 && target_rect.height > 0);
      if (has_target_size)
        meta_window_maybe_apply_size_hints (window, &target_rect);

      meta_compositor_size_change_window (window->display->compositor, window,
                                          META_SIZE_CHANGE_UNMAXIMIZE,
                                          &old_frame_rect, &old_buffer_rect);

      meta_window_move_resize_internal (window,
                                        (META_MOVE_RESIZE_MOVE_ACTION |
                                         META_MOVE_RESIZE_RESIZE_ACTION |
                                         META_MOVE_RESIZE_STATE_CHANGED |
                                         META_MOVE_RESIZE_UNMAXIMIZE),
                                        META_GRAVITY_NORTH_WEST,
                                        target_rect);

      meta_window_recalc_features (window);
      set_net_wm_state (window);
      if (!window->monitor->in_fullscreen)
        meta_display_queue_check_fullscreen (window->display);
    }

  g_object_freeze_notify (G_OBJECT (window));
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MAXIMIZED_HORIZONTALLY]);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_MAXIMIZED_VERTICALLY]);
  g_object_thaw_notify (G_OBJECT (window));
}

void
meta_window_make_above (MetaWindow  *window)
{
  g_return_if_fail (!window->override_redirect);

  meta_window_set_above (window, TRUE);
  meta_window_raise (window);
}

void
meta_window_unmake_above (MetaWindow  *window)
{
  g_return_if_fail (!window->override_redirect);

  meta_window_set_above (window, FALSE);
  meta_window_raise (window);
}

static void
meta_window_set_above (MetaWindow *window,
                       gboolean    new_value)
{
  new_value = new_value != FALSE;
  if (new_value == window->wm_state_above)
    return;

  window->wm_state_above = new_value;
  meta_window_update_layer (window);
  set_net_wm_state (window);
  meta_window_frame_size_changed (window);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_ABOVE]);
}

void
meta_window_make_fullscreen_internal (MetaWindow  *window)
{
  if (!window->fullscreen)
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Fullscreening %s", window->desc);

      window->saved_rect_fullscreen = window->rect;

      window->fullscreen = TRUE;

      meta_stack_freeze (window->display->stack);

      meta_window_raise (window);
      meta_stack_thaw (window->display->stack);

      meta_window_recalc_features (window);
      set_net_wm_state (window);

      /* For the auto-minimize feature, if we fail to get focus */
      meta_display_queue_check_fullscreen (window->display);

      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_FULLSCREEN]);
    }
}

void
meta_window_make_fullscreen (MetaWindow  *window)
{
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (!window->override_redirect);

  if (!window->fullscreen)
    {
      MtkRectangle old_frame_rect, old_buffer_rect;

      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      meta_compositor_size_change_window (window->display->compositor,
                                          window, META_SIZE_CHANGE_FULLSCREEN,
                                          &old_frame_rect, &old_buffer_rect);

      meta_window_make_fullscreen_internal (window);
      meta_window_move_resize_internal (window,
                                        (META_MOVE_RESIZE_MOVE_ACTION |
                                         META_MOVE_RESIZE_RESIZE_ACTION |
                                         META_MOVE_RESIZE_STATE_CHANGED |
                                         META_MOVE_RESIZE_CONSTRAIN),
                                        META_GRAVITY_NORTH_WEST,
                                        window->unconstrained_rect);
    }
}

void
meta_window_unmake_fullscreen (MetaWindow  *window)
{
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (!window->override_redirect);

  if (window->fullscreen)
    {
      MtkRectangle old_frame_rect, old_buffer_rect, target_rect;
      gboolean has_target_size;

      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Unfullscreening %s", window->desc);

      window->fullscreen = FALSE;
      target_rect = window->saved_rect_fullscreen;

      meta_window_frame_size_changed (window);
      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      /* Window's size hints may have changed while maximized, making
       * saved_rect invalid.  #329152
       * Do not enforce limits, if no previous 'saved_rect' has been stored.
       */
      has_target_size = (target_rect.width > 0 && target_rect.height > 0);
      if (has_target_size)
        meta_window_maybe_apply_size_hints (window, &target_rect);

      /* Need to update window->has_resize_func before we move_resize()
       */
      meta_window_recalc_features (window);
      set_net_wm_state (window);

      meta_compositor_size_change_window (window->display->compositor,
                                          window, META_SIZE_CHANGE_UNFULLSCREEN,
                                          &old_frame_rect, &old_buffer_rect);

      meta_window_move_resize_internal (window,
                                        (META_MOVE_RESIZE_MOVE_ACTION |
                                         META_MOVE_RESIZE_RESIZE_ACTION |
                                         META_MOVE_RESIZE_STATE_CHANGED |
                                         META_MOVE_RESIZE_UNFULLSCREEN),
                                        META_GRAVITY_NORTH_WEST,
                                        target_rect);

      meta_display_queue_check_fullscreen (window->display);

      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_FULLSCREEN]);
    }
}

static void
meta_window_clear_fullscreen_monitors (MetaWindow *window)
{
  window->fullscreen_monitors.top = NULL;
  window->fullscreen_monitors.bottom = NULL;
  window->fullscreen_monitors.left = NULL;
  window->fullscreen_monitors.right = NULL;
}

void
meta_window_update_fullscreen_monitors (MetaWindow         *window,
                                        MetaLogicalMonitor *top,
                                        MetaLogicalMonitor *bottom,
                                        MetaLogicalMonitor *left,
                                        MetaLogicalMonitor *right)
{
  if (top && bottom && left && right)
    {
      window->fullscreen_monitors.top = top;
      window->fullscreen_monitors.bottom = bottom;
      window->fullscreen_monitors.left = left;
      window->fullscreen_monitors.right = right;
    }
  else
    {
      meta_window_clear_fullscreen_monitors (window);
    }

  if (window->fullscreen)
    {
      meta_window_queue(window, META_QUEUE_MOVE_RESIZE);
    }
}

gboolean
meta_window_has_fullscreen_monitors (MetaWindow *window)
{
  return window->fullscreen_monitors.top != NULL;
}

void
meta_window_adjust_fullscreen_monitor_rect (MetaWindow   *window,
                                            MtkRectangle *monitor_rect)
{
  MetaWindowClass *window_class = META_WINDOW_GET_CLASS (window);

  if (window_class->adjust_fullscreen_monitor_rect)
    window_class->adjust_fullscreen_monitor_rect (window, monitor_rect);
}

static gboolean
unminimize_func (MetaWindow *window,
                 void       *data)
{
  meta_window_unminimize (window);
  return TRUE;
}

static void
unminimize_window_and_all_transient_parents (MetaWindow *window)
{
  meta_window_unminimize (window);
  meta_window_foreach_ancestor (window, unminimize_func, NULL);
}

void
meta_window_activate_full (MetaWindow     *window,
                           guint32         timestamp,
                           MetaClientType  source_indication,
                           MetaWorkspace  *workspace)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  gboolean allow_workspace_switch;

  if (window->unmanaging)
    {
      g_warning ("Trying to activate unmanaged window '%s'", window->desc);
      return;
    }

  meta_topic (META_DEBUG_FOCUS,
              "_NET_ACTIVE_WINDOW message sent for %s at time %u "
              "by client type %u.",
              window->desc, timestamp, source_indication);

  allow_workspace_switch = (timestamp != 0);
  if (timestamp != 0 &&
      XSERVER_TIME_IS_BEFORE (timestamp, window->display->last_user_time))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "last_user_time (%u) is more recent; ignoring "
                  " _NET_ACTIVE_WINDOW message.",
                  window->display->last_user_time);
      meta_window_set_demands_attention(window);
      return;
    }

  if (timestamp == 0)
    timestamp = meta_display_get_current_time_roundtrip (window->display);

  meta_window_set_user_time (window, timestamp);

  /* disable show desktop mode unless we're a desktop component */
  maybe_leave_show_desktop_mode (window);

  /* Get window on current or given workspace */
  if (workspace == NULL)
    workspace = workspace_manager->active_workspace;

  /* For non-transient windows, we just set up a pulsing indicator,
     rather than move windows or workspaces.
     See http://bugzilla.gnome.org/show_bug.cgi?id=482354 */
  if (window->transient_for == NULL &&
      !allow_workspace_switch &&
      !meta_window_located_on_workspace (window, workspace))
    {
      meta_window_set_demands_attention (window);
      /* We've marked it as demanding, don't need to do anything else. */
      return;
    }
  else if (window->transient_for != NULL)
    {
      /* Move transients to current workspace - preference dialogs should appear over
         the source window.  */
      meta_window_change_workspace (window, workspace);
    }

  unminimize_window_and_all_transient_parents (window);

  if (meta_prefs_get_raise_on_click () ||
      source_indication == META_CLIENT_TYPE_PAGER)
    meta_window_raise (window);

  meta_topic (META_DEBUG_FOCUS,
              "Focusing window %s due to activation",
              window->desc);

  if (meta_window_located_on_workspace (window, workspace))
    meta_window_focus (window, timestamp);
  else
    meta_workspace_activate_with_focus (window->workspace, window, timestamp);

  meta_window_check_alive (window, timestamp);
}

/* This function exists since most of the functionality in window_activate
 * is useful for Mutter, but Mutter shouldn't need to specify a client
 * type for itself.  ;-)
 */
void
meta_window_activate (MetaWindow     *window,
                      guint32         timestamp)
{
  g_return_if_fail (!window->override_redirect);

  /* We're not really a pager, but the behavior we want is the same as if
   * we were such.  If we change the pager behavior later, we could revisit
   * this and just add extra flags to window_activate.
   */
  meta_window_activate_full (window, timestamp, META_CLIENT_TYPE_PAGER, NULL);
}

void
meta_window_activate_with_workspace (MetaWindow     *window,
                                     guint32         timestamp,
                                     MetaWorkspace  *workspace)
{
  g_return_if_fail (!window->override_redirect);

  meta_window_activate_full (window, timestamp, META_CLIENT_TYPE_APPLICATION, workspace);
}

/**
 * meta_window_updates_are_frozen:
 * @window: a #MetaWindow
 *
 * Gets whether the compositor should be updating the window contents;
 * window content updates may be frozen at client request by setting
 * an odd value in the extended _NET_WM_SYNC_REQUEST_COUNTER counter
 * by the window manager during a resize operation while waiting for
 * the client to redraw.
 *
 * Return value: %TRUE if updates are currently frozen
 */
gboolean
meta_window_updates_are_frozen (MetaWindow *window)
{
  return META_WINDOW_GET_CLASS (window)->are_updates_frozen (window);
}

static void
meta_window_reposition (MetaWindow *window)
{
  meta_window_move_resize_internal (window,
                                    (META_MOVE_RESIZE_MOVE_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_CONSTRAIN),
                                    META_GRAVITY_NORTH_WEST,
                                    window->rect);
}

static gboolean
maybe_move_attached_window (MetaWindow *window,
                            void       *data)
{
  if (window->hidden)
    return G_SOURCE_CONTINUE;

  if (meta_window_is_attached_dialog (window) ||
      meta_window_get_placement_rule (window))
    meta_window_reposition (window);

  return G_SOURCE_CONTINUE;
}

/**
 * meta_window_get_monitor:
 * @window: a #MetaWindow
 *
 * Gets index of the monitor that this window is on.
 *
 * Return Value: The index of the monitor in the screens monitor list, or -1
 * if the window has been recently unmanaged and does not have a monitor.
 */
int
meta_window_get_monitor (MetaWindow *window)
{
  if (!window->monitor)
    return -1;

  return window->monitor->number;
}

MetaLogicalMonitor *
meta_window_get_main_logical_monitor (MetaWindow *window)
{
  return window->monitor;
}

MetaLogicalMonitor *
meta_window_get_highest_scale_monitor (MetaWindow *window)
{
  return window->highest_scale_monitor;
}

static MetaLogicalMonitor *
find_monitor_by_winsys_id (MetaWindow *window,
                           uint64_t    winsys_id)
{
  MetaBackend *backend = backend_from_window (window);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      if (logical_monitor->winsys_id == winsys_id)
        return logical_monitor;
    }

  return NULL;
}

MetaLogicalMonitor *
meta_window_find_monitor_from_id (MetaWindow *window)
{
  MetaContext *context = meta_display_get_context (window->display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *old_monitor = window->monitor;
  MetaLogicalMonitor *new_monitor;

  new_monitor = find_monitor_by_winsys_id (window,
                                           window->preferred_output_winsys_id);

  if (old_monitor && !new_monitor)
    new_monitor = find_monitor_by_winsys_id (window, old_monitor->winsys_id);

  if (!new_monitor)
    {
      new_monitor =
        meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
    }

  return new_monitor;
}

/* This is called when the monitor setup has changed. The window->monitor
 * reference is still "valid", but refer to the previous monitor setup */
void
meta_window_update_for_monitors_changed (MetaWindow *window)
{
  MetaContext *context = meta_display_get_context (window->display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  const MetaLogicalMonitor *old, *new;

  if (meta_window_has_fullscreen_monitors (window))
    meta_window_clear_fullscreen_monitors (window);

  if (window->override_redirect || window->type == META_WINDOW_DESKTOP)
    {
      meta_window_update_monitor (window,
                                  META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE);
      goto out;
    }

  old = window->monitor;
  new = meta_window_find_monitor_from_id (window);

  if (window->tile_mode != META_TILE_NONE)
    {
      if (new)
        window->tile_monitor_number = new->number;
      else
        window->tile_monitor_number = -1;
    }

  if (new && old)
    {
      /* This will eventually reach meta_window_update_monitor that
       * will send leave/enter-monitor events. The old != new monitor
       * check will always fail (due to the new logical_monitors set) so
       * we will always send the events, even if the new and old monitor
       * index is the same. That is right, since the enumeration of the
       * monitors changed and the same index could be refereing
       * to a different monitor. */
      meta_window_move_between_rects (window,
                                      META_MOVE_RESIZE_FORCE_UPDATE_MONITOR,
                                      &old->rect,
                                      &new->rect);
    }
  else
    {
      meta_window_update_monitor (window,
                                  META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE);
    }

out:
  g_assert (!window->monitor ||
            g_list_find (meta_monitor_manager_get_logical_monitors (monitor_manager),
                         window->monitor));
}

void
meta_window_update_monitor (MetaWindow                   *window,
                            MetaWindowUpdateMonitorFlags  flags)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  const MetaLogicalMonitor *old, *old_highest_scale;

  old = window->monitor;
  META_WINDOW_GET_CLASS (window)->update_main_monitor (window, flags);
  if (old != window->monitor)
    {
      meta_window_on_all_workspaces_changed (window);

      /* If workspaces only on primary and we moved back to primary due to a user action,
       * ensure that the window is now in that workspace. We do this because while
       * the window is on a non-primary monitor it is always visible, so it would be
       * very jarring if it disappeared when it crossed the monitor border.
       * The one time we want it to both change to the primary monitor and a non-active
       * workspace is when dropping the window on some other workspace thumbnail directly.
       * That should be handled by explicitly moving the window before changing the
       * workspace.
       */
      if (meta_prefs_get_workspaces_only_on_primary () &&
          flags & META_WINDOW_UPDATE_MONITOR_FLAGS_USER_OP &&
          meta_window_is_on_primary_monitor (window)  &&
          workspace_manager->active_workspace != window->workspace)
        meta_window_change_workspace (window, workspace_manager->active_workspace);

      meta_window_main_monitor_changed (window, old);

      /* If we're changing monitors, we need to update the has_maximize_func flag,
       * as the working area has changed. */
      meta_window_recalc_features (window);

      meta_display_queue_check_fullscreen (window->display);
    }

  old_highest_scale = window->highest_scale_monitor;

  window->highest_scale_monitor = window->rect.width > 0 && window->rect.height > 0
    ? meta_window_find_highest_scale_monitor_from_frame_rect (window)
    : window->monitor;

  if (old_highest_scale != window->highest_scale_monitor)
    g_signal_emit (window, window_signals[HIGHEST_SCALE_MONITOR_CHANGED], 0);
}

void
meta_window_move_resize_internal (MetaWindow          *window,
                                  MetaMoveResizeFlags  flags,
                                  MetaGravity          gravity,
                                  MtkRectangle         frame_rect)
{
  /* The rectangle here that's passed in *always* in "frame rect"
   * coordinates. That means the position of the frame's visible bounds,
   * with x and y being absolute (root window) coordinates.
   *
   * For an X11 framed window, the client window's server rectangle is
   * inset from this rectangle by the frame's visible borders, and the
   * frame window's server rectangle is outset by the invisible borders.
   *
   * For an X11 unframed window, the rectangle here directly matches
   * the server's rectangle, since the visible and invisible borders
   * are both 0.
   *
   * For an X11 CSD window, the client window's server rectangle is
   * outset from this rectagle by the client-specified frame extents.
   *
   * For a Wayland window, this rectangle can simply be sent directly
   * to the client.
   */

  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  gboolean did_placement;
  MtkRectangle unconstrained_rect;
  MtkRectangle constrained_rect;
  MtkRectangle temporary_rect;
  int rel_x = 0;
  int rel_y = 0;
  MetaMoveResizeResultFlags result = 0;
  gboolean moved_or_resized = FALSE;
  MetaWindowUpdateMonitorFlags update_monitor_flags;

  g_return_if_fail (!window->override_redirect);

  /* The action has to be a move, a resize or the wayland client
   * acking our choice of size.
   */
  g_assert (flags & (META_MOVE_RESIZE_MOVE_ACTION |
                     META_MOVE_RESIZE_RESIZE_ACTION |
                     META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE));

  did_placement = !window->placed && window->calc_placement;

  /* We don't need it in the idle queue anymore. */
  meta_window_unqueue (window, META_QUEUE_MOVE_RESIZE);

  if ((flags & META_MOVE_RESIZE_RESIZE_ACTION) && (flags & META_MOVE_RESIZE_MOVE_ACTION))
    {
      /* We're both moving and resizing. Just use the passed in rect. */
      unconstrained_rect = frame_rect;
    }
  else if ((flags & META_MOVE_RESIZE_RESIZE_ACTION))
    {
      /* If this is only a resize, then ignore the position given in
       * the parameters and instead calculate the new position from
       * resizing the old rectangle with the given gravity. */
      meta_rectangle_resize_with_gravity (&window->rect,
                                          &unconstrained_rect,
                                          gravity,
                                          frame_rect.width,
                                          frame_rect.height);
    }
  else if ((flags & META_MOVE_RESIZE_MOVE_ACTION))
    {
      /* If this is only a move, then ignore the passed in size and
       * just use the existing size of the window. */
      unconstrained_rect.x = frame_rect.x;
      unconstrained_rect.y = frame_rect.y;
      unconstrained_rect.width = window->rect.width;
      unconstrained_rect.height = window->rect.height;
    }
  else if ((flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE))
    {
      /* This is a Wayland buffer acking our size. The new rect is
       * just the existing one we have. Ignore the passed-in rect
       * completely. */
      unconstrained_rect = window->rect;
    }
  else
    g_assert_not_reached ();

  constrained_rect = unconstrained_rect;
  temporary_rect = window->rect;
  if (flags & META_MOVE_RESIZE_CONSTRAIN && window->monitor)
    {
      MtkRectangle old_rect;
      meta_window_get_frame_rect (window, &old_rect);

      meta_window_constrain (window,
                             flags,
                             gravity,
                             &old_rect,
                             &constrained_rect,
                             &temporary_rect,
                             &rel_x,
                             &rel_y);
    }
  else if (window->placement.rule)
    {
      rel_x = window->placement.pending.rel_x;
      rel_y = window->placement.pending.rel_y;
    }

  /* If we did placement, then we need to save the position that the window
   * was placed at to make sure that meta_window_update_layout() places the
   * window correctly.
   */
  if (did_placement)
    {
      unconstrained_rect.x = constrained_rect.x;
      unconstrained_rect.y = constrained_rect.y;
    }

  /* Do the protocol-specific move/resize logic */
  META_WINDOW_GET_CLASS (window)->move_resize_internal (window,
                                                        gravity,
                                                        unconstrained_rect,
                                                        constrained_rect,
                                                        temporary_rect,
                                                        rel_x,
                                                        rel_y,
                                                        flags, &result);

  if (result & META_MOVE_RESIZE_RESULT_MOVED)
    {
      moved_or_resized = TRUE;
      g_signal_emit (window, window_signals[POSITION_CHANGED], 0);
    }

  if (result & META_MOVE_RESIZE_RESULT_RESIZED)
    {
      moved_or_resized = TRUE;
      g_signal_emit (window, window_signals[SIZE_CHANGED], 0);
    }

  /* Only update the stored size when requested but not when a
   * (potentially outdated) request completes */
  if (!(flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE) || 
      flags & META_MOVE_RESIZE_WAYLAND_CLIENT_RESIZE)
    {
      window->unconstrained_rect = unconstrained_rect;
    }

  if ((moved_or_resized ||
       did_placement ||
       (result & META_MOVE_RESIZE_RESULT_STATE_CHANGED) != 0) &&
      window->known_to_compositor)
    {
      meta_compositor_sync_window_geometry (window->display->compositor,
                                            window,
                                            did_placement);
    }

  update_monitor_flags = META_WINDOW_UPDATE_MONITOR_FLAGS_NONE;
  if (flags & META_MOVE_RESIZE_USER_ACTION)
    update_monitor_flags |= META_WINDOW_UPDATE_MONITOR_FLAGS_USER_OP;
  if (flags & META_MOVE_RESIZE_FORCE_UPDATE_MONITOR)
    update_monitor_flags |= META_WINDOW_UPDATE_MONITOR_FLAGS_FORCE;

  if (window->monitor)
    {
      uint64_t old_output_winsys_id;

      old_output_winsys_id = window->monitor->winsys_id;

      meta_window_update_monitor (window, update_monitor_flags);

      if (old_output_winsys_id != window->monitor->winsys_id &&
          flags & META_MOVE_RESIZE_MOVE_ACTION && flags & META_MOVE_RESIZE_USER_ACTION)
        window->preferred_output_winsys_id = window->monitor->winsys_id;
    }
  else
    {
      meta_window_update_monitor (window, update_monitor_flags);
    }

  if ((result & META_MOVE_RESIZE_RESULT_FRAME_SHAPE_CHANGED) && window->frame_bounds)
    g_clear_pointer (&window->frame_bounds, mtk_region_unref);

  meta_window_foreach_transient (window, maybe_move_attached_window, NULL);

  meta_stack_update_window_tile_matches (window->display->stack,
                                         workspace_manager->active_workspace);

  if (flags & META_MOVE_RESIZE_WAYLAND_CLIENT_RESIZE)
    meta_window_queue (window, META_QUEUE_MOVE_RESIZE);

  /* This is a workaround for #1627. We still don't have any tests that can
   * reproduce this issue reliably and this is not a proper fix! */
  if (flags & META_MOVE_RESIZE_WAYLAND_FINISH_MOVE_RESIZE &&
      (result & META_MOVE_RESIZE_RESULT_MOVED ||
       result & META_MOVE_RESIZE_RESULT_RESIZED) &&
      (window->maximized_horizontally || window->maximized_vertically))
    meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}

/**
 * meta_window_move_frame:
 * @window: a #MetaWindow
 * @user_op: bool to indicate whether or not this is a user operation
 * @root_x_nw: desired x pos
 * @root_y_nw: desired y pos
 *
 * Moves the window to the desired location on window's assigned
 * workspace, using the northwest edge of the frame as the reference,
 * instead of the actual window's origin, but only if a frame is present.
 *
 * Otherwise, acts identically to meta_window_move().
 */
void
meta_window_move_frame (MetaWindow *window,
                        gboolean    user_op,
                        int         root_x_nw,
                        int         root_y_nw)
{
  MetaMoveResizeFlags flags;
  MtkRectangle rect = { root_x_nw, root_y_nw, 0, 0 };

  g_return_if_fail (!window->override_redirect);

  flags = ((user_op ? META_MOVE_RESIZE_USER_ACTION : 0) |
           META_MOVE_RESIZE_MOVE_ACTION |
           META_MOVE_RESIZE_CONSTRAIN);
  meta_window_move_resize_internal (window, flags, META_GRAVITY_NORTH_WEST, rect);
}

static void
meta_window_move_between_rects (MetaWindow          *window,
                                MetaMoveResizeFlags  move_resize_flags,
                                const MtkRectangle  *old_area,
                                const MtkRectangle  *new_area)
{
  double rel_x, rel_y;
  int new_x, new_y;

  if (!old_area)
    {
      new_x = new_area->x;
      new_y = new_area->y;
    }
  else if (mtk_rectangle_contains_rect (old_area, &window->unconstrained_rect) &&
           old_area->width > window->unconstrained_rect.width &&
           old_area->height > window->unconstrained_rect.height &&
           new_area->width >= window->unconstrained_rect.width &&
           new_area->height >= window->unconstrained_rect.height)
    {
      rel_x = (double)(window->unconstrained_rect.x - old_area->x) /
              (old_area->width - window->unconstrained_rect.width);
      rel_y = (double)(window->unconstrained_rect.y - old_area->y) /
              (old_area->height - window->unconstrained_rect.height);

      g_warn_if_fail (rel_x >= 0.0 && rel_x <= 1.0 &&
                      rel_y >= 0.0 && rel_y <= 1.0);

      new_x = new_area->x +
              rel_x * (new_area->width - window->unconstrained_rect.width);
      new_y = new_area->y +
              rel_y * (new_area->height - window->unconstrained_rect.height);
    }
  else
    {
      rel_x = (float)(window->unconstrained_rect.x - old_area->x +
                      (window->unconstrained_rect.width / 2)) / old_area->width;
      rel_y = (float)(window->unconstrained_rect.y - old_area->y +
                      (window->unconstrained_rect.height / 2)) / old_area->height;

      rel_x = CLAMP (rel_x, FLT_EPSILON, 1.0 - FLT_EPSILON);
      rel_y = CLAMP (rel_y, FLT_EPSILON, 1.0 - FLT_EPSILON);

      new_x = new_area->x - (window->unconstrained_rect.width / 2) +
              (rel_x * new_area->width);
      new_y = new_area->y - (window->unconstrained_rect.height / 2) +
              (rel_y * new_area->height);
    }

  window->unconstrained_rect.x = new_x;
  window->unconstrained_rect.y = new_y;

  meta_window_move_resize_internal (window,
                                    (move_resize_flags |
                                     META_MOVE_RESIZE_MOVE_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_CONSTRAIN),
                                    META_GRAVITY_NORTH_WEST,
                                    window->unconstrained_rect);
}

/**
 * meta_window_move_resize_frame:
 * @window: a #MetaWindow
 * @user_op: bool to indicate whether or not this is a user operation
 * @root_x_nw: new x
 * @root_y_nw: new y
 * @w: desired width
 * @h: desired height
 *
 * Resizes the window so that its outer bounds (including frame)
 * fit within the given rect
 */
void
meta_window_move_resize_frame (MetaWindow  *window,
                               gboolean     user_op,
                               int          root_x_nw,
                               int          root_y_nw,
                               int          w,
                               int          h)
{
  MetaMoveResizeFlags flags;
  MtkRectangle rect = { root_x_nw, root_y_nw, w, h };

  g_return_if_fail (!window->override_redirect);

  flags = ((user_op ? META_MOVE_RESIZE_USER_ACTION : 0) |
           META_MOVE_RESIZE_MOVE_ACTION |
           META_MOVE_RESIZE_RESIZE_ACTION |
           META_MOVE_RESIZE_CONSTRAIN);

  meta_window_move_resize_internal (window, flags, META_GRAVITY_NORTH_WEST, rect);
}

/**
 * meta_window_move_to_monitor:
 * @window: a #MetaWindow
 * @monitor: desired monitor index
 *
 * Moves the window to the monitor with index @monitor, keeping
 * the relative position of the window's top left corner.
 */
void
meta_window_move_to_monitor (MetaWindow  *window,
                             int          monitor)
{
  MtkRectangle old_area, new_area;

  if (window->tile_mode != META_TILE_NONE)
    window->tile_monitor_number = monitor;

  meta_window_get_work_area_for_monitor (window,
                                         window->monitor->number,
                                         &old_area);
  meta_window_get_work_area_for_monitor (window,
                                         monitor,
                                         &new_area);

  if (window->unconstrained_rect.width == 0 ||
      window->unconstrained_rect.height == 0 ||
      !mtk_rectangle_overlap (&window->unconstrained_rect, &old_area))
    {
      meta_window_move_between_rects (window, 0, NULL, &new_area);
    }
  else
    {
      MtkRectangle old_frame_rect, old_buffer_rect;

      if (monitor == window->monitor->number)
        return;

      meta_window_get_frame_rect (window, &old_frame_rect);
      meta_window_get_buffer_rect (window, &old_buffer_rect);

      meta_compositor_size_change_window (window->display->compositor, window,
                                          META_SIZE_CHANGE_MONITOR_MOVE,
                                          &old_frame_rect, &old_buffer_rect);

      meta_window_move_between_rects (window, 0, &old_area, &new_area);
    }

  window->preferred_output_winsys_id = window->monitor->winsys_id;

  if (window->fullscreen || window->override_redirect)
    meta_display_queue_check_fullscreen (window->display);
}

static void
adjust_size_for_tile_match (MetaWindow *window,
                            int        *new_w,
                            int        *new_h)
{
  MtkRectangle work_area, rect;
  MetaWindow *tile_match = window->tile_match;

  if (!META_WINDOW_TILED_SIDE_BY_SIDE (window) || !tile_match)
    return;

  meta_window_get_work_area_for_monitor (window, window->tile_monitor_number, &work_area);

  /* Make sure the resize does not break minimum sizes */
  rect = work_area;
  rect.width = *new_w;

  meta_window_frame_rect_to_client_rect (window, &rect, &rect);
  *new_w += MAX(0, window->size_hints.min_width - rect.width);

  /* Make sure we're not resizing the tile match below its min width */
  rect = work_area;
  rect.width = work_area.width - *new_w;

  meta_window_frame_rect_to_client_rect (tile_match, &rect, &rect);
  *new_w -= MAX(0, tile_match->size_hints.min_width - rect.width);
}

void
meta_window_resize_frame_with_gravity (MetaWindow *window,
                                       gboolean     user_op,
                                       int          w,
                                       int          h,
                                       MetaGravity  gravity)
{
  MetaMoveResizeFlags flags;
  MtkRectangle rect;

  rect.width = w;
  rect.height = h;

  if (user_op)
    {
      MetaWindowDrag *window_drag;

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);

      /* When resizing in-tandem with a tile match, we need to respect
       * its minimum width
       */
      if (window_drag &&
          meta_window_drag_get_window (window_drag) == window)
        adjust_size_for_tile_match (window, &w, &h);
      meta_window_update_tile_fraction (window, w, h);
    }

  flags = ((user_op ? META_MOVE_RESIZE_USER_ACTION : 0) |
           META_MOVE_RESIZE_RESIZE_ACTION |
           META_MOVE_RESIZE_CONSTRAIN);
  meta_window_move_resize_internal (window, flags, gravity, rect);
}

void
meta_window_update_layout (MetaWindow *window)
{
  meta_window_move_resize_frame (window, FALSE,
                                 window->unconstrained_rect.x,
                                 window->unconstrained_rect.y,
                                 window->unconstrained_rect.width,
                                 window->unconstrained_rect.height);
}

void
meta_window_get_gravity_position (MetaWindow  *window,
                                  MetaGravity  gravity,
                                  int         *root_x,
                                  int         *root_y)
{
  MtkRectangle frame_extents;
  int w, h;
  int x, y;

  w = window->rect.width;
  h = window->rect.height;

  if (gravity == META_GRAVITY_STATIC)
    {
      frame_extents = window->rect;
      if (window->frame)
        {
          frame_extents.x = window->frame->rect.x + window->frame->child_x;
          frame_extents.y = window->frame->rect.y + window->frame->child_y;
        }
    }
  else
    {
      if (window->frame == NULL)
        frame_extents = window->rect;
      else
        frame_extents = window->frame->rect;
    }

  x = frame_extents.x;
  y = frame_extents.y;

  switch (gravity)
    {
    case META_GRAVITY_NORTH:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_SOUTH:
      /* Find center of frame. */
      x += frame_extents.width / 2;
      /* Center client window on that point. */
      x -= w / 2;
      break;

    case META_GRAVITY_SOUTH_EAST:
    case META_GRAVITY_EAST:
    case META_GRAVITY_NORTH_EAST:
      /* Find right edge of frame */
      x += frame_extents.width;
      /* Align left edge of client at that point. */
      x -= w;
      break;
    default:
      break;
    }

  switch (gravity)
    {
    case META_GRAVITY_WEST:
    case META_GRAVITY_CENTER:
    case META_GRAVITY_EAST:
      /* Find center of frame. */
      y += frame_extents.height / 2;
      /* Center client window there. */
      y -= h / 2;
      break;
    case META_GRAVITY_SOUTH_WEST:
    case META_GRAVITY_SOUTH:
    case META_GRAVITY_SOUTH_EAST:
      /* Find south edge of frame */
      y += frame_extents.height;
      /* Place bottom edge of client there */
      y -= h;
      break;
    default:
      break;
    }

  if (root_x)
    *root_x = x;
  if (root_y)
    *root_y = y;
}

void
meta_window_get_session_geometry (MetaWindow  *window,
                                  int         *x,
                                  int         *y,
                                  int         *width,
                                  int         *height)
{
  meta_window_get_gravity_position (window,
                                    window->size_hints.win_gravity,
                                    x, y);

  *width = (window->rect.width - window->size_hints.base_width) /
    window->size_hints.width_inc;
  *height = (window->rect.height - window->size_hints.base_height) /
    window->size_hints.height_inc;
}

gboolean
meta_window_geometry_contains_rect (MetaWindow   *window,
                                    MtkRectangle *rect)
{
  return mtk_rectangle_contains_rect (&window->rect,
                                      rect);
}

/**
 * meta_window_get_buffer_rect:
 * @window: a #MetaWindow
 * @rect: (out): pointer to an allocated #MtkRectangle
 *
 * Gets the rectangle that the pixmap or buffer of @window occupies.
 *
 * For X11 windows, this is the server-side geometry of the toplevel
 * window.
 *
 * For Wayland windows, this is the bounding rectangle of the attached
 * buffer.
 */
void
meta_window_get_buffer_rect (const MetaWindow *window,
                             MtkRectangle     *rect)
{
  *rect = window->buffer_rect;
}

/**
 * meta_window_client_rect_to_frame_rect:
 * @window: a #MetaWindow
 * @client_rect: client rectangle in root coordinates
 * @frame_rect: (out): location to store the computed corresponding frame bounds.
 *
 * Converts a desired bounds of the client window into the corresponding bounds
 * of the window frame (excluding invisible borders and client side shadows.)
 */
void
meta_window_client_rect_to_frame_rect (MetaWindow   *window,
                                       MtkRectangle *client_rect,
                                       MtkRectangle *frame_rect)
{
  if (!frame_rect)
    return;

  *frame_rect = *client_rect;

  /* The support for G_MAXINT here to mean infinity is a convenience for
   * constraints.c:get_size_limits() and not something that we provide
   * in other locations or document.
   */
  if (window->frame)
    {
      MetaFrameBorders borders;
      meta_frame_calc_borders (window->frame, &borders);

      frame_rect->x -= borders.visible.left;
      frame_rect->y -= borders.visible.top;
      if (frame_rect->width != G_MAXINT)
        frame_rect->width += borders.visible.left + borders.visible.right;
      if (frame_rect->height != G_MAXINT)
        frame_rect->height += borders.visible.top  + borders.visible.bottom;
    }
  else
    {
      const MetaFrameBorder *extents = &window->custom_frame_extents;
      frame_rect->x += extents->left;
      frame_rect->y += extents->top;
      if (frame_rect->width != G_MAXINT)
        frame_rect->width -= extents->left + extents->right;
      if (frame_rect->height != G_MAXINT)
        frame_rect->height -= extents->top + extents->bottom;
    }
}

/**
 * meta_window_frame_rect_to_client_rect:
 * @window: a #MetaWindow
 * @frame_rect: desired frame bounds for the window
 * @client_rect: (out): location to store the computed corresponding client rectangle.
 *
 * Converts a desired frame bounds for a window into the bounds of the client
 * window.
 */
void
meta_window_frame_rect_to_client_rect (MetaWindow   *window,
                                       MtkRectangle *frame_rect,
                                       MtkRectangle *client_rect)
{
  if (!client_rect)
    return;

  *client_rect = *frame_rect;

  if (window->frame)
    {
      MetaFrameBorders borders;
      meta_frame_calc_borders (window->frame, &borders);

      client_rect->x += borders.visible.left;
      client_rect->y += borders.visible.top;
      client_rect->width  -= borders.visible.left + borders.visible.right;
      client_rect->height -= borders.visible.top  + borders.visible.bottom;
    }
  else
    {
      const MetaFrameBorder *extents = &window->custom_frame_extents;
      client_rect->x -= extents->left;
      client_rect->y -= extents->top;
      client_rect->width += extents->left + extents->right;
      client_rect->height += extents->top + extents->bottom;
    }
}

/**
 * meta_window_get_frame_rect:
 * @window: a #MetaWindow
 * @rect: (out): pointer to an allocated #MtkRectangle
 *
 * Gets the rectangle that bounds @window that is what the user thinks of
 * as the edge of the window.
 *
 * This doesn't include any extra reactive area that we or the client
 * adds to the window, or any area that the client adds to draw a client-side shadow.
 */
void
meta_window_get_frame_rect (const MetaWindow *window,
                            MtkRectangle     *rect)
{
  *rect = window->rect;
}

/**
 * meta_window_get_client_area_rect:
 * @window: a #MetaWindow
 * @rect: (out): pointer to a rectangle
 *
 * Gets the rectangle for the boundaries of the client area, relative
 * to the buffer rect.
 */
void
meta_window_get_client_area_rect (const MetaWindow *window,
                                  MtkRectangle     *rect)
{
  MetaFrameBorders borders;

  meta_frame_calc_borders (window->frame, &borders);

  rect->x = borders.total.left;
  rect->y = borders.total.top;

  rect->width = window->buffer_rect.width - borders.total.left - borders.total.right;
  rect->height = window->buffer_rect.height - borders.total.top - borders.total.bottom;
}

void
meta_window_get_titlebar_rect (MetaWindow   *window,
                               MtkRectangle *rect)
{
  meta_window_get_frame_rect (window, rect);

  /* The returned rectangle is relative to the frame rect. */
  rect->x = 0;
  rect->y = 0;

  if (window->frame)
    {
      rect->height = window->frame->child_y;
    }
  else
    {
      /* Pick an arbitrary height for a titlebar. We might want to
       * eventually have CSD windows expose their borders to us. */
      rect->height = 50;
    }
}

/**
 * meta_window_get_startup_id:
 * @window: a #MetaWindow
 *
 * Gets the startup id of the given #MetaWindow
 *
 * Returns: (nullable): the startup id
 */
const char*
meta_window_get_startup_id (MetaWindow *window)
{
#ifdef HAVE_X11_CLIENT
  if (window->startup_id == NULL && window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    {
      MetaGroup *group;

      group = meta_window_x11_get_group (window);

      if (group != NULL)
        return meta_group_get_startup_id (group);
    }
#endif

  return window->startup_id;
}

static MetaWindow*
get_modal_transient (MetaWindow *window)
{
  GSList *windows;
  GSList *tmp;
  MetaWindow *modal_transient;

  /* A window can't be the transient of itself, but this is just for
   * convenience in the loop below; we manually fix things up at the
   * end if no real modal transient was found.
   */
  modal_transient = window;

  windows = meta_display_list_windows (window->display, META_LIST_DEFAULT);
  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *transient = tmp->data;

      if (transient->transient_for == modal_transient &&
          transient->type == META_WINDOW_MODAL_DIALOG)
        {
          modal_transient = transient;
          tmp = windows;
          continue;
        }

      tmp = tmp->next;
    }

  g_slist_free (windows);

  if (window == modal_transient)
    modal_transient = NULL;

  return modal_transient;
}

static gboolean
meta_window_transient_can_focus (MetaWindow *window)
{
#ifdef HAVE_WAYLAND
  if (window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      MetaWaylandSurface *surface = meta_window_get_wayland_surface (window);
      return meta_wayland_surface_get_buffer (surface) != NULL;
    }
#endif

  return TRUE;
}

static void
meta_window_make_most_recent (MetaWindow    *window,
                              MetaWorkspace *target_workspace)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  GList *l;

  /* Marks the window as the most recently used window on a specific workspace.
   * If the window exists on all workspaces, it will become the most recently
   * used sticky window on all other workspaces. This ensures proper tracking
   * among windows on all workspaces while not overriding MRU for other windows.
   */

  for (l = workspace_manager->workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *workspace = l->data;
      GList *self, *link;

      self = g_list_find (workspace->mru_list, window);
      if (!self)
        continue;

      /*
       * Move to the front of the MRU list if the window is on the
       * target_workspace or was explicitly made sticky
       */
      if (workspace == target_workspace || window->on_all_workspaces_requested)
        {
          workspace->mru_list = g_list_delete_link (workspace->mru_list, self);
          workspace->mru_list = g_list_prepend (workspace->mru_list, window);
          continue;
        }

      /* Not sticky and not on the target workspace: we're done here */
      if (!window->on_all_workspaces)
        continue;

      /* Otherwise move it before other sticky windows */
      for (link = workspace->mru_list; link; link = link->next)
        {
          MetaWindow *mru_window = link->data;

          if (mru_window->workspace == NULL)
            break;
        }

      if (link == self)
        continue;

      workspace->mru_list = g_list_delete_link (workspace->mru_list, self);
      workspace->mru_list = g_list_insert_before (workspace->mru_list, link, window);
    }
}

/* XXX META_EFFECT_FOCUS */
void
meta_window_focus (MetaWindow  *window,
                   guint32      timestamp)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MetaWindow *modal_transient;
  MetaBackend *backend;
  ClutterStage *stage;
  MetaWindowDrag *window_drag;
  MetaWindow *grab_window = NULL;

  g_return_if_fail (!window->override_redirect);

  /* This is a oneshot flag */
  window->restore_focus_on_map = FALSE;

  meta_topic (META_DEBUG_FOCUS,
              "Setting input focus to window %s, input: %d focusable: %d",
              window->desc, window->input, meta_window_is_focusable (window));

  if (window->in_workspace_change)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Window %s is currently changing workspaces, not focusing it after all",
                  window->desc);
      return;
    }

  window_drag =
    meta_compositor_get_current_window_drag (window->display->compositor);
  if (window_drag)
    grab_window = meta_window_drag_get_window (window_drag);

  if (grab_window &&
      grab_window != window &&
      !grab_window->unmanaging)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Current focus window %s has global keygrab, not focusing window %s after all",
                  grab_window->desc, window->desc);
      return;
    }

  modal_transient = get_modal_transient (window);
  if (modal_transient != NULL &&
      !modal_transient->unmanaging &&
      meta_window_transient_can_focus (modal_transient))
    {
      meta_topic (META_DEBUG_FOCUS,
                  "%s has %s as a modal transient, so focusing it instead.",
                  window->desc, modal_transient->desc);
      if (!meta_window_located_on_workspace (modal_transient, workspace_manager->active_workspace))
        meta_window_change_workspace (modal_transient, workspace_manager->active_workspace);
      window = modal_transient;
    }

  meta_window_flush_calc_showing (window);

  if (!window->mapped || window->hidden)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Window %s is not showing, not focusing after all",
                  window->desc);
      return;
    }

  META_WINDOW_GET_CLASS (window)->focus (window, timestamp);

  /* Move to the front of all workspaces' MRU lists the window
   * is on. We should only be "removing" it from the MRU list if
   * it's already there.  Note that it's possible that we might
   * be processing this FocusIn after we've changed to a
   * different workspace; we should therefore update the MRU
   * list only if the window is actually on the active
   * workspace.
   */
  if (workspace_manager->active_workspace &&
      meta_window_located_on_workspace (window,
                                        workspace_manager->active_workspace))
    meta_window_make_most_recent (window, workspace_manager->active_workspace);

  backend = backend_from_window (window);
  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  if (clutter_stage_get_grab_actor (stage) == NULL)
    clutter_stage_set_key_focus (stage, NULL);

  if (window->close_dialog &&
      meta_close_dialog_is_visible (window->close_dialog))
    meta_close_dialog_focus (window->close_dialog);

  if (window->wm_state_demands_attention)
    meta_window_unset_demands_attention(window);

/*  meta_effect_run_focus(window, NULL, NULL); */
}

/* Workspace management. Invariants:
 *
 *  - window->workspace describes the workspace the window is on.
 *
 *  - workspace->windows is a list of windows that is located on
 *    that workspace.
 *
 *  - If the window is on_all_workspaces, then
 *    window->workspace == NULL, but workspace->windows contains
 *    the window.
 */

static void
set_workspace_state (MetaWindow    *window,
                     gboolean       on_all_workspaces,
                     MetaWorkspace *workspace)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  /* If we're on all workspaces, then our new workspace must be NULL,
   * otherwise it must be set, unless we're unmanaging. */
  if (on_all_workspaces)
    g_assert_null (workspace);
  else
    g_assert_true (window->unmanaging || workspace != NULL);

  /* If this is an override-redirect window, ensure that the only
   * times we're setting the workspace state is either during construction
   * to mark as on_all_workspaces, or when unmanaging to remove all the
   * workspaces. */
  if (window->override_redirect)
    g_return_if_fail ((window->constructing && on_all_workspaces) || window->unmanaging);

  if (on_all_workspaces == window->on_all_workspaces &&
      workspace == window->workspace &&
      !window->constructing)
    return;

  window->in_workspace_change = TRUE;

  if (window->workspace)
    meta_workspace_remove_window (window->workspace, window);
  else if (window->on_all_workspaces)
    {
      GList *l;
      for (l = workspace_manager->workspaces; l != NULL; l = l->next)
        {
          MetaWorkspace *ws = l->data;
          meta_workspace_remove_window (ws, window);
        }
    }

  window->on_all_workspaces = on_all_workspaces;
  window->workspace = workspace;

  if (window->workspace)
    meta_workspace_add_window (window->workspace, window);
  else if (window->on_all_workspaces)
    {
      GList *l;
      for (l = workspace_manager->workspaces; l != NULL; l = l->next)
        {
          MetaWorkspace *ws = l->data;
          meta_workspace_add_window (ws, window);
        }
    }

  window->in_workspace_change = FALSE;

  if (!window->constructing)
    meta_window_update_appears_focused (window);

  /* queue a move_resize since changing workspaces may change
   * the relevant struts
   */
  if (!window->override_redirect)
    meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_current_workspace_changed (window);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_ON_ALL_WORKSPACES]);
  g_signal_emit (window, window_signals[WORKSPACE_CHANGED], 0);
}

static gboolean
should_be_on_all_workspaces (MetaWindow *window)
{
  if (window->always_sticky)
    return TRUE;

  if (window->on_all_workspaces_requested)
    return TRUE;

  if (window->override_redirect)
    return TRUE;

  if (meta_prefs_get_workspaces_only_on_primary () &&
      !window->unmanaging &&
      window->monitor &&
      !meta_window_is_on_primary_monitor (window))
    return TRUE;

  return FALSE;
}

void
meta_window_on_all_workspaces_changed (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  gboolean on_all_workspaces = should_be_on_all_workspaces (window);

  if (window->on_all_workspaces == on_all_workspaces)
    return;

  MetaWorkspace *workspace;

  if (on_all_workspaces)
    {
      workspace = NULL;
    }
  else
    {
      /* We're coming out of the sticky state. Put the window on
       * the currently active workspace. */
      workspace = workspace_manager->active_workspace;
    }

  set_workspace_state (window, on_all_workspaces, workspace);
}

static void
meta_window_change_workspace_without_transients (MetaWindow    *window,
                                                 MetaWorkspace *workspace)
{
  if (window->unmanaging)
    return;

  /* Try to unstick the window if it's stuck. This doesn't
   * have any guarantee that we'll actually unstick the
   * window, since it could be stuck for other reasons. */
  if (window->on_all_workspaces_requested)
    meta_window_unstick (window);

  /* We failed to unstick the window. */
  if (window->on_all_workspaces)
    return;

  if (window->workspace == workspace)
    return;

  set_workspace_state (window, FALSE, workspace);
}

static gboolean
change_workspace_foreach (MetaWindow *window,
                          void       *data)
{
  meta_window_change_workspace_without_transients (window, data);
  return TRUE;
}

void
meta_window_change_workspace (MetaWindow    *window,
                              MetaWorkspace *workspace)
{
  g_return_if_fail (!window->override_redirect);

  meta_window_change_workspace_without_transients (window, workspace);

  meta_window_foreach_transient (window, change_workspace_foreach,
                                 workspace);
  meta_window_foreach_ancestor (window, change_workspace_foreach,
                                workspace);
}

static void
window_stick_impl (MetaWindow  *window)
{
  meta_verbose ("Sticking window %s current on_all_workspaces = %d",
                window->desc, window->on_all_workspaces);

  if (window->on_all_workspaces_requested)
    return;

  /* We don't change window->workspaces, because we revert
   * to that original workspace list if on_all_workspaces is
   * toggled back off.
   */
  window->on_all_workspaces_requested = TRUE;
  meta_window_on_all_workspaces_changed (window);
}

static void
window_unstick_impl (MetaWindow  *window)
{
  if (!window->on_all_workspaces_requested)
    return;

  /* Revert to window->workspaces */

  window->on_all_workspaces_requested = FALSE;
  meta_window_on_all_workspaces_changed (window);
}

static gboolean
stick_foreach_func (MetaWindow *window,
                    void       *data)
{
  gboolean stick;

  stick = *(gboolean*)data;
  if (stick)
    window_stick_impl (window);
  else
    window_unstick_impl (window);
  return TRUE;
}

void
meta_window_stick (MetaWindow  *window)
{
  gboolean stick = TRUE;

  g_return_if_fail (!window->override_redirect);

  window_stick_impl (window);
  meta_window_foreach_transient (window,
                                 stick_foreach_func,
                                 &stick);
}

void
meta_window_unstick (MetaWindow  *window)
{
  gboolean stick = FALSE;

  g_return_if_fail (!window->override_redirect);

  window_unstick_impl (window);
  meta_window_foreach_transient (window,
                                 stick_foreach_func,
                                 &stick);
}

void
meta_window_current_workspace_changed (MetaWindow *window)
{
  META_WINDOW_GET_CLASS (window)->current_workspace_changed (window);
}

static gboolean
find_root_ancestor (MetaWindow *window,
                    void       *data)
{
  MetaWindow **ancestor = data;

  /* Overwrite the previously "most-root" ancestor with the new one found */
  *ancestor = window;

  /* We want this to continue until meta_window_foreach_ancestor quits because
   * there are no more valid ancestors.
   */
  return TRUE;
}

/**
 * meta_window_find_root_ancestor:
 * @window: a #MetaWindow
 *
 * Follow the chain of parents of @window, skipping transient windows,
 * and return the "root" window which has no non-transient parent.
 *
 * Returns: (transfer none): The root ancestor window
 */
MetaWindow *
meta_window_find_root_ancestor (MetaWindow *window)
{
  MetaWindow *ancestor;
  ancestor = window;
  meta_window_foreach_ancestor (window, find_root_ancestor, &ancestor);
  return ancestor;
}

void
meta_window_raise (MetaWindow  *window)
{
  MetaWindow *ancestor;

  g_return_if_fail (!window->override_redirect);

  ancestor = meta_window_find_root_ancestor (window);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Raising window %s, ancestor of %s",
              ancestor->desc, window->desc);

  /* Raise the ancestor of the window (if the window has no ancestor,
   * then ancestor will be set to the window itself); do this because
   * it's weird to see windows from other apps stacked between a child
   * and parent window of the currently active app.  The stacking
   * constraints in stack.c then magically take care of raising all
   * the child windows appropriately.
   */
  if (window->display->stack == ancestor->display->stack)
    {
      meta_stack_raise (window->display->stack, ancestor);
    }
  else
    {
      meta_warning (
                    "Either stacks aren't per screen or some window has a weird "
                    "transient_for hint; window->display->stack != "
                    "ancestor->screen->stack.  window = %s, ancestor = %s.",
                    window->desc, ancestor->desc);
      /* We could raise the window here, but don't want to do that twice and
       * so we let the case below handle that.
       */
    }

  /* Okay, so stacking constraints misses one case: If a window has
   * two children and we want to raise one of those children, then
   * raising the ancestor isn't enough; we need to also raise the
   * correct child.  See bug 307875.
   */
  if (window != ancestor)
    meta_stack_raise (window->display->stack, window);

  g_signal_emit (window, window_signals[RAISED], 0);
}

/**
 * meta_window_raise_and_make_recent_on_workspace:
 * @window: a #MetaWindow
 * @workspace: the #MetaWorkspace to raise and make it most recent on
 *
 * Raises a window and marks it as the most recently used window on the
 * workspace @target_workspace. If the window exists on all workspaces, it will
 * become the most recently used sticky window on all other workspaces. This
 * ensures proper tracking among windows on all workspaces while not overriding
 * MRU for other windows.
 */
void
meta_window_raise_and_make_recent_on_workspace (MetaWindow    *window,
                                                MetaWorkspace *workspace)
{
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (META_IS_WORKSPACE (workspace));

  meta_window_raise (window);
  meta_window_make_most_recent (window, workspace);
}

void
meta_window_lower (MetaWindow  *window)
{
  g_return_if_fail (!window->override_redirect);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Lowering window %s", window->desc);

  meta_stack_lower (window->display->stack, window);
}

static gboolean
lower_window_and_transients (MetaWindow *window,
                             gpointer    user_data)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  meta_window_lower (window);

  meta_window_foreach_transient (window, lower_window_and_transients, NULL);

  if (meta_prefs_get_raise_on_click ())
    {
      /* Move window to the back of the focusing workspace's MRU list.
       * Do extra sanity checks to avoid possible race conditions.
       * (Borrowed from window.c.)
       */
      if (workspace_manager->active_workspace &&
          meta_window_located_on_workspace (window,
                                            workspace_manager->active_workspace))
        {
          GList *link;
          link = g_list_find (workspace_manager->active_workspace->mru_list,
                              window);
          g_assert (link);

          workspace_manager->active_workspace->mru_list =
            g_list_remove_link (workspace_manager->active_workspace->mru_list,
                                link);
          g_list_free (link);

          workspace_manager->active_workspace->mru_list =
            g_list_append (workspace_manager->active_workspace->mru_list,
                           window);
        }
    }

  return FALSE;
}

void
meta_window_lower_with_transients (MetaWindow *window,
                                   uint32_t    timestamp)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  lower_window_and_transients (window, NULL);

 /* Rather than try to figure that out whether we just lowered
  * the focus window, assume that's always the case. (Typically,
  * this will be invoked via keyboard action or by a mouse action;
  * in either case the window or a modal child will have been focused.) */
  meta_workspace_focus_default_window (workspace_manager->active_workspace,
                                       NULL,
                                       timestamp);
}

/*
 * Move window to the requested workspace; append controls whether new WS
 * should be created if one does not exist.
 */
void
meta_window_change_workspace_by_index (MetaWindow *window,
                                       gint        space_index,
                                       gboolean    append)
{
  MetaWorkspaceManager *workspace_manager;
  MetaWorkspace *workspace;
  MetaDisplay   *display;

  g_return_if_fail (!window->override_redirect);

  if (space_index == -1)
    {
      meta_window_stick (window);
      return;
    }

  display = window->display;
  workspace_manager = display->workspace_manager;

  workspace =
    meta_workspace_manager_get_workspace_by_index (workspace_manager, space_index);

  if (!workspace && append)
    workspace = meta_workspace_manager_append_new_workspace (workspace_manager, FALSE, META_CURRENT_TIME);

  if (workspace)
    meta_window_change_workspace (window, workspace);
}

void
meta_window_update_appears_focused (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager;
  MetaWorkspace *workspace;
  gboolean appears_focused;

  workspace_manager = window->display->workspace_manager;
  workspace = meta_window_get_workspace (window);

  if (workspace && workspace != workspace_manager->active_workspace)
    {
      appears_focused =
        window == meta_workspace_get_default_focus_window (workspace, NULL) &&
        meta_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK;
    }
  else
    {
      appears_focused = window->has_focus || window->attached_focus_window;
    }

  if (window->appears_focused == appears_focused)
    return;

  window->appears_focused = appears_focused;

  set_net_wm_state (window);
  meta_window_frame_size_changed (window);

  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_APPEARS_FOCUSED]);
}

static gboolean
should_propagate_focus_appearance (MetaWindow *window)
{
  /* Parents of attached modal dialogs should appear focused. */
  if (meta_window_is_attached_dialog (window))
    return TRUE;

  /* Parents of these sorts of override-redirect windows should
   * appear focused. */
  switch (window->type)
    {
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_COMBO:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_DND:
    case META_WINDOW_OVERRIDE_OTHER:
      return TRUE;
    default:
      break;
    }

  return FALSE;
}

/**
 * meta_window_propagate_focus_appearance:
 * @window: the window to start propagating from
 * @focused: %TRUE if @window's ancestors should appear focused,
 *   %FALSE if they should not.
 *
 * Adjusts the value of #MetaWindow:appears-focused on @window's
 * ancestors (but not @window itself). If @focused is %TRUE, each of
 * @window's ancestors will have its %attached_focus_window field set
 * to the current %focus_window. If @focused if %FALSE, each of
 * @window's ancestors will have its %attached_focus_window field
 * cleared if it is currently %focus_window.
 */
static void
meta_window_propagate_focus_appearance (MetaWindow *window,
                                        gboolean    focused)
{
  MetaWindow *child, *parent, *focus_window;

  focus_window = window->display->focus_window;

  child = window;
  parent = meta_window_get_transient_for (child);
  while (parent && (!focused || should_propagate_focus_appearance (child)))
    {
      gboolean child_focus_state_changed = FALSE;

      if (focused && parent->attached_focus_window != focus_window)
        {
          child_focus_state_changed = (parent->attached_focus_window == NULL);
          parent->attached_focus_window = focus_window;
        }
      else if (parent->attached_focus_window == focus_window)
        {
          child_focus_state_changed = (parent->attached_focus_window != NULL);
          parent->attached_focus_window = NULL;
        }

      if (child_focus_state_changed && !parent->has_focus)
        {
          meta_window_update_appears_focused (parent);
        }

      child = parent;
      parent = meta_window_get_transient_for (child);
    }
}

void
meta_window_set_focused_internal (MetaWindow *window,
                                  gboolean    focused)
{
  if (focused)
    {
      window->has_focus = TRUE;
      if (window->override_redirect)
        return;

      /* Ungrab click to focus button since the sync grab can interfere
       * with some things you might do inside the focused window, by
       * causing the client to get funky enter/leave events.
       *
       * The reason we usually have a passive grab on the window is
       * so that we can intercept clicks and raise the window in
       * response. For click-to-focus we don't need that since the
       * focused window is already raised. When raise_on_click is
       * FALSE we also don't need that since we don't do anything
       * when the window is clicked.
       *
       * There is dicussion in bugs 102209, 115072, and 461577
       */
      if (meta_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK ||
          !meta_prefs_get_raise_on_click())
        {
          meta_display_ungrab_focus_window_button (window->display, window);
          /* Since we ungrab with XIAnyModifier above, all button
             grabs go way so we need to re-grab the window buttons. */
          meta_display_grab_window_buttons (window->display, window);
        }

      g_signal_emit (window, window_signals[FOCUS], 0);

      if (!window->attached_focus_window)
        meta_window_update_appears_focused (window);

      meta_window_propagate_focus_appearance (window, TRUE);
    }
  else
    {
      window->has_focus = FALSE;
      if (window->override_redirect)
        return;

      meta_window_propagate_focus_appearance (window, FALSE);

      if (!window->attached_focus_window)
        meta_window_update_appears_focused (window);

      /* Re-grab for click to focus and raise-on-click, if necessary */
      if (meta_prefs_get_focus_mode () == G_DESKTOP_FOCUS_MODE_CLICK ||
          !meta_prefs_get_raise_on_click ())
        meta_display_grab_focus_window_button (window->display, window);
    }
}

/**
 * meta_window_get_icon_geometry:
 * @window: a #MetaWindow
 * @rect: (out): rectangle into which to store the returned geometry.
 *
 * Gets the location of the icon corresponding to the window.
 *
 * The location will be provided set by the task bar or other user interface
 * element displaying the icon, and is relative to the root window.
 *
 * Return value: %TRUE if the icon geometry was successfully retrieved.
 */
gboolean
meta_window_get_icon_geometry (MetaWindow   *window,
                               MtkRectangle *rect)
{
  g_return_val_if_fail (!window->override_redirect, FALSE);

  if (window->icon_geometry_set)
    {
      if (rect)
        *rect = window->icon_geometry;

      return TRUE;
    }

  return FALSE;
}

/**
 * meta_window_set_icon_geometry:
 * @window: a #MetaWindow
 * @rect: (nullable): rectangle with the desired geometry or %NULL.
 *
 * Sets or unsets the location of the icon corresponding to the window.
 *
 * If set, the location should correspond to a dock, task bar or other user
 * interface element displaying the icon, and is relative to the root window.
 */
void
meta_window_set_icon_geometry (MetaWindow   *window,
                               MtkRectangle *rect)
{
  if (rect)
    {
      window->icon_geometry = *rect;
      window->icon_geometry_set = TRUE;
    }
  else
    {
      window->icon_geometry_set = FALSE;
    }
}

static GList*
meta_window_get_workspaces (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  if (window->on_all_workspaces)
    return workspace_manager->workspaces;
  else if (window->workspace != NULL)
    return window->workspace->list_containing_self;
  else if (window->constructing)
    return NULL;
  else
    g_assert_not_reached ();
  return NULL;
}

static void
invalidate_work_areas (MetaWindow *window)
{
  GList *tmp;

  tmp = meta_window_get_workspaces (window);

  while (tmp != NULL)
    {
      meta_workspace_invalidate_work_area (tmp->data);
      tmp = tmp->next;
    }
}

void
meta_window_update_struts (MetaWindow *window)
{
  if (META_WINDOW_GET_CLASS (window)->update_struts (window))
    invalidate_work_areas (window);
}

static void
meta_window_type_changed (MetaWindow *window)
{
  gboolean old_decorated = window->decorated;
  GObject  *object = G_OBJECT (window);

  window->attached = meta_window_should_attach_to_parent (window);
  meta_window_recalc_features (window);

  if (!window->override_redirect)
    set_net_wm_state (window);

  /* Update frame */
  if (window->decorated)
    meta_window_ensure_frame (window);
  else
    meta_window_destroy_frame (window);

  /* update stacking constraints */
  meta_window_update_layer (window);

  meta_window_grab_keys (window);

  g_object_freeze_notify (object);

  if (old_decorated != window->decorated)
    g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_DECORATED]);

  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_WINDOW_TYPE]);

  g_object_thaw_notify (object);
}

void
meta_window_set_type (MetaWindow     *window,
                      MetaWindowType  type)
{
  if (window->type == type)
    return;

  window->type = type;
  meta_window_type_changed (window);
}

void
meta_window_frame_size_changed (MetaWindow *window)
{
  if (window->frame)
    meta_frame_clear_cached_borders (window->frame);
}

static void
meta_window_get_default_skip_hints (MetaWindow *window,
                                    gboolean   *skip_taskbar_out,
                                    gboolean   *skip_pager_out)
{
  META_WINDOW_GET_CLASS (window)->get_default_skip_hints (window, skip_taskbar_out, skip_pager_out);
}

static void
meta_window_recalc_skip_features (MetaWindow *window)
{
  switch (window->type)
    {
      /* Force skip taskbar/pager on these window types */
    case META_WINDOW_DESKTOP:
    case META_WINDOW_DOCK:
    case META_WINDOW_TOOLBAR:
    case META_WINDOW_MENU:
    case META_WINDOW_UTILITY:
    case META_WINDOW_SPLASHSCREEN:
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_DND:
    case META_WINDOW_OVERRIDE_OTHER:
      window->skip_taskbar = TRUE;
      window->skip_pager = TRUE;
      break;

    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
      /* only skip taskbar if we have a real transient parent
         (and ignore the application hints) */
      if (window->transient_for != NULL)
        window->skip_taskbar = TRUE;
      else
        window->skip_taskbar = window->skip_from_window_list;
      break;

    case META_WINDOW_NORMAL:
      {
        gboolean skip_taskbar_hint, skip_pager_hint;
        meta_window_get_default_skip_hints (window, &skip_taskbar_hint, &skip_pager_hint);
        window->skip_taskbar = skip_taskbar_hint | window->skip_from_window_list;
        window->skip_pager = skip_pager_hint | window->skip_from_window_list;
      }
      break;
    }
}

void
meta_window_recalc_features (MetaWindow *window)
{
  gboolean old_has_close_func;
  gboolean old_has_minimize_func;
  gboolean old_has_move_func;
  gboolean old_has_resize_func;
  gboolean old_always_sticky;
  gboolean old_skip_taskbar;

  old_has_close_func = window->has_close_func;
  old_has_minimize_func = window->has_minimize_func;
  old_has_move_func = window->has_move_func;
  old_has_resize_func = window->has_resize_func;
  old_always_sticky = window->always_sticky;
  old_skip_taskbar = window->skip_taskbar;

  /* Use MWM hints initially */
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    window->decorated = window->mwm_decorated;
  else
    window->decorated = FALSE;
  window->border_only = window->mwm_border_only;
  window->has_close_func = window->mwm_has_close_func;
  window->has_minimize_func = window->mwm_has_minimize_func;
  window->has_maximize_func = window->mwm_has_maximize_func;
  window->has_move_func = window->mwm_has_move_func;

  window->has_resize_func = TRUE;

  /* If min_size == max_size, then don't allow resize */
  if (window->size_hints.min_width == window->size_hints.max_width &&
      window->size_hints.min_height == window->size_hints.max_height)
    window->has_resize_func = FALSE;
  else if (!window->mwm_has_resize_func)
    {
      /* We ignore mwm_has_resize_func because WM_NORMAL_HINTS is the
       * authoritative source for that info. Some apps such as mplayer or
       * xine disable resize via MWM but not WM_NORMAL_HINTS, but that
       * leads to e.g. us not fullscreening their windows.  Apps that set
       * MWM but not WM_NORMAL_HINTS are basically broken. We complain
       * about these apps but make them work.
       */

      meta_warning ("Window %s sets an MWM hint indicating it isn't resizable, but sets min size %d x %d and max size %d x %d; this doesn't make much sense.",
                    window->desc,
                    window->size_hints.min_width,
                    window->size_hints.min_height,
                    window->size_hints.max_width,
                    window->size_hints.max_height);
    }

  window->has_fullscreen_func = TRUE;

  window->always_sticky = FALSE;

  /* Semantic category overrides the MWM hints */
  if (window->type == META_WINDOW_TOOLBAR)
    window->decorated = FALSE;

  if (window->type == META_WINDOW_DESKTOP ||
      window->type == META_WINDOW_DOCK ||
      window->override_redirect)
    window->always_sticky = TRUE;

  if (window->override_redirect ||
      meta_window_get_frame_type (window) == META_FRAME_TYPE_LAST)
    {
      window->decorated = FALSE;
      window->has_close_func = FALSE;

      /* FIXME this keeps panels and things from using
       * NET_WM_MOVERESIZE; the problem is that some
       * panels (edge panels) have fixed possible locations,
       * and others ("floating panels") do not.
       *
       * Perhaps we should require edge panels to explicitly
       * disable movement?
       */
      window->has_move_func = FALSE;
      window->has_resize_func = FALSE;
    }

  if (window->type != META_WINDOW_NORMAL)
    {
      window->has_minimize_func = FALSE;
      window->has_maximize_func = FALSE;
      window->has_fullscreen_func = FALSE;
    }

  if (!window->has_resize_func)
    {
      window->has_maximize_func = FALSE;
      MtkRectangle display_rect = { 0 };

      meta_display_get_size (window->display, &display_rect.width,
                             &display_rect.height);

      /* don't allow fullscreen if we can't resize, unless the size
       * is entire screen size (kind of broken, because we
       * actually fullscreen to monitor size not screen size)
       */
      if (window->size_hints.min_width == display_rect.width &&
          window->size_hints.min_height == display_rect.height)
        ; /* leave fullscreen available */
      else
        window->has_fullscreen_func = FALSE;
    }

  /* We leave fullscreen windows decorated, just push the frame outside
   * the screen. This avoids flickering to unparent them.
   *
   * Note that setting has_resize_func = FALSE here must come after the
   * above code that may disable fullscreen, because if the window
   * is not resizable purely due to fullscreen, we don't want to
   * disable fullscreen mode.
   */
  if (window->fullscreen)
    {
      window->has_move_func = FALSE;
      window->has_resize_func = FALSE;
      window->has_maximize_func = FALSE;
    }

  if (window->has_maximize_func && window->monitor)
    {
      MtkRectangle work_area, client_rect;

      meta_window_get_work_area_current_monitor (window, &work_area);
      meta_window_frame_rect_to_client_rect (window, &work_area, &client_rect);

      if (window->size_hints.min_width > client_rect.width ||
          window->size_hints.min_height > client_rect.height)
        window->has_maximize_func = FALSE;
    }

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Window %s fullscreen = %d not resizable, maximizable = %d fullscreenable = %d min size %dx%d max size %dx%d",
              window->desc,
              window->fullscreen,
              window->has_maximize_func, window->has_fullscreen_func,
              window->size_hints.min_width,
              window->size_hints.min_height,
              window->size_hints.max_width,
              window->size_hints.max_height);

  meta_window_recalc_skip_features (window);

  /* To prevent users from losing windows, let's prevent users from
   * minimizing skip-taskbar windows through the window decorations. */
  if (window->skip_taskbar)
    window->has_minimize_func = FALSE;

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Window %s decorated = %d border_only = %d has_close = %d has_minimize = %d has_maximize = %d has_move = %d skip_taskbar = %d skip_pager = %d",
              window->desc,
              window->decorated,
              window->border_only,
              window->has_close_func,
              window->has_minimize_func,
              window->has_maximize_func,
              window->has_move_func,
              window->skip_taskbar,
              window->skip_pager);

  if (old_skip_taskbar != window->skip_taskbar)
    g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_SKIP_TASKBAR]);

  if (old_always_sticky != window->always_sticky)
    meta_window_on_all_workspaces_changed (window);

  /* FIXME:
   * Lame workaround for recalc_features being used overzealously.
   * The fix is to only recalc_features when something has
   * actually changed.
   */
  if (window->constructing                               ||
      old_has_close_func != window->has_close_func       ||
      old_has_minimize_func != window->has_minimize_func ||
      old_has_move_func != window->has_move_func         ||
      old_has_resize_func != window->has_resize_func     ||
      old_always_sticky != window->always_sticky)
    set_allowed_actions_hint (window);

  if (window->has_resize_func != old_has_resize_func)
    g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_RESIZEABLE]);

  meta_window_frame_size_changed (window);
}

void
meta_window_show_menu (MetaWindow         *window,
                       MetaWindowMenuType  menu,
                       int                 x,
                       int                 y)
{
  g_return_if_fail (!window->override_redirect);
  meta_compositor_show_window_menu (window->display->compositor, window, menu, x, y);
}

void
meta_window_shove_titlebar_onscreen (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MtkRectangle  frame_rect;
  GList         *onscreen_region;
  int            horiz_amount, vert_amount;

  g_return_if_fail (!window->override_redirect);

  /* If there's no titlebar, don't bother */
  if (!window->frame)
    return;

  /* Get the basic info we need */
  meta_window_get_frame_rect (window, &frame_rect);
  onscreen_region = workspace_manager->active_workspace->screen_region;

  /* Extend the region (just in case the window is too big to fit on the
   * screen), then shove the window on screen, then return the region to
   * normal.
   */
  horiz_amount = frame_rect.width;
  vert_amount  = frame_rect.height;
  meta_rectangle_expand_region (onscreen_region,
                                horiz_amount,
                                horiz_amount,
                                0,
                                vert_amount);
  meta_rectangle_shove_into_region(onscreen_region,
                                   FIXED_DIRECTION_X,
                                   &frame_rect);
  meta_rectangle_expand_region (onscreen_region,
                                -horiz_amount,
                                -horiz_amount,
                                0,
                                -vert_amount);

  meta_window_move_frame (window, FALSE, frame_rect.x, frame_rect.y);
}

gboolean
meta_window_titlebar_is_onscreen (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MtkRectangle  titlebar_rect, frame_rect;
  GList         *onscreen_region;
  gboolean       is_onscreen;

  const int min_height_needed  = 8;
  const float min_width_percent  = 0.5;
  const int min_width_absolute = 50;

  /* Titlebar can't be offscreen if there is no titlebar... */
  if (!window->frame)
    return TRUE;

  /* Get the rectangle corresponding to the titlebar */
  meta_window_get_titlebar_rect (window, &titlebar_rect);

  /* Translate into screen coordinates */
  meta_window_get_frame_rect (window, &frame_rect);
  titlebar_rect.x = frame_rect.x;
  titlebar_rect.y = frame_rect.y;

  /* Run through the spanning rectangles for the screen and see if one of
   * them overlaps with the titlebar sufficiently to consider it onscreen.
   */
  is_onscreen = FALSE;
  onscreen_region = workspace_manager->active_workspace->screen_region;
  while (onscreen_region)
    {
      MtkRectangle *spanning_rect = onscreen_region->data;
      MtkRectangle overlap;

      mtk_rectangle_intersect (&titlebar_rect, spanning_rect, &overlap);
      if (overlap.height > MIN (titlebar_rect.height, min_height_needed) &&
          overlap.width  > MIN (titlebar_rect.width * min_width_percent,
                                min_width_absolute))
        {
          is_onscreen = TRUE;
          break;
        }

      onscreen_region = onscreen_region->next;
    }

  return is_onscreen;
}

void
meta_window_get_work_area_for_logical_monitor (MetaWindow         *window,
                                               MetaLogicalMonitor *logical_monitor,
                                               MtkRectangle       *area)
{
  GList *tmp;

  g_assert (logical_monitor);

  /* Initialize to the whole monitor */
  *area = logical_monitor->rect;

  tmp = meta_window_get_workspaces (window);
  while (tmp != NULL)
    {
      MtkRectangle workspace_work_area;
      meta_workspace_get_work_area_for_logical_monitor (tmp->data,
                                                        logical_monitor,
                                                        &workspace_work_area);
      mtk_rectangle_intersect (area,
                               &workspace_work_area,
                               area);
      tmp = tmp->next;
    }

  meta_topic (META_DEBUG_WORKAREA,
              "Window %s monitor %d has work area %d,%d %d x %d",
              window->desc, logical_monitor->number,
              area->x, area->y, area->width, area->height);
}

/**
 * meta_window_get_work_area_current_monitor:
 * @window: a #MetaWindow
 * @area: (out): a location to store the work area
 *
 * Get the work area for the monitor @window is currently on.
 */
void
meta_window_get_work_area_current_monitor (MetaWindow   *window,
                                           MtkRectangle *area)
{
  meta_window_get_work_area_for_logical_monitor (window, window->monitor, area);
}

/**
 * meta_window_get_work_area_for_monitor:
 * @window: a #MetaWindow
 * @which_monitor: a moniotr to get the work area for
 * @area: (out): a location to store the work area
 *
 * Get the work area for @window, given the monitor index
 * @which_monitor.
 */
void
meta_window_get_work_area_for_monitor (MetaWindow   *window,
                                       int           which_monitor,
                                       MtkRectangle *area)
{
  MetaBackend *backend = backend_from_window (window);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;

  g_return_if_fail (which_monitor >= 0);

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_number (monitor_manager,
                                                          which_monitor);

  meta_window_get_work_area_for_logical_monitor (window, logical_monitor, area);
}

/**
 * meta_window_get_work_area_all_monitors:
 * @window: a #MetaWindow
 * @area: (out): a location to store the work area
 *
 * Get the work area for all monitors for @window.
 */
void
meta_window_get_work_area_all_monitors (MetaWindow   *window,
                                        MtkRectangle *area)
{
  GList *tmp;
  MtkRectangle display_rect = { 0 };

  meta_display_get_size (window->display,
                         &display_rect.width,
                         &display_rect.height);

  /* Initialize to the whole display */
  *area = display_rect;

  tmp = meta_window_get_workspaces (window);
  while (tmp != NULL)
    {
      MtkRectangle workspace_work_area;
      meta_workspace_get_work_area_all_monitors (tmp->data,
                                                 &workspace_work_area);
      mtk_rectangle_intersect (area,
                               &workspace_work_area,
                               area);
      tmp = tmp->next;
    }

  meta_topic (META_DEBUG_WORKAREA,
              "Window %s has whole-screen work area %d,%d %d x %d",
              window->desc, area->x, area->y, area->width, area->height);
}

int
meta_window_get_current_tile_monitor_number (MetaWindow *window)
{
  int tile_monitor_number = window->tile_monitor_number;

  if (tile_monitor_number < 0)
    {
      meta_warning ("%s called with an invalid monitor number; using 0 instead", G_STRFUNC);
      tile_monitor_number = 0;
    }

  return tile_monitor_number;
}

void
meta_window_get_tile_area (MetaWindow   *window,
                           MetaTileMode  tile_mode,
                           MtkRectangle *tile_area)
{
  MtkRectangle work_area;
  int tile_monitor_number;
  double fraction;

  g_return_if_fail (tile_mode != META_TILE_NONE);

  tile_monitor_number = meta_window_get_current_tile_monitor_number (window);

  meta_window_get_work_area_for_monitor (window, tile_monitor_number, &work_area);
  meta_window_get_tile_fraction (window, tile_mode, &fraction);

  *tile_area = work_area;
  tile_area->width = round (tile_area->width * fraction);

  if (tile_mode == META_TILE_RIGHT)
    tile_area->x += work_area.width - tile_area->width;
}

/**
 * meta_window_is_client_decorated:
 *
 * Check if if the window has decorations drawn by the client.
 *
 * `window->decorated` refers only to whether we should add decorations.
 */
gboolean
meta_window_is_client_decorated (MetaWindow *window)
{
  if (window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
    {
      /* Assume all Wayland clients draw decorations - not strictly
       * true but good enough for current purposes.
       */
      return TRUE;
    }
  else
    {
      /* Currently the implementation here is hackish -
       * has_custom_frame_extents() is set if _GTK_FRAME_EXTENTS is set
       * to any value even 0. GTK+ always sets _GTK_FRAME_EXTENTS for
       * client-side-decorated window, even if the value is 0 because
       * the window is maxized and has no invisible borders or shadows.
       */
      return window->has_custom_frame_extents;
    }
}

/**
 * meta_window_foreach_transient:
 * @window: a #MetaWindow
 * @func: (scope call) (closure user_data): Called for each window which is a transient of @window (transitively)
 * @user_data: User data
 *
 * Call @func for every window which is either transient for @window, or is
 * a transient of a window which is in turn transient for @window.
 * The order of window enumeration is not defined.
 *
 * Iteration will stop if @func at any point returns %FALSE.
 */
void
meta_window_foreach_transient (MetaWindow            *window,
                               MetaWindowForeachFunc  func,
                               void                  *user_data)
{
  GSList *windows;
  GSList *tmp;

  windows = meta_display_list_windows (window->display, META_LIST_DEFAULT);

  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *transient = tmp->data;

      if (meta_window_is_ancestor_of_transient (window, transient))
        {
          if (!(* func) (transient, user_data))
            break;
        }

      tmp = tmp->next;
    }

  g_slist_free (windows);
}

/**
 * meta_window_foreach_ancestor:
 * @window: a #MetaWindow
 * @func: (scope call) (closure user_data): Called for each window which is a transient parent of @window
 * @user_data: User data
 *
 * If @window is transient, call @func with the window for which it's transient,
 * repeatedly until either we find a non-transient window, or @func returns %FALSE.
 */
void
meta_window_foreach_ancestor (MetaWindow            *window,
                              MetaWindowForeachFunc  func,
                              void                  *user_data)
{
  MetaWindow *w;

  w = window;
  do
    {
      if (w->transient_for == NULL)
        break;

      w = w->transient_for;
    }
  while (w && (* func) (w, user_data));
}

typedef struct
{
  MetaWindow *ancestor;
  gboolean found;
} FindAncestorData;

static gboolean
find_ancestor_func (MetaWindow *window,
                    void       *data)
{
  FindAncestorData *d = data;

  if (window == d->ancestor)
    {
      d->found = TRUE;
      return FALSE;
    }

  return TRUE;
}

/**
 * meta_window_is_ancestor_of_transient:
 * @window: a #MetaWindow
 * @transient: a #MetaWindow
 *
 * The function determines whether @window is an ancestor of @transient; it does
 * so by traversing the @transient's ancestors until it either locates @window
 * or reaches an ancestor that is not transient.
 *
 * Return Value: %TRUE if window is an ancestor of transient.
 */
gboolean
meta_window_is_ancestor_of_transient (MetaWindow *window,
                                      MetaWindow *transient)
{
  FindAncestorData d;

  d.ancestor = window;
  d.found = FALSE;

  meta_window_foreach_ancestor (transient, find_ancestor_func, &d);

  return d.found;
}

/**
 * meta_window_begin_grab_op:
 * @window:
 * @op:
 * @device: (nullable):
 * @sequence: (nullable):
 * @timestamp:
 * @pos_hint: (nullable):
 **/
gboolean
meta_window_begin_grab_op (MetaWindow           *window,
                           MetaGrabOp            op,
                           ClutterInputDevice   *device,
                           ClutterEventSequence *sequence,
                           guint32               timestamp,
                           graphene_point_t     *pos_hint)
{
  return meta_compositor_drag_window (window->display->compositor,
                                      window, op,
                                      device, sequence,
                                      timestamp,
                                      pos_hint);
}

MetaStackLayer
meta_window_get_default_layer (MetaWindow *window)
{
  if (window->wm_state_below)
    return META_LAYER_BOTTOM;
  else if (window->wm_state_above && !META_WINDOW_MAXIMIZED (window))
    return META_LAYER_TOP;
  else if (window->type == META_WINDOW_DESKTOP)
    return META_LAYER_DESKTOP;
  else if (window->type == META_WINDOW_DOCK)
    {
      if (window->monitor && window->monitor->in_fullscreen)
        return META_LAYER_BOTTOM;
      else
        return META_LAYER_DOCK;
    }
  else
    return META_LAYER_NORMAL;
}

void
meta_window_update_layer (MetaWindow *window)
{
#ifdef HAVE_X11_CLIENT
  MetaGroup *group = NULL;

  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    group = meta_window_x11_get_group (window);

  meta_stack_freeze (window->display->stack);
  if (group)
    meta_group_update_layers (group);
  else
    meta_stack_update_layer (window->display->stack, window);
  meta_stack_thaw (window->display->stack);
#else
  meta_stack_freeze (window->display->stack);
  meta_stack_update_layer (window->display->stack, window);
  meta_stack_thaw (window->display->stack);
#endif
}

/* ensure_mru_position_after ensures that window appears after
 * below_this_one in the active_workspace's mru_list (i.e. it treats
 * window as having been less recently used than below_this_one)
 */
static void
ensure_mru_position_after (MetaWindow *window,
                           MetaWindow *after_this_one)
{
  /* This is sort of slow since it runs through the entire list more
   * than once (especially considering the fact that we expect the
   * windows of interest to be the first two elements in the list),
   * but it doesn't matter while we're only using it on new window
   * map.
   */

  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  GList* active_mru_list;
  GList* window_position;
  GList* after_this_one_position;

  active_mru_list         = workspace_manager->active_workspace->mru_list;
  window_position         = g_list_find (active_mru_list, window);
  after_this_one_position = g_list_find (active_mru_list, after_this_one);

  /* after_this_one_position is NULL when we switch workspaces, but in
   * that case we don't need to do any MRU shuffling so we can simply
   * return.
   */
  if (after_this_one_position == NULL)
    return;

  if (g_list_length (window_position) > g_list_length (after_this_one_position))
    {
      workspace_manager->active_workspace->mru_list =
        g_list_delete_link (workspace_manager->active_workspace->mru_list,
                            window_position);

      workspace_manager->active_workspace->mru_list =
        g_list_insert_before (workspace_manager->active_workspace->mru_list,
                              after_this_one_position->next,
                              window);
    }
}

gboolean
meta_window_is_in_stack (MetaWindow *window)
{
  return window->stack_position >= 0;
}

void
meta_window_stack_just_below (MetaWindow *window,
                              MetaWindow *below_this_one)
{
  g_return_if_fail (window         != NULL);
  g_return_if_fail (below_this_one != NULL);

  if (window->stack_position > below_this_one->stack_position)
    {
      meta_topic (META_DEBUG_STACK,
                  "Setting stack position of window %s to %d (making it below window %s).",
                  window->desc,
                  below_this_one->stack_position,
                  below_this_one->desc);
      meta_window_set_stack_position (window, below_this_one->stack_position);
    }
  else
    {
      meta_topic (META_DEBUG_STACK,
                  "Window %s  was already below window %s.",
                  window->desc, below_this_one->desc);
    }
}

void
meta_window_stack_just_above (MetaWindow *window,
                              MetaWindow *above_this_one)
{
  g_return_if_fail (window         != NULL);
  g_return_if_fail (above_this_one != NULL);

  if (window->stack_position < above_this_one->stack_position)
    {
      meta_topic (META_DEBUG_STACK,
                  "Setting stack position of window %s to %d (making it above window %s).",
                  window->desc,
                  above_this_one->stack_position,
                  above_this_one->desc);
      meta_window_set_stack_position (window, above_this_one->stack_position);
    }
  else
    {
      meta_topic (META_DEBUG_STACK,
                  "Window %s  was already above window %s.",
                  window->desc, above_this_one->desc);
    }
}

/**
 * meta_window_get_user_time:
 * @window: a #MetaWindow
 *
 * The user time represents a timestamp for the last time the user
 * interacted with this window.
 *
 * Note this property is only available for non-override-redirect windows.
 *
 * The property is set by Mutter initially upon window creation,
 * and updated thereafter on input events (key and button presses) seen by Mutter,
 * client updates to the _NET_WM_USER_TIME property (if later than the current time)
 * and when focusing the window.
 *
 * Returns: The last time the user interacted with this window.
 */
guint32
meta_window_get_user_time (MetaWindow *window)
{
  return window->net_wm_user_time;
}

void
meta_window_set_user_time (MetaWindow *window,
                           guint32     timestamp)
{
  /* FIXME: If Soeren's suggestion in bug 151984 is implemented, it will allow
   * us to sanity check the timestamp here and ensure it doesn't correspond to
   * a future time.
   */

  g_return_if_fail (!window->override_redirect);

  /* Only update the time if this timestamp is newer... */
  if (window->net_wm_user_time_set &&
      XSERVER_TIME_IS_BEFORE (timestamp, window->net_wm_user_time))
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Window %s _NET_WM_USER_TIME not updated to %u, because it "
                  "is less than %u",
                  window->desc, timestamp, window->net_wm_user_time);
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Window %s has _NET_WM_USER_TIME of %u",
                  window->desc, timestamp);
      window->net_wm_user_time_set = TRUE;
      window->net_wm_user_time = timestamp;
      if (XSERVER_TIME_IS_BEFORE (window->display->last_user_time, timestamp))
        window->display->last_user_time = timestamp;

      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_USER_TIME]);
    }
}

/**
 * meta_window_get_stable_sequence:
 * @window: A #MetaWindow
 *
 * The stable sequence number is a monotonicially increasing
 * unique integer assigned to each #MetaWindow upon creation.
 *
 * This number can be useful for sorting windows in a stable
 * fashion.
 *
 * Returns: Internal sequence number for this window
 */
guint32
meta_window_get_stable_sequence (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), 0);

  return window->stable_sequence;
}

/* Sets the demands_attention hint on a window, but only
 * if it's at least partially obscured (see #305882).
 */
void
meta_window_set_demands_attention (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;
  MtkRectangle candidate_rect, other_rect;
  GList *stack = window->display->stack->sorted;
  MetaWindow *other_window;
  gboolean obscured = FALSE;

  MetaWorkspace *workspace = workspace_manager->active_workspace;

  if (window->wm_state_demands_attention)
    return;

  if (!meta_window_located_on_workspace (window, workspace))
    {
      /* windows on other workspaces are necessarily obscured */
      obscured = TRUE;
    }
  else if (window->minimized)
    {
      obscured = TRUE;
    }
  else
    {
      meta_window_get_frame_rect (window, &candidate_rect);

      /* The stack is sorted with the top windows first. */

      while (stack != NULL && stack->data != window)
        {
          other_window = stack->data;
          stack = stack->next;

          if (meta_window_located_on_workspace (other_window, workspace))
            {
              meta_window_get_frame_rect (other_window, &other_rect);

              if (mtk_rectangle_overlap (&candidate_rect, &other_rect))
                {
                  obscured = TRUE;
                  break;
                }
            }
        }
    }

  if (obscured)
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Marking %s as needing attention",
                  window->desc);

      window->wm_state_demands_attention = TRUE;
      set_net_wm_state (window);
      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_DEMANDS_ATTENTION]);
      g_signal_emit_by_name (window->display, "window-demands-attention",
                             window);
    }
  else
    {
      /* If the window's in full view, there's no point setting the flag. */

      meta_topic (META_DEBUG_WINDOW_OPS,
                 "Not marking %s as needing attention because "
                 "it's in full view",
                 window->desc);
    }
}

void
meta_window_unset_demands_attention (MetaWindow *window)
{
  meta_topic (META_DEBUG_WINDOW_OPS,
              "Marking %s as not needing attention", window->desc);

  if (window->wm_state_demands_attention)
    {
      window->wm_state_demands_attention = FALSE;
      set_net_wm_state (window);
      g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_DEMANDS_ATTENTION]);
    }
}

/**
 * meta_window_get_frame: (skip)
 * @window: a #MetaWindow
 *
 */
MetaFrame *
meta_window_get_frame (MetaWindow *window)
{
  return window->frame;
}

/**
 * meta_window_appears_focused:
 * @window: a #MetaWindow
 *
 * Determines if the window should be drawn with a focused appearance.
 *
 * This is true for focused windows but also true for windows with a focused modal
 * dialog attached.
 *
 * Return value: %TRUE if the window should be drawn with a focused frame
 */
gboolean
meta_window_appears_focused (MetaWindow *window)
{
  return window->appears_focused;
}

gboolean
meta_window_has_focus (MetaWindow *window)
{
  return window->has_focus;
}

/**
 * meta_window_is_override_redirect:
 * @window: A #MetaWindow
 *
 * Returns: %TRUE if this window isn't managed by mutter; it will
 * control its own positioning and mutter won't draw decorations
 * among other things.  In X terminology this is "override redirect".
 */
gboolean
meta_window_is_override_redirect (MetaWindow *window)
{
  return window->override_redirect;
}

/**
 * meta_window_is_skip_taskbar:
 * @window: A #MetaWindow
 *
 * Gets whether this window should be ignored by task lists.
 *
 * Return value: %TRUE if the skip bar hint is set.
 */
gboolean
meta_window_is_skip_taskbar (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), FALSE);

  return window->skip_taskbar;
}

/**
 * meta_window_get_display:
 * @window: A #MetaWindow
 *
 * Returns: (transfer none): The display for @window
 */
MetaDisplay *
meta_window_get_display (MetaWindow *window)
{
  return window->display;
}

MetaWindowType
meta_window_get_window_type (MetaWindow *window)
{
  return window->type;
}

/**
 * meta_window_get_workspace:
 * @window: a #MetaWindow
 *
 * Gets the [class@Meta.Workspace] that the window is currently displayed on.
 *
 * If the window is on all workspaces, returns the currently active
 * workspace.
 *
 * Return value: (transfer none): the #MetaWorkspace for the window
 */
MetaWorkspace *
meta_window_get_workspace (MetaWindow *window)
{
  MetaWorkspaceManager *workspace_manager = window->display->workspace_manager;

  if (window->on_all_workspaces)
    return workspace_manager->active_workspace;
  else
    return window->workspace;
}

gboolean
meta_window_is_on_all_workspaces (MetaWindow *window)
{
  return window->on_all_workspaces;
}

gboolean
meta_window_is_hidden (MetaWindow *window)
{
  return window->hidden;
}

const char *
meta_window_get_description (MetaWindow *window)
{
  if (!window)
    return NULL;

  return window->desc;
}

/**
 * meta_window_get_wm_class:
 * @window: a #MetaWindow
 *
 * Return the current value of the name part of `WM_CLASS` X property.
 *
 * Returns: (nullable): the current value of the name part of `WM_CLASS` X
 * property
 */
const char *
meta_window_get_wm_class (MetaWindow *window)
{
  if (!window)
    return NULL;

  return window->res_class;
}

/**
 * meta_window_get_wm_class_instance:
 * @window: a #MetaWindow
 *
 * Return the current value of the instance part of `WM_CLASS` X property.
 *
 * Returns: (nullable): the current value of the instance part of `WM_CLASS` X
 * property.
 */
const char *
meta_window_get_wm_class_instance (MetaWindow *window)
{
  if (!window)
    return NULL;

  return window->res_name;
}

/**
 * meta_window_get_sandboxed_app_id:
 * @window: a #MetaWindow
 *
 * Gets an unique id for a sandboxed app (currently flatpaks and snaps are
 * supported).
 *
 * Returns: (transfer none) (nullable): the sandboxed application ID or %NULL
 **/
const char *
meta_window_get_sandboxed_app_id (MetaWindow *window)
{
  /* We're abusing this API here not to break the gnome shell assumptions
   * or adding a new function, to be renamed to generic names in new versions */
  return window->sandboxed_app_id;
}

/**
 * meta_window_get_gtk_theme_variant:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the theme variant or %NULL
 **/
const char *
meta_window_get_gtk_theme_variant (MetaWindow *window)
{
  return window->gtk_theme_variant;
}

/**
 * meta_window_get_gtk_application_id:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the application ID
 **/
const char *
meta_window_get_gtk_application_id (MetaWindow *window)
{
  return window->gtk_application_id;
}

/**
 * meta_window_get_gtk_unique_bus_name:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the unique name
 **/
const char *
meta_window_get_gtk_unique_bus_name (MetaWindow *window)
{
  return window->gtk_unique_bus_name;
}

/**
 * meta_window_get_gtk_application_object_path:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the object path
 **/
const char *
meta_window_get_gtk_application_object_path (MetaWindow *window)
{
  return window->gtk_application_object_path;
}

/**
 * meta_window_get_gtk_window_object_path:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the object path
 **/
const char *
meta_window_get_gtk_window_object_path (MetaWindow *window)
{
  return window->gtk_window_object_path;
}

/**
 * meta_window_get_gtk_app_menu_object_path:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the object path
 **/
const char *
meta_window_get_gtk_app_menu_object_path (MetaWindow *window)
{
  return window->gtk_app_menu_object_path;
}

/**
 * meta_window_get_gtk_menubar_object_path:
 * @window: a #MetaWindow
 *
 * Returns: (transfer none) (nullable): the object path
 **/
const char *
meta_window_get_gtk_menubar_object_path (MetaWindow *window)
{
  return window->gtk_menubar_object_path;
}

/**
 * meta_window_get_compositor_private:
 * @window: a #MetaWindow
 *
 * Gets the compositor's wrapper object for @window.
 *
 * Return value: (transfer none): the wrapper object.
 **/
GObject *
meta_window_get_compositor_private (MetaWindow *window)
{
  if (!window)
    return NULL;
  return window->compositor_private;
}

void
meta_window_set_compositor_private (MetaWindow *window, GObject *priv)
{
  if (!window)
    return;
  window->compositor_private = priv;
}

const char *
meta_window_get_role (MetaWindow *window)
{
  if (!window)
    return NULL;

  return window->role;
}

/**
 * meta_window_get_title:
 * @window: a #MetaWindow
 *
 * Returns: the current title of the window.
 */
const char *
meta_window_get_title (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), NULL);

  return window->title;
}

MetaStackLayer
meta_window_get_layer (MetaWindow *window)
{
  return window->layer;
}

/**
 * meta_window_get_transient_for:
 * @window: a #MetaWindow
 *
 * Returns the #MetaWindow for the window that is pointed to by the
 * WM_TRANSIENT_FOR hint on this window (see XGetTransientForHint()
 * or XSetTransientForHint()). Mutter keeps transient windows above their
 * parents. A typical usage of this hint is for a dialog that wants to stay
 * above its associated window.
 *
 * Returns: (transfer none) (nullable): the window this window is transient for,
 * or %NULL if the WM_TRANSIENT_FOR hint is unset or does not point to a
 * toplevel window that Mutter knows about.
 */
MetaWindow *
meta_window_get_transient_for (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), NULL);

  return window->transient_for;
}

/**
 * meta_window_get_pid:
 * @window: a #MetaWindow
 *
 * Returns the pid of the process that created this window, if available
 * to the windowing system.
 *
 * Note that the value returned by this is vulnerable to spoofing attacks
 * by the client.
 *
 * Return value: the pid, or 0 if not known.
 */
pid_t
meta_window_get_pid (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), 0);

  if (window->client_pid == 0)
    window->client_pid = META_WINDOW_GET_CLASS (window)->get_client_pid (window);

  return window->client_pid;
}

/**
 * meta_window_get_unit_cgroup:
 * @window: a #MetaWindow
 *
 * Returns: (nullable): a #GFile for the cgroup path, or %NULL.
 */
GFile *
meta_window_get_unit_cgroup (MetaWindow *window)
{
#ifdef HAVE_LIBSYSTEMD
  g_autofree char *contents = NULL;
  g_autofree char *complete_path = NULL;
  g_autofree char *unit_name = NULL;
  g_autofree char *unit_path = NULL;
  char *unit_end;
  pid_t pid;

  if (!window->has_valid_cgroup)
    return NULL;

  if (window->cgroup_path)
    return window->cgroup_path;

  pid = meta_window_get_pid (window);
  if (pid < 1)
    return NULL;

  if (sd_pid_get_cgroup (pid, &contents) < 0)
    {
      window->has_valid_cgroup = FALSE;
      return NULL;
    }
  g_strstrip (contents);

  complete_path = g_strdup_printf ("%s%s", "/sys/fs/cgroup", contents);

  if (sd_pid_get_user_unit (pid, &unit_name) < 0)
    {
      window->has_valid_cgroup = FALSE;
      return NULL;
    }
  g_strstrip (unit_name);

  unit_end = strstr (complete_path, unit_name) + strlen (unit_name);
  *unit_end = '\0';

  window->cgroup_path = g_file_new_for_path (complete_path);

  return window->cgroup_path;
#else
  return NULL;
#endif
}

gboolean
meta_window_unit_cgroup_equal (MetaWindow *window1,
                               MetaWindow *window2)
{
  GFile *window1_file, *window2_file;

  window1_file = meta_window_get_unit_cgroup (window1);
  window2_file = meta_window_get_unit_cgroup (window2);

  if (!window1_file || !window2_file)
    return FALSE;

  return g_file_equal (window1_file, window2_file);
}

/**
 * meta_window_is_remote:
 * @window: a #MetaWindow
 *
 * Returns: %TRUE if this window originates from a host
 * different from the one running mutter.
 */
gboolean
meta_window_is_remote (MetaWindow *window)
{
  return window->is_remote;
}

/**
 * meta_window_get_mutter_hints:
 * @window: a #MetaWindow
 *
 * Gets the current value of the _MUTTER_HINTS property.
 *
 * The purpose of the hints is to allow fine-tuning of the Window Manager and
 * Compositor behaviour on per-window basis, and is intended primarily for
 * hints that are plugin-specific.
 *
 * The property is a list of colon-separated key=value pairs. The key names for
 * any plugin-specific hints must be suitably namespaced to allow for shared
 * use; 'mutter-' key prefix is reserved for internal use, and must not be used
 * by plugins.
 *
 * Returns: (transfer none) (nullable): the _MUTTER_HINTS string, or %NULL if no
 * hints are set.
 */
const char *
meta_window_get_mutter_hints (MetaWindow *window)
{
  g_return_val_if_fail (META_IS_WINDOW (window), NULL);

  return window->mutter_hints;
}

/**
 * meta_window_get_frame_type:
 * @window: a #MetaWindow
 *
 * Gets the type of window decorations that should be used for this window.
 *
 * Return value: the frame type
 */
MetaFrameType
meta_window_get_frame_type (MetaWindow *window)
{
  MetaFrameType base_type = META_FRAME_TYPE_LAST;

  switch (window->type)
    {
    case META_WINDOW_NORMAL:
      base_type = META_FRAME_TYPE_NORMAL;
      break;

    case META_WINDOW_DIALOG:
      base_type = META_FRAME_TYPE_DIALOG;
      break;

    case META_WINDOW_MODAL_DIALOG:
      if (meta_window_is_attached_dialog (window))
        base_type = META_FRAME_TYPE_ATTACHED;
      else
        base_type = META_FRAME_TYPE_MODAL_DIALOG;
      break;

    case META_WINDOW_MENU:
      base_type = META_FRAME_TYPE_MENU;
      break;

    case META_WINDOW_UTILITY:
      base_type = META_FRAME_TYPE_UTILITY;
      break;

    case META_WINDOW_DESKTOP:
    case META_WINDOW_DOCK:
    case META_WINDOW_TOOLBAR:
    case META_WINDOW_SPLASHSCREEN:
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_DND:
    case META_WINDOW_OVERRIDE_OTHER:
      /* No frame */
      base_type = META_FRAME_TYPE_LAST;
      break;
    }

  if (base_type == META_FRAME_TYPE_LAST)
    {
      /* can't add border if undecorated */
      return META_FRAME_TYPE_LAST;
    }
  else if (window->border_only)
    {
      /* override base frame type */
      return META_FRAME_TYPE_BORDER;
    }
  else
    {
      return base_type;
    }
}

/**
 * meta_window_get_frame_bounds:
 * @window: a #MetaWindow
 *
 * Gets a region representing the outer bounds of the window's frame.
 *
 * Return value: (transfer none) (nullable): a #MtkRegion
 *  holding the outer bounds of the window, or %NULL if the window
 *  doesn't have a frame.
 */
MtkRegion *
meta_window_get_frame_bounds (MetaWindow *window)
{
  if (!window->frame_bounds)
    {
      if (window->frame)
        window->frame_bounds = meta_frame_get_frame_bounds (window->frame);
    }

  return window->frame_bounds;
}

/**
 * meta_window_is_attached_dialog:
 * @window: a #MetaWindow
 *
 * Tests if @window should be attached to its parent window.
 *
 * If the `attach_modal_dialogs` option is not enabled, this will
 * always return %FALSE.
 *
 * Return value: whether @window should be attached to its parent
 */
gboolean
meta_window_is_attached_dialog (MetaWindow *window)
{
  return window->attached;
}

static gboolean
has_attached_foreach_func (MetaWindow *window,
                           void       *data)
{
  gboolean *is_attached = data;

  *is_attached = window->attached && !window->unmanaging;

  if (*is_attached)
    return FALSE;

  return TRUE;
}


/**
 * meta_window_has_attached_dialogs:
 * @window: a #MetaWindow
 *
 * Tests if @window has any transients attached to it.
 *
 * If the `attach_modal_dialogs` option is not enabled, this will
 * always return %FALSE.
 *
 * Return value: whether @window has attached transients
 */
gboolean
meta_window_has_attached_dialogs (MetaWindow *window)
{
  gboolean has_attached = FALSE;

  meta_window_foreach_transient (window,
                                 has_attached_foreach_func,
                                 &has_attached);
  return has_attached;
}

static gboolean
has_modals_foreach_func (MetaWindow *window,
                         void       *data)
{
  gboolean *is_modal = data;

  *is_modal = window->type == META_WINDOW_MODAL_DIALOG && !window->unmanaging;

  if (*is_modal)
    return FALSE;

  return TRUE;
}

/**
 * meta_window_has_modals:
 * @window: a #MetaWindow
 *
 * Return value: whether @window has any modal transients
 */
gboolean
meta_window_has_modals (MetaWindow *window)
{
  gboolean has_modals = FALSE;

  meta_window_foreach_transient (window, has_modals_foreach_func, &has_modals);
  return has_modals;
}

/**
 * meta_window_get_tile_match:
 * @window: a #MetaWindow
 *
 * Returns the matching tiled window on the same monitor as @window. This is
 * the topmost tiled window in a complementary tile mode that is:
 *
 *  - on the same monitor;
 *  - on the same workspace;
 *  - spanning the remaining monitor width;
 *  - there is no 3rd window stacked between both tiled windows that's
 *    partially visible in the common edge.
 *
 * Return value: (transfer none) (nullable): the matching tiled window or
 * %NULL if it doesn't exist.
 */
MetaWindow *
meta_window_get_tile_match (MetaWindow *window)
{
  return window->tile_match;
}

void
meta_window_compute_tile_match (MetaWindow *window)
{
  window->tile_match = meta_window_find_tile_match (window, window->tile_mode);
}

static MetaWindow *
meta_window_find_tile_match (MetaWindow   *window,
                             MetaTileMode  current_mode)
{
  MetaWindow *match;
  MetaStack *stack;
  MetaTileMode match_tile_mode = META_TILE_NONE;

  if (window->minimized)
    return NULL;

  if (current_mode == META_TILE_LEFT)
    match_tile_mode = META_TILE_RIGHT;
  else if (current_mode == META_TILE_RIGHT)
    match_tile_mode = META_TILE_LEFT;
  else
    return NULL;

  stack = window->display->stack;

  for (match = meta_stack_get_top (stack);
       match;
       match = meta_stack_get_below (stack, match, FALSE))
    {
      if (!match->minimized &&
          match->tile_mode == match_tile_mode &&
          match->tile_monitor_number == window->tile_monitor_number &&
          meta_window_get_workspace (match) == meta_window_get_workspace (window))
        break;
    }

  if (match)
    {
      MetaWindow *above, *bottommost, *topmost;
      MtkRectangle above_rect, bottommost_rect, topmost_rect;
      MetaWindowDrag *window_drag;

      if (meta_stack_windows_cmp (window->display->stack, match, window) > 0)
        {
          topmost = match;
          bottommost = window;
        }
      else
        {
          topmost = window;
          bottommost = match;
        }

      meta_window_get_frame_rect (bottommost, &bottommost_rect);
      meta_window_get_frame_rect (topmost, &topmost_rect);

      window_drag =
        meta_compositor_get_current_window_drag (window->display->compositor);

      /*
       * If we are looking for a tile match while actually being tiled,
       * rather than a match for a potential tile mode, then discard
       * windows with too much gap or overlap
       */
      if (window->tile_mode == current_mode &&
          !(window_drag &&
            meta_grab_op_is_resizing (meta_window_drag_get_grab_op (window_drag)) &&
            meta_window_drag_get_window (window_drag) == window &&
            window->tile_match != NULL))
        {
          int threshold = meta_prefs_get_drag_threshold ();
          if (ABS (topmost_rect.x - bottommost_rect.x - bottommost_rect.width) > threshold &&
              ABS (bottommost_rect.x - topmost_rect.x - topmost_rect.width) > threshold)
            return NULL;
        }

      /*
       * If there's a window stacked in between which is partially visible
       * behind the topmost tile we don't consider the tiles to match.
       */
      for (above = meta_stack_get_above (stack, bottommost, FALSE);
           above && above != topmost;
           above = meta_stack_get_above (stack, above, FALSE))
        {
          if (above->minimized ||
              above->monitor != window->monitor ||
              meta_window_get_workspace (above) != meta_window_get_workspace (window))
            continue;

          meta_window_get_frame_rect (above, &above_rect);

          if (mtk_rectangle_overlap (&above_rect, &bottommost_rect) &&
              mtk_rectangle_overlap (&above_rect, &topmost_rect))
            return NULL;
        }
    }

  return match;
}

void
meta_window_set_title (MetaWindow *window,
                       const char *title)
{
  g_free (window->title);
  window->title = g_strdup (title);

  meta_window_update_desc (window);

  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_TITLE]);
}

void
meta_window_set_wm_class (MetaWindow *window,
                          const char *wm_class,
                          const char *wm_instance)
{
  g_free (window->res_class);
  g_free (window->res_name);

  window->res_name = g_strdup (wm_instance);
  window->res_class = g_strdup (wm_class);

  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_WM_CLASS]);
}

void
meta_window_set_gtk_dbus_properties (MetaWindow *window,
                                     const char *application_id,
                                     const char *unique_bus_name,
                                     const char *appmenu_path,
                                     const char *menubar_path,
                                     const char *application_object_path,
                                     const char *window_object_path)
{
  g_object_freeze_notify (G_OBJECT (window));

  g_free (window->gtk_application_id);
  window->gtk_application_id = g_strdup (application_id);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_APPLICATION_ID]);

  g_free (window->gtk_unique_bus_name);
  window->gtk_unique_bus_name = g_strdup (unique_bus_name);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_UNIQUE_BUS_NAME]);

  g_free (window->gtk_app_menu_object_path);
  window->gtk_app_menu_object_path = g_strdup (appmenu_path);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_APP_MENU_OBJECT_PATH]);

  g_free (window->gtk_menubar_object_path);
  window->gtk_menubar_object_path = g_strdup (menubar_path);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_MENUBAR_OBJECT_PATH]);

  g_free (window->gtk_application_object_path);
  window->gtk_application_object_path = g_strdup (application_object_path);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_APPLICATION_OBJECT_PATH]);

  g_free (window->gtk_window_object_path);
  window->gtk_window_object_path = g_strdup (window_object_path);
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_GTK_WINDOW_OBJECT_PATH]);

  g_object_thaw_notify (G_OBJECT (window));
}

static gboolean
check_transient_for_loop (MetaWindow *window,
                          MetaWindow *parent)
{
  while (parent)
    {
      if (parent == window)
        return TRUE;
      parent = parent->transient_for;
    }

  return FALSE;
}

gboolean
meta_window_has_transient_type (MetaWindow *window)
{
  return (window->type == META_WINDOW_DIALOG ||
          window->type == META_WINDOW_MODAL_DIALOG ||
          window->type == META_WINDOW_TOOLBAR ||
          window->type == META_WINDOW_MENU ||
          window->type == META_WINDOW_UTILITY);
}

void
meta_window_set_transient_for (MetaWindow *window,
                               MetaWindow *parent)
{
  MetaWindowClass *klass = META_WINDOW_GET_CLASS (window);

  if (check_transient_for_loop (window, parent))
    {
      meta_warning ("Setting %s transient for %s would create a loop.",
                    window->desc, parent->desc);
      return;
    }

  if (window->appears_focused && window->transient_for != NULL)
    meta_window_propagate_focus_appearance (window, FALSE);

  if (!klass->set_transient_for (window, parent))
    return;

  if (window->attached && parent == NULL)
    {
      guint32 timestamp;

      timestamp =
        meta_display_get_current_time_roundtrip (window->display);
      meta_window_delete (window, timestamp);
      return;
    }

  g_set_object (&window->transient_for, parent);

  /* update stacking constraints */
  if (!window->override_redirect)
    meta_stack_update_transient (window->display->stack, window);

  if (!window->constructing && !window->override_redirect)
    meta_window_queue (window, META_QUEUE_MOVE_RESIZE | META_QUEUE_CALC_SHOWING);

  if (window->appears_focused && window->transient_for != NULL)
    meta_window_propagate_focus_appearance (window, TRUE);
}

void
meta_window_set_opacity (MetaWindow *window,
                         guint8      opacity)
{
  window->opacity = opacity;

  meta_compositor_window_opacity_changed (window->display->compositor, window);
}

static gboolean
window_has_pointer_wayland (MetaWindow *window)
{
  ClutterSeat *seat;
  ClutterInputDevice *dev;
  ClutterStage *stage;
  ClutterActor *pointer_actor, *window_actor;

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  dev = clutter_seat_get_pointer (seat);
  stage = CLUTTER_STAGE (meta_backend_get_stage (backend_from_window (window)));
  pointer_actor = clutter_stage_get_device_actor (stage, dev, NULL);
  window_actor = CLUTTER_ACTOR (meta_window_get_compositor_private (window));

  return pointer_actor && clutter_actor_contains (window_actor, pointer_actor);
}

gboolean
meta_window_has_pointer (MetaWindow *window)
{
  if (meta_is_wayland_compositor ())
    return window_has_pointer_wayland (window);
#ifdef HAVE_X11_CLIENT
  else
    return meta_window_x11_has_pointer (window);
#else
  g_assert_not_reached ();
#endif
}

gboolean
meta_window_handle_ungrabbed_event (MetaWindow         *window,
                                    const ClutterEvent *event)
{
  MetaDisplay *display = window->display;
  gboolean unmodified;
  gboolean is_window_grab;
  gboolean is_window_button_grab_allowed;
  ClutterModifierType grab_mods, event_mods;
  ClutterInputDevice *source;
  ClutterEventType event_type;
  uint32_t time_ms;
  gfloat x, y;
  guint button;

  if (window->unmanaging)
    return CLUTTER_EVENT_PROPAGATE;

  event_type = clutter_event_type (event);
  time_ms = clutter_event_get_time (event);

  if (event_type != CLUTTER_BUTTON_PRESS &&
      event_type != CLUTTER_TOUCH_BEGIN)
    return CLUTTER_EVENT_PROPAGATE;

  if (event_type == CLUTTER_TOUCH_BEGIN)
    {
      ClutterEventSequence *sequence;

      button = 1;
      sequence = clutter_event_get_event_sequence (event);
      if (!meta_display_is_pointer_emulating_sequence (window->display, sequence))
        return CLUTTER_EVENT_PROPAGATE;
    }
  else
    button = clutter_event_get_button (event);

  /* Some windows might not ask for input, in which case we might be here
   * because we selected for ButtonPress on the root window. In that case,
   * we have to take special care not to act for an override-redirect window.
   */
  if (window->override_redirect)
    return CLUTTER_EVENT_PROPAGATE;

  /* Don't focus panels--they must explicitly request focus.
   * See bug 160470
   */
  if (window->type != META_WINDOW_DOCK)
    {
      meta_topic (META_DEBUG_FOCUS,
                  "Focusing %s due to button %u press (display.c)",
                  window->desc, button);
      meta_window_focus (window, time_ms);
      meta_window_check_alive (window, time_ms);
    }

  /* We have three passive button grabs:
   * - on any button, without modifiers => focuses and maybe raises the window
   * - on resize button, with modifiers => start an interactive resizing
   *   (normally <Super>middle)
   * - on move button, with modifiers => start an interactive move
   *   (normally <Super>left)
   * - on menu button, with modifiers => show the window menu
   *   (normally <Super>right)
   *
   * We may get here because we actually have a button
   * grab on the window, or because we're a wayland
   * compositor and thus we see all the events, so we
   * need to check if the event is interesting.
   * We want an event that is not modified for a window.
   *
   * We may have other events on the window, for example
   * a click on a frame button, but that's not for us to
   * care about. Just let the event through.
   */

  grab_mods = meta_display_get_compositor_modifiers (display);
  event_mods = clutter_event_get_state (event);
  unmodified = (event_mods & grab_mods) == 0;
  source = clutter_event_get_source_device (event);
  is_window_button_grab_allowed = !display->focus_window ||
    !meta_window_shortcuts_inhibited (display->focus_window, source);
  is_window_grab = (is_window_button_grab_allowed &&
                    ((event_mods & grab_mods) == grab_mods));

  clutter_event_get_coords (event, &x, &y);

  if (unmodified)
    {
      if (meta_prefs_get_raise_on_click ())
        meta_window_raise (window);
      else
        meta_topic (META_DEBUG_FOCUS,
                    "Not raising window on click due to don't-raise-on-click option");
    }
  else if (is_window_grab && (int) button == meta_prefs_get_mouse_button_resize ())
    {
      if (window->has_resize_func)
        {
          gboolean north, south;
          gboolean west, east;
          MtkRectangle frame_rect;
          MetaGrabOp op = META_GRAB_OP_WINDOW_BASE;

          meta_window_get_frame_rect (window, &frame_rect);

          west = x < (frame_rect.x + 1 * frame_rect.width / 3);
          east = x > (frame_rect.x + 2 * frame_rect.width / 3);
          north = y < (frame_rect.y + 1 * frame_rect.height / 3);
          south = y > (frame_rect.y + 2 * frame_rect.height / 3);

          if (west)
            op |= META_GRAB_OP_WINDOW_DIR_WEST;
          if (east)
            op |= META_GRAB_OP_WINDOW_DIR_EAST;
          if (north)
            op |= META_GRAB_OP_WINDOW_DIR_NORTH;
          if (south)
            op |= META_GRAB_OP_WINDOW_DIR_SOUTH;

          if (op != META_GRAB_OP_WINDOW_BASE)
            {
              op |= META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED;
              if (meta_window_begin_grab_op (window,
                                             op,
                                             clutter_event_get_device (event),
                                             clutter_event_get_event_sequence (event),
                                             time_ms,
                                             NULL))
                return CLUTTER_EVENT_STOP;
            }
        }
    }
  else if (is_window_grab && (int) button == meta_prefs_get_mouse_button_menu ())
    {
      if (meta_prefs_get_raise_on_click ())
        meta_window_raise (window);

      meta_window_show_menu (window,
                             META_WINDOW_MENU_WM,
                             x, y);

      return CLUTTER_EVENT_STOP;
    }
  else if (is_window_grab && (int) button == 1)
    {
      if (window->has_move_func)
        {
          if (meta_window_begin_grab_op (window,
                                         META_GRAB_OP_MOVING |
                                         META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED,
                                         clutter_event_get_device (event),
                                         clutter_event_get_event_sequence (event),
                                         time_ms,
                                         NULL))
            return CLUTTER_EVENT_STOP;
        }
    }

  return CLUTTER_EVENT_PROPAGATE;
}

gboolean
meta_window_can_maximize (MetaWindow *window)
{
  return window->has_maximize_func;
}

gboolean
meta_window_can_minimize (MetaWindow *window)
{
  return window->has_minimize_func;
}

gboolean
meta_window_can_close (MetaWindow *window)
{
  return window->has_close_func;
}

gboolean
meta_window_is_always_on_all_workspaces (MetaWindow *window)
{
  return window->always_sticky;
}

gboolean
meta_window_is_above (MetaWindow *window)
{
  return window->wm_state_above;
}

gboolean
meta_window_allows_move (MetaWindow *window)
{
  return META_WINDOW_ALLOWS_MOVE (window);
}

gboolean
meta_window_allows_resize (MetaWindow *window)
{
  return META_WINDOW_ALLOWS_RESIZE (window);
}

void
meta_window_set_urgent (MetaWindow *window,
                        gboolean    urgent)
{
  if (window->urgent == urgent)
    return;

  window->urgent = urgent;
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_URGENT]);

  if (urgent)
    g_signal_emit_by_name (window->display, "window-marked-urgent", window);
}

void
meta_window_grab_op_began (MetaWindow *window,
                           MetaGrabOp  op)
{
  META_WINDOW_GET_CLASS (window)->grab_op_began (window, op);
}

void
meta_window_grab_op_ended (MetaWindow *window,
                           MetaGrabOp  op)
{
  META_WINDOW_GET_CLASS (window)->grab_op_ended (window, op);
}

void
meta_window_emit_size_changed (MetaWindow *window)
{
  g_signal_emit (window, window_signals[SIZE_CHANGED], 0);
}

MetaPlacementRule *
meta_window_get_placement_rule (MetaWindow *window)
{
  return window->placement.rule;
}

void
meta_window_force_restore_shortcuts (MetaWindow         *window,
                                     ClutterInputDevice *source)
{
  META_WINDOW_GET_CLASS (window)->force_restore_shortcuts (window, source);
}

gboolean
meta_window_shortcuts_inhibited (MetaWindow         *window,
                                 ClutterInputDevice *source)
{
  return META_WINDOW_GET_CLASS (window)->shortcuts_inhibited (window, source);
}

gboolean
meta_window_is_focusable (MetaWindow *window)
{
  g_return_val_if_fail (!window->unmanaging, FALSE);

  return META_WINDOW_GET_CLASS (window)->is_focusable (window);
}

gboolean
meta_window_can_ping (MetaWindow *window)
{
  g_return_val_if_fail (!window->unmanaging, FALSE);

  return META_WINDOW_GET_CLASS (window)->can_ping (window);
}

gboolean
meta_window_is_stackable (MetaWindow *window)
{
  return META_WINDOW_GET_CLASS (window)->is_stackable (window);
}

gboolean
meta_window_is_focus_async (MetaWindow *window)
{
  return META_WINDOW_GET_CLASS (window)->is_focus_async (window);
}

MetaStackLayer
meta_window_calculate_layer (MetaWindow *window)
{
  return META_WINDOW_GET_CLASS (window)->calculate_layer (window);
}

#ifdef HAVE_WAYLAND
MetaWaylandSurface *
meta_window_get_wayland_surface (MetaWindow *window)
{
  MetaWindowClass *klass = META_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (klass->get_wayland_surface != NULL, NULL);

  return klass->get_wayland_surface (window);
}
#endif

/**
 * meta_window_get_id:
 * @window: a #MetaWindow
 *
 * Returns the window id associated with window.
 *
 * Returns: The window id
 */
uint64_t
meta_window_get_id (MetaWindow *window)
{
  return window->id;
}

/**
 * meta_window_get_client_type:
 * @window: a #MetaWindow
 *
 * Returns the #MetaWindowClientType of the window.
 *
 * Returns: The root ancestor window
 */
MetaWindowClientType
meta_window_get_client_type (MetaWindow *window)
{
  return window->client_type;
}

static gboolean
meta_window_close_dialog_timeout (MetaWindow *window)
{
  meta_window_show_close_dialog (window);
  window->close_dialog_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

void
meta_window_ensure_close_dialog_timeout (MetaWindow *window)
{
  guint check_alive_timeout = meta_prefs_get_check_alive_timeout ();

  if (window->is_alive)
    return;
  if (window->close_dialog_timeout_id != 0)
    return;
  if (check_alive_timeout == 0)
    return;

  window->close_dialog_timeout_id =
    g_timeout_add (check_alive_timeout,
                   (GSourceFunc) meta_window_close_dialog_timeout,
                   window);
  g_source_set_name_by_id (window->close_dialog_timeout_id,
                           "[mutter] meta_window_close_dialog_timeout");
}

void
meta_window_set_alive (MetaWindow *window,
                       gboolean    is_alive)
{
  if (window->is_alive == is_alive)
    return;

  window->is_alive = is_alive;
  g_object_notify_by_pspec (G_OBJECT (window), obj_props[PROP_IS_ALIVE]);

  if (is_alive)
    {
      g_clear_handle_id (&window->close_dialog_timeout_id, g_source_remove);
      meta_window_hide_close_dialog (window);
    }
}

gboolean
meta_window_get_alive (MetaWindow *window)
{
  return window->is_alive;
}

gboolean
meta_window_calculate_bounds (MetaWindow *window,
                              int        *bounds_width,
                              int        *bounds_height)
{
  MetaLogicalMonitor *main_monitor;

  main_monitor = meta_window_get_main_logical_monitor (window);
  if (main_monitor)
    {
      MtkRectangle work_area;

      meta_window_get_work_area_for_logical_monitor (window,
                                                     main_monitor,
                                                     &work_area);

      *bounds_width = work_area.width;
      *bounds_height = work_area.height;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

int
meta_get_window_suspend_timeout_s (void)
{
  return SUSPEND_HIDDEN_TIMEOUT_S;
}
