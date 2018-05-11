/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
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
 * SECTION:screen
 * @title: MetaScreen
 * @short_description: Mutter X screen handler
 */

#include <config.h>
#include "screen-private.h"
#include <meta/main.h>
#include "util-private.h"
#include <meta/errors.h>
#include "window-private.h"
#include "frame.h"
#include <meta/prefs.h>
#include "workspace-private.h"
#include "keybindings-private.h"
#include "stack.h"
#include <meta/compositor.h>
#include <meta/meta-enum-types.h>
#include "core.h"
#include "meta-cursor-tracker-private.h"
#include "boxes-private.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"

#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xcomposite.h>

#include <X11/Xatom.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "x11/window-x11.h"
#include "x11/xprops.h"

#include "backends/x11/meta-backend-x11.h"
#include "backends/meta-cursor-sprite-xcursor.h"

static char* get_screen_name (MetaDisplay *display,
                              int          number);

static void update_num_workspaces  (MetaScreen *screen,
                                    guint32     timestamp);
static void set_workspace_names    (MetaScreen *screen);
static void prefs_changed_callback (MetaPreference pref,
                                    gpointer       data);

static void set_desktop_geometry_hint (MetaScreen *screen);
static void set_desktop_viewport_hint (MetaScreen *screen);

static void on_monitors_changed_internal (MetaMonitorManager *manager,
                                          MetaScreen         *screen);
static void on_monitors_changed          (MetaMonitorManager *manager,
                                          MetaScreen         *screen);

enum
{
  PROP_N_WORKSPACES = 1,
};

enum
{
  RESTACKED,
  WORKSPACE_ADDED,
  WORKSPACE_REMOVED,
  WORKSPACE_SWITCHED,
  WINDOW_ENTERED_MONITOR,
  WINDOW_LEFT_MONITOR,
  STARTUP_SEQUENCE_CHANGED,
  WORKAREAS_CHANGED,
  MONITORS_CHANGED,
  IN_FULLSCREEN_CHANGED,

  LAST_SIGNAL
};

static guint screen_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (MetaScreen, meta_screen, G_TYPE_OBJECT);

static GQuark quark_screen_x11_logical_monitor_data = 0;

typedef struct _MetaScreenX11LogicalMonitorData
{
  int xinerama_index;
} MetaScreenX11LogicalMonitorData;

static void
meta_screen_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
#if 0
  MetaScreen *screen = META_SCREEN (object);
#endif

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_screen_get_property (GObject      *object,
                          guint         prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  MetaScreen *screen = META_SCREEN (object);

  switch (prop_id)
    {
    case PROP_N_WORKSPACES:
      g_value_set_int (value, meta_screen_get_n_workspaces (screen));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_screen_finalize (GObject *object)
{
  /* Actual freeing done in meta_screen_free() for now */
}

static void
meta_screen_class_init (MetaScreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec   *pspec;

  object_class->get_property = meta_screen_get_property;
  object_class->set_property = meta_screen_set_property;
  object_class->finalize = meta_screen_finalize;

  screen_signals[RESTACKED] =
    g_signal_new ("restacked",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaScreenClass, restacked),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  pspec = g_param_spec_int ("n-workspaces",
                            "N Workspaces",
                            "Number of workspaces",
                            1, G_MAXINT, 1,
                            G_PARAM_READABLE);

  screen_signals[WORKSPACE_ADDED] =
    g_signal_new ("workspace-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_INT);

  screen_signals[WORKSPACE_REMOVED] =
    g_signal_new ("workspace-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_INT);

  screen_signals[WORKSPACE_SWITCHED] =
    g_signal_new ("workspace-switched",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  3,
                  G_TYPE_INT,
                  G_TYPE_INT,
                  META_TYPE_MOTION_DIRECTION);

  screen_signals[WINDOW_ENTERED_MONITOR] =
    g_signal_new ("window-entered-monitor",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  META_TYPE_WINDOW);

  screen_signals[WINDOW_LEFT_MONITOR] =
    g_signal_new ("window-left-monitor",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  META_TYPE_WINDOW);

  screen_signals[STARTUP_SEQUENCE_CHANGED] =
    g_signal_new ("startup-sequence-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  screen_signals[WORKAREAS_CHANGED] =
    g_signal_new ("workareas-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaScreenClass, workareas_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  screen_signals[MONITORS_CHANGED] =
    g_signal_new ("monitors-changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (MetaScreenClass, monitors_changed),
          NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  screen_signals[IN_FULLSCREEN_CHANGED] =
    g_signal_new ("in-fullscreen-changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);

  g_object_class_install_property (object_class,
                                   PROP_N_WORKSPACES,
                                   pspec);

  quark_screen_x11_logical_monitor_data =
    g_quark_from_static_string ("-meta-screen-logical-monitor-x11-data");
}

static void
meta_screen_init (MetaScreen *screen)
{
}

static int
set_wm_check_hint (MetaScreen *screen)
{
  unsigned long data[1];

  g_return_val_if_fail (screen->display->leader_window != None, 0);

  data[0] = screen->display->leader_window;

  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);

  return Success;
}

static void
unset_wm_check_hint (MetaScreen *screen)
{
  XDeleteProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SUPPORTING_WM_CHECK);
}

static int
set_supported_hint (MetaScreen *screen)
{
  Atom atoms[] = {
#define EWMH_ATOMS_ONLY
#define item(x)  screen->display->atom_##x,
#include <x11/atomnames.h>
#undef item
#undef EWMH_ATOMS_ONLY

    screen->display->atom__GTK_FRAME_EXTENTS,
    screen->display->atom__GTK_SHOW_WINDOW_MENU,
    screen->display->atom__GTK_EDGE_CONSTRAINTS,
  };

  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SUPPORTED,
                   XA_ATOM,
                   32, PropModeReplace,
                   (guchar*) atoms, G_N_ELEMENTS(atoms));

  return Success;
}

static int
set_wm_icon_size_hint (MetaScreen *screen)
{
#define N_VALS 6
  gulong vals[N_VALS];

  /* We've bumped the real icon size up to 96x96, but
   * we really should not add these sorts of constraints
   * on clients still using the legacy WM_HINTS interface.
   */
#define LEGACY_ICON_SIZE 32

  /* min width, min height, max w, max h, width inc, height inc */
  vals[0] = LEGACY_ICON_SIZE;
  vals[1] = LEGACY_ICON_SIZE;
  vals[2] = LEGACY_ICON_SIZE;
  vals[3] = LEGACY_ICON_SIZE;
  vals[4] = 0;
  vals[5] = 0;
#undef LEGACY_ICON_SIZE

  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom_WM_ICON_SIZE,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) vals, N_VALS);

  return Success;
#undef N_VALS
}

static MetaScreenX11LogicalMonitorData *
get_screen_x11_logical_monitor_data (MetaLogicalMonitor *logical_monitor)
{
  return g_object_get_qdata (G_OBJECT (logical_monitor),
                             quark_screen_x11_logical_monitor_data);
}

static MetaScreenX11LogicalMonitorData *
ensure_screen_x11_logical_monitor_data (MetaLogicalMonitor *logical_monitor)
{
  MetaScreenX11LogicalMonitorData *data;

  data = get_screen_x11_logical_monitor_data (logical_monitor);
  if (data)
    return data;

  data = g_new0 (MetaScreenX11LogicalMonitorData, 1);
  g_object_set_qdata_full (G_OBJECT (logical_monitor),
                           quark_screen_x11_logical_monitor_data,
                           data,
                           g_free);

  return data;
}

static void
meta_screen_ensure_xinerama_indices (MetaScreen *screen)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;
  XineramaScreenInfo *infos;
  int n_infos, j;

  if (screen->has_xinerama_indices)
    return;

  screen->has_xinerama_indices = TRUE;

  if (!XineramaIsActive (screen->display->xdisplay))
    return;

  infos = XineramaQueryScreens (screen->display->xdisplay, &n_infos);
  if (n_infos <= 0 || infos == NULL)
    {
      meta_XFree (infos);
      return;
    }

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      for (j = 0; j < n_infos; ++j)
        {
          if (logical_monitor->rect.x == infos[j].x_org &&
              logical_monitor->rect.y == infos[j].y_org &&
              logical_monitor->rect.width == infos[j].width &&
              logical_monitor->rect.height == infos[j].height)
            {
              MetaScreenX11LogicalMonitorData *logical_monitor_data;

              logical_monitor_data =
                ensure_screen_x11_logical_monitor_data (logical_monitor);
              logical_monitor_data->xinerama_index = j;
            }
        }
    }

  meta_XFree (infos);
}

int
meta_screen_logical_monitor_to_xinerama_index (MetaScreen         *screen,
                                               MetaLogicalMonitor *logical_monitor)
{
  MetaScreenX11LogicalMonitorData *logical_monitor_data;

  g_return_val_if_fail (logical_monitor, -1);

  meta_screen_ensure_xinerama_indices (screen);

  logical_monitor_data = get_screen_x11_logical_monitor_data (logical_monitor);

  return logical_monitor_data->xinerama_index;
}

MetaLogicalMonitor *
meta_screen_xinerama_index_to_logical_monitor (MetaScreen *screen,
                                               int         xinerama_index)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  meta_screen_ensure_xinerama_indices (screen);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaScreenX11LogicalMonitorData *logical_monitor_data;

      logical_monitor_data =
        ensure_screen_x11_logical_monitor_data (logical_monitor);

      if (logical_monitor_data->xinerama_index == xinerama_index)
        return logical_monitor;
    }

  return NULL;
}

static void
reload_logical_monitors (MetaScreen *screen)
{
  GList *l;

  for (l = screen->workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *space = l->data;
      meta_workspace_invalidate_work_area (space);
    }

  screen->has_xinerama_indices = FALSE;
}

/* The guard window allows us to leave minimized windows mapped so
 * that compositor code may provide live previews of them.
 * Instead of being unmapped/withdrawn, they get pushed underneath
 * the guard window. We also select events on the guard window, which
 * should effectively be forwarded to events on the background actor,
 * providing that the scene graph is set up correctly.
 */
static Window
create_guard_window (Display *xdisplay, MetaScreen *screen)
{
  XSetWindowAttributes attributes;
  Window guard_window;
  gulong create_serial;

  attributes.event_mask = NoEventMask;
  attributes.override_redirect = True;

  /* We have to call record_add() after we have the new window ID,
   * so save the serial for the CreateWindow request until then */
  create_serial = XNextRequest(xdisplay);
  guard_window =
    XCreateWindow (xdisplay,
		   screen->xroot,
		   0, /* x */
		   0, /* y */
		   screen->rect.width,
		   screen->rect.height,
		   0, /* border width */
		   0, /* depth */
		   InputOnly, /* class */
		   CopyFromParent, /* visual */
		   CWEventMask|CWOverrideRedirect,
		   &attributes);

  /* https://bugzilla.gnome.org/show_bug.cgi?id=710346 */
  XStoreName (xdisplay, guard_window, "mutter guard window");

  {
    if (!meta_is_wayland_compositor ())
      {
        MetaBackendX11 *backend = META_BACKEND_X11 (meta_get_backend ());
        Display *backend_xdisplay = meta_backend_x11_get_xdisplay (backend);
        unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
        XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

        XISetMask (mask.mask, XI_ButtonPress);
        XISetMask (mask.mask, XI_ButtonRelease);
        XISetMask (mask.mask, XI_Motion);

        /* Sync on the connection we created the window on to
         * make sure it's created before we select on it on the
         * backend connection. */
        XSync (xdisplay, False);

        XISelectEvents (backend_xdisplay, guard_window, &mask, 1);
      }
  }

  meta_stack_tracker_record_add (screen->stack_tracker,
                                 guard_window,
                                 create_serial);

  meta_stack_tracker_lower (screen->stack_tracker,
                            guard_window);
  XMapWindow (xdisplay, guard_window);
  return guard_window;
}

static Window
take_manager_selection (MetaDisplay *display,
                        Window       xroot,
                        Atom         manager_atom,
                        int          timestamp,
                        gboolean     should_replace)
{
  Display *xdisplay = display->xdisplay;
  Window current_owner, new_owner;

  current_owner = XGetSelectionOwner (xdisplay, manager_atom);
  if (current_owner != None)
    {
      XSetWindowAttributes attrs;

      if (should_replace)
        {
          /* We want to find out when the current selection owner dies */
          meta_error_trap_push (display);
          attrs.event_mask = StructureNotifyMask;
          XChangeWindowAttributes (xdisplay, current_owner, CWEventMask, &attrs);
          if (meta_error_trap_pop_with_return (display) != Success)
            current_owner = None; /* don't wait for it to die later on */
        }
      else
        {
          meta_warning (_("Display “%s” already has a window manager; try using the --replace option to replace the current window manager."),
                        display->name);
          return None;
        }
    }

  /* We need SelectionClear and SelectionRequest events on the new owner,
   * but those cannot be masked, so we only need NoEventMask.
   */
  new_owner = meta_create_offscreen_window (xdisplay, xroot, NoEventMask);

  XSetSelectionOwner (xdisplay, manager_atom, new_owner, timestamp);

  if (XGetSelectionOwner (xdisplay, manager_atom) != new_owner)
    {
      meta_warning ("Could not acquire selection: %s", XGetAtomName (xdisplay, manager_atom));
      return None;
    }

  {
    /* Send client message indicating that we are now the selection owner */
    XClientMessageEvent ev;

    ev.type = ClientMessage;
    ev.window = xroot;
    ev.message_type = display->atom_MANAGER;
    ev.format = 32;
    ev.data.l[0] = timestamp;
    ev.data.l[1] = manager_atom;

    XSendEvent (xdisplay, xroot, False, StructureNotifyMask, (XEvent *) &ev);
  }

  /* Wait for old window manager to go away */
  if (current_owner != None)
    {
      XEvent event;

      /* We sort of block infinitely here which is probably lame. */

      meta_verbose ("Waiting for old window manager to exit\n");
      do
        XWindowEvent (xdisplay, current_owner, StructureNotifyMask, &event);
      while (event.type != DestroyNotify);
    }

  return new_owner;
}

MetaScreen*
meta_screen_new (MetaDisplay *display,
                 guint32      timestamp)
{
  MetaScreen *screen;
  int number;
  Screen *xscreen;
  Window xroot;
  Display *xdisplay;
  Window new_wm_sn_owner;
  gboolean replace_current_wm;
  Atom wm_sn_atom;
  char buf[128];
  MetaMonitorManager *manager;

  replace_current_wm = meta_get_replace_current_wm ();

  number = meta_ui_get_screen_number ();

  /* Only display->name, display->xdisplay, and display->error_traps
   * can really be used in this function, since normally screens are
   * created from the MetaDisplay constructor
   */

  xdisplay = display->xdisplay;

  meta_verbose ("Trying screen %d on display '%s'\n",
                number, display->name);

  xroot = RootWindow (xdisplay, number);

  /* FVWM checks for None here, I don't know if this
   * ever actually happens
   */
  if (xroot == None)
    {
      meta_warning (_("Screen %d on display “%s” is invalid\n"),
                    number, display->name);
      return NULL;
    }

  sprintf (buf, "WM_S%d", number);

  wm_sn_atom = XInternAtom (xdisplay, buf, False);
  new_wm_sn_owner = take_manager_selection (display, xroot, wm_sn_atom, timestamp, replace_current_wm);
  if (new_wm_sn_owner == None)
    return NULL;

  {
    long event_mask;
    unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_Enter);
    XISetMask (mask.mask, XI_Leave);
    XISetMask (mask.mask, XI_FocusIn);
    XISetMask (mask.mask, XI_FocusOut);
#ifdef HAVE_XI23
    if (META_DISPLAY_HAS_XINPUT_23 (display))
      {
        XISetMask (mask.mask, XI_BarrierHit);
        XISetMask (mask.mask, XI_BarrierLeave);
      }
#endif /* HAVE_XI23 */
    XISelectEvents (xdisplay, xroot, &mask, 1);

    event_mask = (SubstructureRedirectMask | SubstructureNotifyMask |
                  StructureNotifyMask | ColormapChangeMask | PropertyChangeMask);
    XSelectInput (xdisplay, xroot, event_mask);
  }

  /* Select for cursor changes so the cursor tracker is up to date. */
  XFixesSelectCursorInput (xdisplay, xroot, XFixesDisplayCursorNotifyMask);

  screen = g_object_new (META_TYPE_SCREEN, NULL);
  screen->closing = 0;

  screen->display = display;
  screen->screen_name = get_screen_name (display, number);
  screen->xroot = xroot;
  screen->rect.x = screen->rect.y = 0;

  manager = meta_monitor_manager_get ();
  g_signal_connect (manager, "monitors-changed-internal",
                    G_CALLBACK (on_monitors_changed_internal), screen);
  g_signal_connect (manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), screen);

  meta_monitor_manager_get_screen_size (manager,
                                        &screen->rect.width,
                                        &screen->rect.height);
  xscreen = ScreenOfDisplay (xdisplay, number);
  screen->current_cursor = -1; /* invalid/unset */
  screen->default_xvisual = DefaultVisualOfScreen (xscreen);
  screen->default_depth = DefaultDepthOfScreen (xscreen);

  screen->wm_sn_selection_window = new_wm_sn_owner;
  screen->wm_sn_atom = wm_sn_atom;
  screen->wm_sn_timestamp = timestamp;
  screen->work_area_later = 0;
  screen->check_fullscreen_later = 0;

  screen->active_workspace = NULL;
  screen->workspaces = NULL;
  screen->rows_of_workspaces = 1;
  screen->columns_of_workspaces = -1;
  screen->vertical_workspaces = FALSE;
  screen->starting_corner = META_SCREEN_TOPLEFT;
  screen->guard_window = None;

  /* If we're a Wayland compositor, then we don't grab the COW, since it
   * will map it. */
  if (!meta_is_wayland_compositor ())
    screen->composite_overlay_window = XCompositeGetOverlayWindow (xdisplay, xroot);

  /* Now that we've gotten taken a reference count on the COW, we
   * can close the helper that is holding on to it */
  meta_restart_finish ();

  reload_logical_monitors (screen);

  meta_screen_set_cursor (screen, META_CURSOR_DEFAULT);

  /* Handle creating a no_focus_window for this screen */
  screen->no_focus_window =
    meta_create_offscreen_window (display->xdisplay,
                                  screen->xroot,
                                  FocusChangeMask|KeyPressMask|KeyReleaseMask);
  XMapWindow (display->xdisplay, screen->no_focus_window);
  /* Done with no_focus_window stuff */

  set_wm_icon_size_hint (screen);

  set_supported_hint (screen);

  set_wm_check_hint (screen);

  set_desktop_viewport_hint (screen);

  set_desktop_geometry_hint (screen);

  meta_screen_update_workspace_layout (screen);

  /* Screens must have at least one workspace at all times,
   * so create that required workspace.
   */
  meta_workspace_new (screen);

  screen->keys_grabbed = FALSE;
  meta_screen_grab_keys (screen);

  screen->ui = meta_ui_new (screen->display->xdisplay);

  screen->tile_preview_timeout_id = 0;

  screen->stack = meta_stack_new (screen);
  screen->stack_tracker = meta_stack_tracker_new (screen);

  meta_prefs_add_listener (prefs_changed_callback, screen);

  meta_verbose ("Added screen %d ('%s') root 0x%lx\n",
                number, screen->screen_name, screen->xroot);

  return screen;
}

void
meta_screen_init_workspaces (MetaScreen *screen)
{
  MetaWorkspace *current_workspace;
  uint32_t current_workspace_index = 0;
  guint32 timestamp;

  g_return_if_fail (META_IS_SCREEN (screen));

  timestamp = screen->wm_sn_timestamp;

  /* Get current workspace */
  if (meta_prop_get_cardinal (screen->display,
                              screen->xroot,
                              screen->display->atom__NET_CURRENT_DESKTOP,
                              &current_workspace_index))
    meta_verbose ("Read existing _NET_CURRENT_DESKTOP = %d\n",
                  (int) current_workspace_index);
  else
    meta_verbose ("No _NET_CURRENT_DESKTOP present\n");

  update_num_workspaces (screen, timestamp);

  set_workspace_names (screen);

  /* Switch to the _NET_CURRENT_DESKTOP workspace */
  current_workspace = meta_screen_get_workspace_by_index (screen,
                                                          current_workspace_index);

  if (current_workspace != NULL)
    meta_workspace_activate (current_workspace, timestamp);
  else
    meta_workspace_activate (screen->workspaces->data, timestamp);
}

void
meta_screen_free (MetaScreen *screen,
                  guint32     timestamp)
{
  MetaDisplay *display;

  display = screen->display;

  screen->closing += 1;

  meta_compositor_unmanage (screen->display->compositor);

  meta_display_unmanage_windows_for_screen (display, screen, timestamp);

  meta_prefs_remove_listener (prefs_changed_callback, screen);

  meta_screen_ungrab_keys (screen);

  meta_ui_free (screen->ui);

  meta_stack_free (screen->stack);
  meta_stack_tracker_free (screen->stack_tracker);

  meta_error_trap_push (screen->display);
  XSelectInput (screen->display->xdisplay, screen->xroot, 0);
  if (meta_error_trap_pop_with_return (screen->display) != Success)
    meta_warning ("Could not release screen %d on display \"%s\"\n",
                  meta_ui_get_screen_number (), screen->display->name);

  unset_wm_check_hint (screen);

  XDestroyWindow (screen->display->xdisplay,
                  screen->wm_sn_selection_window);

  if (screen->work_area_later != 0)
    meta_later_remove (screen->work_area_later);
  if (screen->check_fullscreen_later != 0)
    meta_later_remove (screen->check_fullscreen_later);

  if (screen->tile_preview_timeout_id)
    g_source_remove (screen->tile_preview_timeout_id);

  g_free (screen->screen_name);

  g_object_unref (screen);
}

void
meta_screen_create_guard_window (MetaScreen *screen)
{
  if (screen->guard_window == None)
    screen->guard_window = create_guard_window (screen->display->xdisplay, screen);
}

void
meta_screen_manage_all_windows (MetaScreen *screen)
{
  guint64 *_children;
  guint64 *children;
  int n_children, i;

  meta_stack_freeze (screen->stack);
  meta_stack_tracker_get_stack (screen->stack_tracker, &_children, &n_children);

  /* Copy the stack as it will be modified as part of the loop */
  children = g_memdup (_children, sizeof (guint64) * n_children);

  for (i = 0; i < n_children; ++i)
    {
      g_assert (META_STACK_ID_IS_X11 (children[i]));
      meta_window_x11_new (screen->display, children[i], TRUE,
                           META_COMP_EFFECT_NONE);
    }

  g_free (children);
  meta_stack_thaw (screen->stack);
}

static void
prefs_changed_callback (MetaPreference pref,
                        gpointer       data)
{
  MetaScreen *screen = data;

  if ((pref == META_PREF_NUM_WORKSPACES ||
       pref == META_PREF_DYNAMIC_WORKSPACES) &&
      !meta_prefs_get_dynamic_workspaces ())
    {
      /* GSettings doesn't provide timestamps, but luckily update_num_workspaces
       * often doesn't need it...
       */
      guint32 timestamp =
        meta_display_get_current_time_roundtrip (screen->display);
      update_num_workspaces (screen, timestamp);
    }
  else if (pref == META_PREF_WORKSPACE_NAMES)
    {
      set_workspace_names (screen);
    }
}


static char*
get_screen_name (MetaDisplay *display,
                 int          number)
{
  char *p;
  char *dname;
  char *scr;

  /* DisplayString gives us a sort of canonical display,
   * vs. the user-entered name from XDisplayName()
   */
  dname = g_strdup (DisplayString (display->xdisplay));

  /* Change display name to specify this screen.
   */
  p = strrchr (dname, ':');
  if (p)
    {
      p = strchr (p, '.');
      if (p)
        *p = '\0';
    }

  scr = g_strdup_printf ("%s.%d", dname, number);

  g_free (dname);

  return scr;
}

void
meta_screen_foreach_window (MetaScreen           *screen,
                            MetaListWindowsFlags  flags,
                            MetaScreenWindowFunc  func,
                            gpointer              data)
{
  GSList *windows;

  /* If we end up doing this often, just keeping a list
   * of windows might be sensible.
   */

  windows = meta_display_list_windows (screen->display, flags);

  g_slist_foreach (windows, (GFunc) func, data);

  g_slist_free (windows);
}

int
meta_screen_get_n_workspaces (MetaScreen *screen)
{
  return g_list_length (screen->workspaces);
}

/**
 * meta_screen_get_workspace_by_index:
 * @screen: a #MetaScreen
 * @index: index of one of the screen's workspaces
 *
 * Gets the workspace object for one of a screen's workspaces given the workspace
 * index. It's valid to call this function with an out-of-range index and it
 * will robustly return %NULL.
 *
 * Return value: (transfer none): the workspace object with specified index, or %NULL
 *   if the index is out of range.
 */
MetaWorkspace*
meta_screen_get_workspace_by_index (MetaScreen  *screen,
                                    int          idx)
{
  return g_list_nth_data (screen->workspaces, idx);
}

static void
set_number_of_spaces_hint (MetaScreen *screen,
			   int         n_spaces)
{
  unsigned long data[1];

  if (screen->closing > 0)
    return;

  data[0] = n_spaces;

  meta_verbose ("Setting _NET_NUMBER_OF_DESKTOPS to %lu\n", data[0]);

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_NUMBER_OF_DESKTOPS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

static void
set_desktop_geometry_hint (MetaScreen *screen)
{
  unsigned long data[2];

  if (screen->closing > 0)
    return;

  data[0] = screen->rect.width;
  data[1] = screen->rect.height;

  meta_verbose ("Setting _NET_DESKTOP_GEOMETRY to %lu, %lu\n", data[0], data[1]);

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_DESKTOP_GEOMETRY,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  meta_error_trap_pop (screen->display);
}

static void
set_desktop_viewport_hint (MetaScreen *screen)
{
  unsigned long data[2];

  if (screen->closing > 0)
    return;

  /*
   * Mutter does not implement viewports, so this is a fixed 0,0
   */
  data[0] = 0;
  data[1] = 0;

  meta_verbose ("Setting _NET_DESKTOP_VIEWPORT to 0, 0\n");

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_DESKTOP_VIEWPORT,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  meta_error_trap_pop (screen->display);
}

void
meta_screen_remove_workspace (MetaScreen *screen, MetaWorkspace *workspace,
                              guint32 timestamp)
{
  GList         *l;
  GList         *next;
  MetaWorkspace *neighbour = NULL;
  int            index;
  gboolean       active_index_changed;
  int            new_num;

  l = g_list_find (screen->workspaces, workspace);
  if (!l)
    return;

  next = l->next;

  if (l->prev)
    neighbour = l->prev->data;
  else if (l->next)
    neighbour = l->next->data;
  else
    {
      /* Cannot remove the only workspace! */
      return;
    }

  meta_workspace_relocate_windows (workspace, neighbour);

  if (workspace == screen->active_workspace)
    meta_workspace_activate (neighbour, timestamp);

  /* To emit the signal after removing the workspace */
  index = meta_workspace_index (workspace);
  active_index_changed = index < meta_screen_get_active_workspace_index (screen);

  /* This also removes the workspace from the screens list */
  meta_workspace_remove (workspace);

  new_num = g_list_length (screen->workspaces);

  set_number_of_spaces_hint (screen, new_num);

  if (!meta_prefs_get_dynamic_workspaces ())
    meta_prefs_set_num_workspaces (new_num);

  /* If deleting a workspace before the current workspace, the active
   * workspace index changes, so we need to update that hint */
  if (active_index_changed)
      meta_screen_set_active_workspace_hint (screen);

  for (l = next; l != NULL; l = l->next)
    {
      MetaWorkspace *w = l->data;
      meta_workspace_index_changed (w);
    }

  meta_screen_queue_workarea_recalc (screen);

  g_signal_emit (screen, screen_signals[WORKSPACE_REMOVED], 0, index);
  g_object_notify (G_OBJECT (screen), "n-workspaces");
}

/**
 * meta_screen_append_new_workspace:
 * @screen: a #MetaScreen
 * @activate: %TRUE if the workspace should be switched to after creation
 * @timestamp: if switching to a new workspace, timestamp to be used when
 *   focusing a window on the new workspace. (Doesn't hurt to pass a valid
 *   timestamp when available even if not switching workspaces.)
 *
 * Append a new workspace to the screen and (optionally) switch to that
 * screen.
 *
 * Return value: (transfer none): the newly appended workspace.
 */
MetaWorkspace *
meta_screen_append_new_workspace (MetaScreen *screen, gboolean activate,
                                  guint32 timestamp)
{
  MetaWorkspace *w;
  int new_num;

  /* This also adds the workspace to the screen list */
  w = meta_workspace_new (screen);

  if (!w)
    return NULL;

  if (activate)
    meta_workspace_activate (w, timestamp);

  new_num = g_list_length (screen->workspaces);

  set_number_of_spaces_hint (screen, new_num);

  if (!meta_prefs_get_dynamic_workspaces ())
    meta_prefs_set_num_workspaces (new_num);

  meta_screen_queue_workarea_recalc (screen);

  g_signal_emit (screen, screen_signals[WORKSPACE_ADDED],
                 0, meta_workspace_index (w));
  g_object_notify (G_OBJECT (screen), "n-workspaces");

  return w;
}


static void
update_num_workspaces (MetaScreen *screen,
                       guint32     timestamp)
{
  int new_num, old_num;
  GList *l;
  int i;
  GList *extras;
  MetaWorkspace *last_remaining;
  gboolean need_change_space;

  if (meta_prefs_get_dynamic_workspaces ())
    {
      int n_items;
      uint32_t *list;

      n_items = 0;
      list = NULL;

      if (meta_prop_get_cardinal_list (screen->display, screen->xroot,
                                       screen->display->atom__NET_NUMBER_OF_DESKTOPS,
                                       &list, &n_items))
        {
          new_num = list[0];
          meta_XFree (list);
        }
      else
        {
          new_num = 1;
        }
    }
  else
    {
      new_num = meta_prefs_get_num_workspaces ();
    }

  g_assert (new_num > 0);

  if (g_list_length (screen->workspaces) == (guint) new_num)
    {
      if (screen->display->display_opening)
        set_number_of_spaces_hint (screen, new_num);
      return;
    }

  last_remaining = NULL;
  extras = NULL;
  i = 0;
  for (l = screen->workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *w = l->data;

      if (i >= new_num)
        extras = g_list_prepend (extras, w);
      else
        last_remaining = w;

      ++i;
    }
  old_num = i;

  g_assert (last_remaining);

  /* Get rid of the extra workspaces by moving all their windows
   * to last_remaining, then activating last_remaining if
   * one of the removed workspaces was active. This will be a bit
   * wacky if the config tool for changing number of workspaces
   * is on a removed workspace ;-)
   */
  need_change_space = FALSE;
  for (l = extras; l != NULL; l = l->next)
    {
      MetaWorkspace *w = l->data;

      meta_workspace_relocate_windows (w, last_remaining);

      if (w == screen->active_workspace)
        need_change_space = TRUE;
    }

  if (need_change_space)
    meta_workspace_activate (last_remaining, timestamp);

  /* Should now be safe to free the workspaces */
  for (l = extras; l != NULL; l = l->next)
    {
      MetaWorkspace *w = l->data;

      meta_workspace_remove (w);
    }

  g_list_free (extras);

  for (i = old_num; i < new_num; i++)
    meta_workspace_new (screen);

  set_number_of_spaces_hint (screen, new_num);

  meta_screen_queue_workarea_recalc (screen);

  for (i = old_num; i < new_num; i++)
    g_signal_emit (screen, screen_signals[WORKSPACE_ADDED], 0, i);

  g_object_notify (G_OBJECT (screen), "n-workspaces");
}

static int
find_highest_logical_monitor_scale (MetaBackend      *backend,
                                    MetaCursorSprite *cursor_sprite)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterRect cursor_rect;
  GList *logical_monitors;
  GList *l;
  int highest_scale = 0.0;

  cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
						     cursor_sprite);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      ClutterRect logical_monitor_rect =
	      meta_rectangle_to_clutter_rect (&logical_monitor->rect);

      if (!clutter_rect_intersection (&cursor_rect,
				      &logical_monitor_rect,
				      NULL))
        continue;

      highest_scale = MAX (highest_scale, logical_monitor->scale);
    }

  return highest_scale;
}

static void
root_cursor_prepare_at (MetaCursorSpriteXcursor *sprite_xcursor,
                        int                      x,
                        int                      y,
                        MetaScreen              *screen)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorSprite *cursor_sprite = META_CURSOR_SPRITE (sprite_xcursor);

  if (meta_is_stage_views_scaled ())
    {
      int scale;

      scale = find_highest_logical_monitor_scale (backend, cursor_sprite);
      if (scale != 0.0)
        {
          meta_cursor_sprite_xcursor_set_theme_scale (sprite_xcursor, scale);
          meta_cursor_sprite_set_texture_scale (cursor_sprite, 1.0 / scale);
        }
    }
  else
    {
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      MetaLogicalMonitor *logical_monitor;

      logical_monitor =
        meta_monitor_manager_get_logical_monitor_at (monitor_manager, x, y);

      /* Reload the cursor texture if the scale has changed. */
      if (logical_monitor)
        {
          meta_cursor_sprite_xcursor_set_theme_scale (sprite_xcursor,
                                                      logical_monitor->scale);
          meta_cursor_sprite_set_texture_scale (cursor_sprite, 1.0);
        }
    }
}

static void
manage_root_cursor_sprite_scale (MetaScreen              *screen,
                                 MetaCursorSpriteXcursor *sprite_xcursor)
{
  g_signal_connect_object (sprite_xcursor,
                           "prepare-at",
                           G_CALLBACK (root_cursor_prepare_at),
                           screen,
                           0);
}

void
meta_screen_update_cursor (MetaScreen *screen)
{
  MetaDisplay *display = screen->display;
  MetaCursor cursor = screen->current_cursor;
  Cursor xcursor;
  MetaCursorSpriteXcursor *sprite_xcursor;
  MetaBackend *backend = meta_get_backend ();
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  sprite_xcursor = meta_cursor_sprite_xcursor_new (cursor);

  if (meta_is_wayland_compositor ())
    manage_root_cursor_sprite_scale (screen, sprite_xcursor);

  meta_cursor_tracker_set_root_cursor (cursor_tracker,
                                       META_CURSOR_SPRITE (sprite_xcursor));
  g_object_unref (sprite_xcursor);

  /* Set a cursor for X11 applications that don't specify their own */
  xcursor = meta_display_create_x_cursor (display, cursor);

  XDefineCursor (display->xdisplay, screen->xroot, xcursor);
  XFlush (display->xdisplay);
  XFreeCursor (display->xdisplay, xcursor);
}

void
meta_screen_set_cursor (MetaScreen *screen,
                        MetaCursor  cursor)
{
  if (cursor == screen->current_cursor)
    return;

  screen->current_cursor = cursor;
  meta_screen_update_cursor (screen);
}

static gboolean
meta_screen_update_tile_preview_timeout (gpointer data)
{
  MetaScreen *screen = data;
  MetaWindow *window = screen->display->grab_window;
  gboolean needs_preview = FALSE;

  screen->tile_preview_timeout_id = 0;

  if (window)
    {
      switch (screen->preview_tile_mode)
        {
          case META_TILE_LEFT:
          case META_TILE_RIGHT:
              if (!META_WINDOW_TILED_SIDE_BY_SIDE (window))
                needs_preview = TRUE;
              break;

          case META_TILE_MAXIMIZED:
              if (!META_WINDOW_MAXIMIZED (window))
                needs_preview = TRUE;
              break;

          default:
              needs_preview = FALSE;
              break;
        }
    }

  if (needs_preview)
    {
      MetaRectangle tile_rect;
      int monitor;

      monitor = meta_window_get_current_tile_monitor_number (window);
      meta_window_get_tile_area (window, screen->preview_tile_mode, &tile_rect);
      meta_compositor_show_tile_preview (screen->display->compositor,
                                         window, &tile_rect, monitor);
    }
  else
    meta_compositor_hide_tile_preview (screen->display->compositor);

  return FALSE;
}

#define TILE_PREVIEW_TIMEOUT_MS 200

void
meta_screen_update_tile_preview (MetaScreen *screen,
                                 gboolean    delay)
{
  if (delay)
    {
      if (screen->tile_preview_timeout_id > 0)
        return;

      screen->tile_preview_timeout_id =
        g_timeout_add (TILE_PREVIEW_TIMEOUT_MS,
                       meta_screen_update_tile_preview_timeout,
                       screen);
      g_source_set_name_by_id (screen->tile_preview_timeout_id,
                               "[mutter] meta_screen_update_tile_preview_timeout");
    }
  else
    {
      if (screen->tile_preview_timeout_id > 0)
        g_source_remove (screen->tile_preview_timeout_id);

      meta_screen_update_tile_preview_timeout ((gpointer)screen);
    }
}

void
meta_screen_hide_tile_preview (MetaScreen *screen)
{
  if (screen->tile_preview_timeout_id > 0)
    g_source_remove (screen->tile_preview_timeout_id);
  screen->tile_preview_timeout_id = 0;

  screen->preview_tile_mode = META_TILE_NONE;
  meta_compositor_hide_tile_preview (screen->display->compositor);
}

MetaWindow*
meta_screen_get_mouse_window (MetaScreen  *screen,
                              MetaWindow  *not_this_one)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaWindow *window;
  int x, y;

  if (not_this_one)
    meta_topic (META_DEBUG_FOCUS,
                "Focusing mouse window excluding %s\n", not_this_one->desc);

  meta_cursor_tracker_get_pointer (cursor_tracker, &x, &y, NULL);

  window = meta_stack_get_default_focus_window_at_point (screen->stack,
                                                         screen->active_workspace,
                                                         not_this_one,
                                                         x, y);

  return window;
}

int
meta_screen_get_monitor_index_for_rect (MetaScreen    *screen,
                                        MetaRectangle *rect)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_rect (monitor_manager, rect);
  if (!logical_monitor)
    return -1;

  return logical_monitor->number;
}

int
meta_screen_get_monitor_neighbor_index (MetaScreen         *screen,
                                        int                 which_monitor,
                                        MetaScreenDirection direction)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MetaLogicalMonitor *neighbor;

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_number (monitor_manager,
                                                          which_monitor);
  neighbor = meta_monitor_manager_get_logical_monitor_neighbor (monitor_manager,
                                                                logical_monitor,
                                                                direction);
  return neighbor ? neighbor->number : -1;
}

/**
 * meta_screen_get_current_monitor:
 * @screen: a #MetaScreen
 *
 * Gets the index of the monitor that currently has the mouse pointer.
 *
 * Return value: a monitor index
 */
int
meta_screen_get_current_monitor (MetaScreen *screen)
{
  MetaBackend *backend = meta_get_backend ();
  MetaLogicalMonitor *logical_monitor;

  logical_monitor = meta_backend_get_current_logical_monitor (backend);

  /* Pretend its the first when there is no actual current monitor. */
  if (!logical_monitor)
    return 0;

  return logical_monitor->number;
}

/**
 * meta_screen_get_n_monitors:
 * @screen: a #MetaScreen
 *
 * Gets the number of monitors that are joined together to form @screen.
 *
 * Return value: the number of monitors
 */
int
meta_screen_get_n_monitors (MetaScreen *screen)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_return_val_if_fail (META_IS_SCREEN (screen), 0);

  return meta_monitor_manager_get_num_logical_monitors (monitor_manager);
}

/**
 * meta_screen_get_primary_monitor:
 * @screen: a #MetaScreen
 *
 * Gets the index of the primary monitor on this @screen.
 *
 * Return value: a monitor index
 */
int
meta_screen_get_primary_monitor (MetaScreen *screen)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;

  g_return_val_if_fail (META_IS_SCREEN (screen), 0);

  logical_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
  if (logical_monitor)
    return logical_monitor->number;
  else
    return -1;
}

/**
 * meta_screen_get_monitor_geometry:
 * @screen: a #MetaScreen
 * @monitor: the monitor number
 * @geometry: (out): location to store the monitor geometry
 *
 * Stores the location and size of the indicated monitor in @geometry.
 */
void
meta_screen_get_monitor_geometry (MetaScreen    *screen,
                                  int            monitor,
                                  MetaRectangle *geometry)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
#ifndef G_DISABLE_CHECKS
  int n_logical_monitors =
    meta_monitor_manager_get_num_logical_monitors (monitor_manager);
#endif

  g_return_if_fail (META_IS_SCREEN (screen));
  g_return_if_fail (monitor >= 0 && monitor < n_logical_monitors);
  g_return_if_fail (geometry != NULL);

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_number (monitor_manager,
                                                          monitor);
  *geometry = logical_monitor->rect;
}

#define _NET_WM_ORIENTATION_HORZ 0
#define _NET_WM_ORIENTATION_VERT 1

#define _NET_WM_TOPLEFT     0
#define _NET_WM_TOPRIGHT    1
#define _NET_WM_BOTTOMRIGHT 2
#define _NET_WM_BOTTOMLEFT  3

void
meta_screen_update_workspace_layout (MetaScreen *screen)
{
  uint32_t *list;
  int n_items;

  if (screen->workspace_layout_overridden)
    return;

  list = NULL;
  n_items = 0;

  if (meta_prop_get_cardinal_list (screen->display,
                                   screen->xroot,
                                   screen->display->atom__NET_DESKTOP_LAYOUT,
                                   &list, &n_items))
    {
      if (n_items == 3 || n_items == 4)
        {
          int cols, rows;

          switch (list[0])
            {
            case _NET_WM_ORIENTATION_HORZ:
              screen->vertical_workspaces = FALSE;
              break;
            case _NET_WM_ORIENTATION_VERT:
              screen->vertical_workspaces = TRUE;
              break;
            default:
              meta_warning ("Someone set a weird orientation in _NET_DESKTOP_LAYOUT\n");
              break;
            }

          cols = list[1];
          rows = list[2];

          if (rows <= 0 && cols <= 0)
            {
              meta_warning ("Columns = %d rows = %d in _NET_DESKTOP_LAYOUT makes no sense\n", rows, cols);
            }
          else
            {
              if (rows > 0)
                screen->rows_of_workspaces = rows;
              else
                screen->rows_of_workspaces = -1;

              if (cols > 0)
                screen->columns_of_workspaces = cols;
              else
                screen->columns_of_workspaces = -1;
            }

          if (n_items == 4)
            {
              switch (list[3])
                {
                  case _NET_WM_TOPLEFT:
                    screen->starting_corner = META_SCREEN_TOPLEFT;
                    break;
                  case _NET_WM_TOPRIGHT:
                    screen->starting_corner = META_SCREEN_TOPRIGHT;
                    break;
                  case _NET_WM_BOTTOMRIGHT:
                    screen->starting_corner = META_SCREEN_BOTTOMRIGHT;
                    break;
                  case _NET_WM_BOTTOMLEFT:
                    screen->starting_corner = META_SCREEN_BOTTOMLEFT;
                    break;
                  default:
                    meta_warning ("Someone set a weird starting corner in _NET_DESKTOP_LAYOUT\n");
                    break;
                }
            }
          else
            screen->starting_corner = META_SCREEN_TOPLEFT;
        }
      else
        {
          meta_warning ("Someone set _NET_DESKTOP_LAYOUT to %d integers instead of 4 "
                        "(3 is accepted for backwards compat)\n", n_items);
        }

      meta_XFree (list);
    }

  meta_verbose ("Workspace layout rows = %d cols = %d orientation = %d starting corner = %u\n",
                screen->rows_of_workspaces,
                screen->columns_of_workspaces,
                screen->vertical_workspaces,
                screen->starting_corner);
}

/**
 * meta_screen_override_workspace_layout:
 * @screen: a #MetaScreen
 * @starting_corner: the corner at which the first workspace is found
 * @vertical_layout: if %TRUE the workspaces are laid out in columns rather than rows
 * @n_rows: number of rows of workspaces, or -1 to determine the number of rows from
 *   @n_columns and the total number of workspaces
 * @n_columns: number of columns of workspaces, or -1 to determine the number of columns from
 *   @n_rows and the total number of workspaces
 *
 * Explicitly set the layout of workspaces. Once this has been called, the contents of the
 * _NET_DESKTOP_LAYOUT property on the root window are completely ignored.
 */
void
meta_screen_override_workspace_layout (MetaScreen      *screen,
                                       MetaScreenCorner starting_corner,
                                       gboolean         vertical_layout,
                                       int              n_rows,
                                       int              n_columns)
{
  g_return_if_fail (META_IS_SCREEN (screen));
  g_return_if_fail (n_rows > 0 || n_columns > 0);
  g_return_if_fail (n_rows != 0 && n_columns != 0);

  screen->workspace_layout_overridden = TRUE;
  screen->vertical_workspaces = vertical_layout != FALSE;
  screen->starting_corner = starting_corner;
  screen->rows_of_workspaces = n_rows;
  screen->columns_of_workspaces = n_columns;

  /* In theory we should remove _NET_DESKTOP_LAYOUT from _NET_SUPPORTED at this
   * point, but it's unlikely that anybody checks that, and it's unlikely that
   * anybody who checks that handles changes, so we'd probably just create
   * a race condition. And it's hard to implement with the code in set_supported_hint()
   */
}

static void
set_workspace_names (MetaScreen *screen)
{
  /* This updates names on root window when the pref changes,
   * note we only get prefs change notify if things have
   * really changed.
   */
  GString *flattened;
  int i;
  int n_spaces;

  /* flatten to nul-separated list */
  n_spaces = meta_screen_get_n_workspaces (screen);
  flattened = g_string_new ("");
  i = 0;
  while (i < n_spaces)
    {
      const char *name;

      name = meta_prefs_get_workspace_name (i);

      if (name)
        g_string_append_len (flattened, name,
                             strlen (name) + 1);
      else
        g_string_append_len (flattened, "", 1);

      ++i;
    }

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay,
                   screen->xroot,
                   screen->display->atom__NET_DESKTOP_NAMES,
		   screen->display->atom_UTF8_STRING,
                   8, PropModeReplace,
		   (unsigned char *)flattened->str, flattened->len);
  meta_error_trap_pop (screen->display);

  g_string_free (flattened, TRUE);
}

void
meta_screen_update_workspace_names (MetaScreen *screen)
{
  char **names;
  int n_names;
  int i;

  /* this updates names in prefs when the root window property changes,
   * iff the new property contents don't match what's already in prefs
   */

  names = NULL;
  n_names = 0;
  if (!meta_prop_get_utf8_list (screen->display,
                                screen->xroot,
                                screen->display->atom__NET_DESKTOP_NAMES,
                                &names, &n_names))
    {
      meta_verbose ("Failed to get workspace names from root window\n");
      return;
    }

  i = 0;
  while (i < n_names)
    {
      meta_topic (META_DEBUG_PREFS,
                  "Setting workspace %d name to \"%s\" due to _NET_DESKTOP_NAMES change\n",
                  i, names[i] ? names[i] : "null");
      meta_prefs_change_workspace_name (i, names[i]);

      ++i;
    }

  g_strfreev (names);
}

Window
meta_create_offscreen_window (Display *xdisplay,
                              Window   parent,
                              long     valuemask)
{
  XSetWindowAttributes attrs;

  /* we want to be override redirect because sometimes we
   * create a window on a screen we aren't managing.
   * (but on a display we are managing at least one screen for)
   */
  attrs.override_redirect = True;
  attrs.event_mask = valuemask;

  return XCreateWindow (xdisplay,
                        parent,
                        -100, -100, 1, 1,
                        0,
                        CopyFromParent,
                        CopyFromParent,
                        (Visual *)CopyFromParent,
                        CWOverrideRedirect | CWEventMask,
                        &attrs);
}

static void
set_work_area_hint (MetaScreen *screen)
{
  int num_workspaces;
  GList *l;
  unsigned long *data, *tmp;
  MetaRectangle area;

  num_workspaces = meta_screen_get_n_workspaces (screen);
  data = g_new (unsigned long, num_workspaces * 4);
  tmp = data;

  for (l = screen->workspaces; l != NULL; l = l->next)
    {
      MetaWorkspace *workspace = l->data;

      meta_workspace_get_work_area_all_monitors (workspace, &area);
      tmp[0] = area.x;
      tmp[1] = area.y;
      tmp[2] = area.width;
      tmp[3] = area.height;

      tmp += 4;
    }

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
		   screen->display->atom__NET_WORKAREA,
		   XA_CARDINAL, 32, PropModeReplace,
		   (guchar*) data, num_workspaces*4);
  g_free (data);
  meta_error_trap_pop (screen->display);

  g_signal_emit (screen, screen_signals[WORKAREAS_CHANGED], 0);
}

static gboolean
set_work_area_later_func (MetaScreen *screen)
{
  meta_topic (META_DEBUG_WORKAREA,
              "Running work area hint computation function\n");

  screen->work_area_later = 0;

  set_work_area_hint (screen);

  return FALSE;
}

void
meta_screen_queue_workarea_recalc (MetaScreen *screen)
{
  /* Recompute work area later before redrawing */
  if (screen->work_area_later == 0)
    {
      meta_topic (META_DEBUG_WORKAREA,
                  "Adding work area hint computation function\n");
      screen->work_area_later =
        meta_later_add (META_LATER_BEFORE_REDRAW,
                        (GSourceFunc) set_work_area_later_func,
                        screen,
                        NULL);
    }
}


#ifdef WITH_VERBOSE_MODE
static const char *
meta_screen_corner_to_string (MetaScreenCorner corner)
{
  switch (corner)
    {
    case META_SCREEN_TOPLEFT:
      return "TopLeft";
    case META_SCREEN_TOPRIGHT:
      return "TopRight";
    case META_SCREEN_BOTTOMLEFT:
      return "BottomLeft";
    case META_SCREEN_BOTTOMRIGHT:
      return "BottomRight";
    }

  return "Unknown";
}
#endif /* WITH_VERBOSE_MODE */

void
meta_screen_calc_workspace_layout (MetaScreen          *screen,
                                   int                  num_workspaces,
                                   int                  current_space,
                                   MetaWorkspaceLayout *layout)
{
  int rows, cols;
  int grid_area;
  int *grid;
  int i, r, c;
  int current_row, current_col;

  rows = screen->rows_of_workspaces;
  cols = screen->columns_of_workspaces;
  if (rows <= 0 && cols <= 0)
    cols = num_workspaces;

  if (rows <= 0)
    rows = num_workspaces / cols + ((num_workspaces % cols) > 0 ? 1 : 0);
  if (cols <= 0)
    cols = num_workspaces / rows + ((num_workspaces % rows) > 0 ? 1 : 0);

  /* paranoia */
  if (rows < 1)
    rows = 1;
  if (cols < 1)
    cols = 1;

  g_assert (rows != 0 && cols != 0);

  grid_area = rows * cols;

  meta_verbose ("Getting layout rows = %d cols = %d current = %d "
                "num_spaces = %d vertical = %s corner = %s\n",
                rows, cols, current_space, num_workspaces,
                screen->vertical_workspaces ? "(true)" : "(false)",
                meta_screen_corner_to_string (screen->starting_corner));

  /* ok, we want to setup the distances in the workspace array to go
   * in each direction. Remember, there are many ways that a workspace
   * array can be setup.
   * see http://www.freedesktop.org/standards/wm-spec/1.2/html/x109.html
   * and look at the _NET_DESKTOP_LAYOUT section for details.
   * For instance:
   */
  /* starting_corner = META_SCREEN_TOPLEFT
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       1234                                    1357
   *       5678                                    2468
   *
   * starting_corner = META_SCREEN_TOPRIGHT
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       4321                                    7531
   *       8765                                    8642
   *
   * starting_corner = META_SCREEN_BOTTOMLEFT
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       5678                                    2468
   *       1234                                    1357
   *
   * starting_corner = META_SCREEN_BOTTOMRIGHT
   *  vertical_workspaces = 0                 vertical_workspaces=1
   *       8765                                    8642
   *       4321                                    7531
   *
   */
  /* keep in mind that we could have a ragged layout, e.g. the "8"
   * in the above grids could be missing
   */


  grid = g_new (int, grid_area);

  current_row = -1;
  current_col = -1;
  i = 0;

  switch (screen->starting_corner)
    {
    case META_SCREEN_TOPLEFT:
      if (screen->vertical_workspaces)
        {
          c = 0;
          while (c < cols)
            {
              r = 0;
              while (r < rows)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++r;
                }
              ++c;
            }
        }
      else
        {
          r = 0;
          while (r < rows)
            {
              c = 0;
              while (c < cols)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++c;
                }
              ++r;
            }
        }
      break;
    case META_SCREEN_TOPRIGHT:
      if (screen->vertical_workspaces)
        {
          c = cols - 1;
          while (c >= 0)
            {
              r = 0;
              while (r < rows)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++r;
                }
              --c;
            }
        }
      else
        {
          r = 0;
          while (r < rows)
            {
              c = cols - 1;
              while (c >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --c;
                }
              ++r;
            }
        }
      break;
    case META_SCREEN_BOTTOMLEFT:
      if (screen->vertical_workspaces)
        {
          c = 0;
          while (c < cols)
            {
              r = rows - 1;
              while (r >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --r;
                }
              ++c;
            }
        }
      else
        {
          r = rows - 1;
          while (r >= 0)
            {
              c = 0;
              while (c < cols)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  ++c;
                }
              --r;
            }
        }
      break;
    case META_SCREEN_BOTTOMRIGHT:
      if (screen->vertical_workspaces)
        {
          c = cols - 1;
          while (c >= 0)
            {
              r = rows - 1;
              while (r >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --r;
                }
              --c;
            }
        }
      else
        {
          r = rows - 1;
          while (r >= 0)
            {
              c = cols - 1;
              while (c >= 0)
                {
                  grid[r*cols+c] = i;
                  ++i;
                  --c;
                }
              --r;
            }
        }
      break;
    }

  if (i != grid_area)
    meta_bug ("did not fill in the whole workspace grid in %s (%d filled)\n",
              G_STRFUNC, i);

  current_row = 0;
  current_col = 0;
  r = 0;
  while (r < rows)
    {
      c = 0;
      while (c < cols)
        {
          if (grid[r*cols+c] == current_space)
            {
              current_row = r;
              current_col = c;
            }
          else if (grid[r*cols+c] >= num_workspaces)
            {
              /* flag nonexistent spaces with -1 */
              grid[r*cols+c] = -1;
            }
          ++c;
        }
      ++r;
    }

  layout->rows = rows;
  layout->cols = cols;
  layout->grid = grid;
  layout->grid_area = grid_area;
  layout->current_row = current_row;
  layout->current_col = current_col;

#ifdef WITH_VERBOSE_MODE
  if (meta_is_verbose ())
    {
      r = 0;
      while (r < layout->rows)
        {
          meta_verbose (" ");
          meta_push_no_msg_prefix ();
          c = 0;
          while (c < layout->cols)
            {
              if (r == layout->current_row &&
                  c == layout->current_col)
                meta_verbose ("*%2d ", layout->grid[r*layout->cols+c]);
              else
                meta_verbose ("%3d ", layout->grid[r*layout->cols+c]);
              ++c;
            }
          meta_verbose ("\n");
          meta_pop_no_msg_prefix ();
          ++r;
        }
    }
#endif /* WITH_VERBOSE_MODE */
}

void
meta_screen_free_workspace_layout (MetaWorkspaceLayout *layout)
{
  g_free (layout->grid);
}

static void
meta_screen_resize_func (MetaWindow *window,
                         gpointer    user_data)
{
  if (window->struts)
    {
      meta_window_update_struts (window);
    }
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);

  meta_window_recalc_features (window);
}

static void
on_monitors_changed_internal (MetaMonitorManager *manager,
                              MetaScreen         *screen)
{
  meta_monitor_manager_get_screen_size (manager,
                                        &screen->rect.width,
                                        &screen->rect.height);

  reload_logical_monitors (screen);
  set_desktop_geometry_hint (screen);

  /* Resize the guard window to fill the screen again. */
  if (screen->guard_window != None)
    {
      XWindowChanges changes;

      changes.x = 0;
      changes.y = 0;
      changes.width = screen->rect.width;
      changes.height = screen->rect.height;

      XConfigureWindow(screen->display->xdisplay,
                       screen->guard_window,
                       CWX | CWY | CWWidth | CWHeight,
                       &changes);
    }

  /* Fix up monitor for all windows on this screen */
  meta_screen_foreach_window (screen, META_LIST_INCLUDE_OVERRIDE_REDIRECT, (MetaScreenWindowFunc) meta_window_update_for_monitors_changed, 0);

  /* Queue a resize on all the windows */
  meta_screen_foreach_window (screen, META_LIST_DEFAULT, meta_screen_resize_func, 0);

  meta_screen_queue_check_fullscreen (screen);
}

static void
on_monitors_changed (MetaMonitorManager *manager,
                     MetaScreen         *screen)
{
  /* Inform the external world about what has happened */
  g_signal_emit (screen, screen_signals[MONITORS_CHANGED], 0);
}

void
meta_screen_update_showing_desktop_hint (MetaScreen *screen)
{
  unsigned long data[1];

  data[0] = screen->active_workspace->showing_desktop ? 1 : 0;

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_SHOWING_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

static void
queue_windows_showing (MetaScreen *screen)
{
  GSList *windows, *l;

  /* Must operate on all windows on display instead of just on the
   * active_workspace's window list, because the active_workspace's
   * window list may not contain the on_all_workspace windows.
   */
  windows = meta_display_list_windows (screen->display, META_LIST_DEFAULT);

  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *w = l->data;
      meta_window_queue (w, META_QUEUE_CALC_SHOWING);
    }

  g_slist_free (windows);
}

void
meta_screen_minimize_all_on_active_workspace_except (MetaScreen *screen,
                                                     MetaWindow *keep)
{
  GList *l;

  for (l = screen->active_workspace->windows; l != NULL; l = l->next)
    {
      MetaWindow *w = l->data;

      if (w->has_minimize_func && w != keep)
	meta_window_minimize (w);
    }
}

void
meta_screen_show_desktop (MetaScreen *screen,
                          guint32     timestamp)
{
  GList *l;

  if (screen->active_workspace->showing_desktop)
    return;

  screen->active_workspace->showing_desktop = TRUE;

  queue_windows_showing (screen);

  /* Focus the most recently used META_WINDOW_DESKTOP window, if there is one;
   * see bug 159257.
   */
  for (l = screen->active_workspace->mru_list; l != NULL; l = l->next)
    {
      MetaWindow *w = l->data;

      if (w->type == META_WINDOW_DESKTOP)
        {
          meta_window_focus (w, timestamp);
          break;
        }
    }

  meta_screen_update_showing_desktop_hint (screen);
}

void
meta_screen_unshow_desktop (MetaScreen *screen)
{
  if (!screen->active_workspace->showing_desktop)
    return;

  screen->active_workspace->showing_desktop = FALSE;

  queue_windows_showing (screen);

  meta_screen_update_showing_desktop_hint (screen);
}

/**
 * meta_screen_get_startup_sequences: (skip)
 * @screen:
 *
 * Return value: (transfer none): Currently active #SnStartupSequence items
 */
GSList *
meta_screen_get_startup_sequences (MetaScreen *screen)
{
  return screen->startup_sequences;
}

/* Sets the initial_timestamp and initial_workspace properties
 * of a window according to information given us by the
 * startup-notification library.
 *
 * Returns TRUE if startup properties have been applied, and
 * FALSE if they have not (for example, if they had already
 * been applied.)
 */
gboolean
meta_screen_apply_startup_properties (MetaScreen *screen,
                                      MetaWindow *window)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  const char *startup_id;
  GSList *l;
  SnStartupSequence *sequence;

  /* Does the window have a startup ID stored? */
  startup_id = meta_window_get_startup_id (window);

  meta_topic (META_DEBUG_STARTUP,
              "Applying startup props to %s id \"%s\"\n",
              window->desc,
              startup_id ? startup_id : "(none)");

  sequence = NULL;
  if (startup_id == NULL)
    {
      /* No startup ID stored for the window. Let's ask the
       * startup-notification library whether there's anything
       * stored for the resource name or resource class hints.
       */
      for (l = screen->startup_sequences; l != NULL; l = l->next)
        {
          const char *wmclass;
          SnStartupSequence *seq = l->data;

          wmclass = sn_startup_sequence_get_wmclass (seq);

          if (wmclass != NULL &&
              ((window->res_class &&
                strcmp (wmclass, window->res_class) == 0) ||
               (window->res_name &&
                strcmp (wmclass, window->res_name) == 0)))
            {
              sequence = seq;

              g_assert (window->startup_id == NULL);
              window->startup_id = g_strdup (sn_startup_sequence_get_id (sequence));
              startup_id = window->startup_id;

              meta_topic (META_DEBUG_STARTUP,
                          "Ending legacy sequence %s due to window %s\n",
                          sn_startup_sequence_get_id (sequence),
                          window->desc);

              sn_startup_sequence_complete (sequence);
              break;
            }
        }
    }

  /* Still no startup ID? Bail. */
  if (startup_id == NULL)
    return FALSE;

  /* We might get this far and not know the sequence ID (if the window
   * already had a startup ID stored), so let's look for one if we don't
   * already know it.
   */
  if (sequence == NULL)
    {
      for (l = screen->startup_sequences; l != NULL; l = l->next)
        {
          SnStartupSequence *seq = l->data;
          const char *id;

          id = sn_startup_sequence_get_id (seq);

          if (strcmp (id, startup_id) == 0)
            {
              sequence = seq;
              break;
            }
        }
    }

  if (sequence != NULL)
    {
      gboolean changed_something = FALSE;

      meta_topic (META_DEBUG_STARTUP,
                  "Found startup sequence for window %s ID \"%s\"\n",
                  window->desc, startup_id);

      if (!window->initial_workspace_set)
        {
          int space = sn_startup_sequence_get_workspace (sequence);
          if (space >= 0)
            {
              meta_topic (META_DEBUG_STARTUP,
                          "Setting initial window workspace to %d based on startup info\n",
                          space);

              window->initial_workspace_set = TRUE;
              window->initial_workspace = space;
              changed_something = TRUE;
            }
        }

      if (!window->initial_timestamp_set)
        {
          guint32 timestamp = sn_startup_sequence_get_timestamp (sequence);
          meta_topic (META_DEBUG_STARTUP,
                      "Setting initial window timestamp to %u based on startup info\n",
                      timestamp);

          window->initial_timestamp_set = TRUE;
          window->initial_timestamp = timestamp;
          changed_something = TRUE;
        }

      return changed_something;
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Did not find startup sequence for window %s ID \"%s\"\n",
                  window->desc, startup_id);
    }

#endif /* HAVE_STARTUP_NOTIFICATION */

  return FALSE;
}

int
meta_screen_get_screen_number (MetaScreen *screen)
{
  return meta_ui_get_screen_number ();
}

/**
 * meta_screen_get_display:
 * @screen: A #MetaScreen
 *
 * Retrieve the display associated with screen.
 *
 * Returns: (transfer none): Display
 */
MetaDisplay *
meta_screen_get_display (MetaScreen *screen)
{
  return screen->display;
}

/**
 * meta_screen_get_xroot: (skip)
 * @screen: A #MetaScreen
 *
 */
Window
meta_screen_get_xroot (MetaScreen *screen)
{
  return screen->xroot;
}

/**
 * meta_screen_get_size:
 * @screen: A #MetaScreen
 * @width: (out): The width of the screen
 * @height: (out): The height of the screen
 *
 * Retrieve the size of the screen.
 */
void
meta_screen_get_size (MetaScreen *screen,
                      int        *width,
                      int        *height)
{
  if (width != NULL)
    *width = screen->rect.width;

  if (height != NULL)
    *height = screen->rect.height;
}

void
meta_screen_set_cm_selection (MetaScreen *screen)
{
  char selection[32];
  Atom a;
  guint32 timestamp;

  timestamp = meta_display_get_current_time_roundtrip (screen->display);
  g_snprintf (selection, sizeof (selection), "_NET_WM_CM_S%d",
              meta_ui_get_screen_number ());
  a = XInternAtom (screen->display->xdisplay, selection, False);
  screen->wm_cm_selection_window = take_manager_selection (screen->display, screen->xroot, a, timestamp, TRUE);
}

/**
 * meta_screen_get_workspaces: (skip)
 * @screen: a #MetaScreen
 *
 * Returns: (transfer none) (element-type Meta.Workspace): The workspaces for @screen
 */
GList *
meta_screen_get_workspaces (MetaScreen *screen)
{
  return screen->workspaces;
}

int
meta_screen_get_active_workspace_index (MetaScreen *screen)
{
  MetaWorkspace *active = screen->active_workspace;

  if (!active)
    return -1;

  return meta_workspace_index (active);
}

/**
 * meta_screen_get_active_workspace:
 * @screen: A #MetaScreen
 *
 * Returns: (transfer none): The current workspace
 */
MetaWorkspace *
meta_screen_get_active_workspace (MetaScreen *screen)
{
  return screen->active_workspace;
}

void
meta_screen_focus_default_window (MetaScreen *screen,
                                  guint32     timestamp)
{
  meta_workspace_focus_default_window (screen->active_workspace,
                                       NULL,
                                       timestamp);
}

void
meta_screen_restacked (MetaScreen *screen)
{
  g_signal_emit (screen, screen_signals[RESTACKED], 0);
}

void
meta_screen_workspace_switched (MetaScreen         *screen,
                                int                 from,
                                int                 to,
                                MetaMotionDirection direction)
{
  g_signal_emit (screen, screen_signals[WORKSPACE_SWITCHED], 0,
                 from, to, direction);
}

void
meta_screen_set_active_workspace_hint (MetaScreen *screen)
{
  unsigned long data[1];

  /* this is because we destroy the spaces in order,
   * so we always end up setting a current desktop of
   * 0 when closing a screen, so lose the current desktop
   * on restart. By doing this we keep the current
   * desktop on restart.
   */
  if (screen->closing > 0)
    return;

  data[0] = meta_workspace_index (screen->active_workspace);

  meta_verbose ("Setting _NET_CURRENT_DESKTOP to %lu\n", data[0]);

  meta_error_trap_push (screen->display);
  XChangeProperty (screen->display->xdisplay, screen->xroot,
                   screen->display->atom__NET_CURRENT_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  meta_error_trap_pop (screen->display);
}

static gboolean
check_fullscreen_func (gpointer data)
{
  MetaScreen *screen = data;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;
  MetaWindow *window;
  GSList *fullscreen_monitors = NULL;
  GSList *obscured_monitors = NULL;
  gboolean in_fullscreen_changed = FALSE;

  screen->check_fullscreen_later = 0;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  /* We consider a monitor in fullscreen if it contains a fullscreen window;
   * however we make an exception for maximized windows above the fullscreen
   * one, as in that case window+chrome fully obscure the fullscreen window.
   */
  for (window = meta_stack_get_top (screen->stack);
       window;
       window = meta_stack_get_below (screen->stack, window, FALSE))
    {
      gboolean covers_monitors = FALSE;

      if (window->screen != screen || window->hidden)
        continue;

      if (window->fullscreen)
        {
          covers_monitors = TRUE;
        }
      else if (window->override_redirect)
        {
          /* We want to handle the case where an application is creating an
           * override-redirect window the size of the screen (monitor) and treat
           * it similarly to a fullscreen window, though it doesn't have fullscreen
           * window management behavior. (Being O-R, it's not managed at all.)
           */
          if (meta_window_is_monitor_sized (window))
            covers_monitors = TRUE;
        }
      else if (window->maximized_horizontally &&
               window->maximized_vertically)
        {
          MetaLogicalMonitor *logical_monitor;

          logical_monitor = meta_window_get_main_logical_monitor (window);
          if (!g_slist_find (obscured_monitors, logical_monitor))
            obscured_monitors = g_slist_prepend (obscured_monitors,
                                                 logical_monitor);
        }

      if (covers_monitors)
        {
          MetaRectangle window_rect;

          meta_window_get_frame_rect (window, &window_rect);

          for (l = logical_monitors; l; l = l->next)
            {
              MetaLogicalMonitor *logical_monitor = l->data;

              if (meta_rectangle_overlap (&window_rect,
                                          &logical_monitor->rect) &&
                  !g_slist_find (fullscreen_monitors, logical_monitor) &&
                  !g_slist_find (obscured_monitors, logical_monitor))
                fullscreen_monitors = g_slist_prepend (fullscreen_monitors,
                                                       logical_monitor);
            }
        }
    }

  g_slist_free (obscured_monitors);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      gboolean in_fullscreen;

      in_fullscreen = g_slist_find (fullscreen_monitors,
                                    logical_monitor) != NULL;
      if (in_fullscreen != logical_monitor->in_fullscreen)
        {
          logical_monitor->in_fullscreen = in_fullscreen;
          in_fullscreen_changed = TRUE;
        }
    }

  g_slist_free (fullscreen_monitors);

  if (in_fullscreen_changed)
    {
      /* DOCK window stacking depends on the monitor's fullscreen
         status so we need to trigger a re-layering. */
      MetaWindow *window = meta_stack_get_top (screen->stack);
      if (window)
        meta_stack_update_layer (screen->stack, window);

      g_signal_emit (screen, screen_signals[IN_FULLSCREEN_CHANGED], 0, NULL);
    }

  return FALSE;
}

void
meta_screen_queue_check_fullscreen (MetaScreen *screen)
{
  if (!screen->check_fullscreen_later)
    screen->check_fullscreen_later = meta_later_add (META_LATER_CHECK_FULLSCREEN,
                                                     check_fullscreen_func,
                                                     screen, NULL);
}

/**
 * meta_screen_get_monitor_in_fullscreen:
 * @screen: a #MetaScreen
 * @monitor: the monitor number
 *
 * Determines whether there is a fullscreen window obscuring the specified
 * monitor. If there is a fullscreen window, the desktop environment will
 * typically hide any controls that might obscure the fullscreen window.
 *
 * You can get notification when this changes by connecting to
 * MetaScreen::in-fullscreen-changed.
 *
 * Returns: %TRUE if there is a fullscreen window covering the specified monitor.
 */
gboolean
meta_screen_get_monitor_in_fullscreen (MetaScreen  *screen,
                                       int          monitor)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
#ifndef G_DISABLE_CHECKS
  int n_logical_monitors =
    meta_monitor_manager_get_num_logical_monitors (monitor_manager);
#endif

  g_return_val_if_fail (META_IS_SCREEN (screen), FALSE);
  g_return_val_if_fail (monitor >= 0 &&
                        monitor < n_logical_monitors, FALSE);

  logical_monitor =
    meta_monitor_manager_get_logical_monitor_from_number (monitor_manager,
                                                          monitor);

  /* We use -1 as a flag to mean "not known yet" for notification purposes */
  return logical_monitor->in_fullscreen == TRUE;
}

gboolean
meta_screen_handle_xevent (MetaScreen *screen,
                           XEvent     *xevent)
{
  MetaBackend *backend = meta_get_backend ();
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  if (meta_cursor_tracker_handle_xevent (cursor_tracker, xevent))
    return TRUE;

  return FALSE;
}
