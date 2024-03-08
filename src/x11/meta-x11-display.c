/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003, 2004 Red Hat, Inc.
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

/**
 * MetaX11Display:
 *
 * Mutter X display handler
 *
 * The X11 display is represented as a #MetaX11Display struct.
 */

#include "config.h"

#include "core/display-private.h"
#include "x11/meta-x11-display-private.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dnd-private.h"
#include "backends/meta-cursor-sprite-xcursor.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-settings-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "core/frame.h"
#include "core/meta-workspace-manager-private.h"
#include "core/util-private.h"
#include "core/workspace-private.h"
#include "meta/main.h"
#include "mtk/mtk-x11.h"
#include "x11/events.h"
#include "x11/group-props.h"
#include "x11/meta-x11-selection-private.h"
#include "x11/window-props.h"
#include "x11/window-x11.h"
#include "x11/xprops.h"

#ifdef HAVE_XWAYLAND
#include "wayland/meta-xwayland-private.h"
#endif

G_DEFINE_TYPE (MetaX11Display, meta_x11_display, G_TYPE_OBJECT)

static GQuark quark_x11_display_logical_monitor_data = 0;

typedef struct _MetaX11EventFilter MetaX11EventFilter;

struct _MetaX11EventFilter
{
  unsigned int id;
  MetaX11DisplayEventFunc func;
  gpointer user_data;
  GDestroyNotify destroy_notify;
};

typedef struct _MetaX11DisplayLogicalMonitorData
{
  int xinerama_index;
} MetaX11DisplayLogicalMonitorData;

static char *get_screen_name (Display *xdisplay,
                              int      number);

static void on_monitors_changed_internal (MetaMonitorManager *monitor_manager,
                                          MetaX11Display     *x11_display);

static void update_cursor_theme (MetaX11Display *x11_display);
static void unset_wm_check_hint (MetaX11Display *x11_display);

static void prefs_changed_callback (MetaPreference pref,
                                    void          *data);

static void meta_x11_display_init_frames_client (MetaX11Display *x11_display);

static void meta_x11_display_remove_cursor_later (MetaX11Display *x11_display);

static void meta_x11_display_set_input_focus (MetaX11Display *x11_display,
                                              MetaWindow     *window,
                                              uint32_t        timestamp);

static Window meta_x11_display_create_offscreen_window (MetaX11Display *x11_display,
                                                        Window          parent,
                                                        long            valuemask);

static MetaBackend *
backend_from_x11_display (MetaX11Display *x11_display)
{
  MetaDisplay *display = meta_x11_display_get_display (x11_display);
  MetaContext *context = meta_display_get_context (display);

  return meta_context_get_backend (context);
}

static void
meta_x11_display_unmanage_windows (MetaX11Display *x11_display)
{
  GList *windows, *l;

  if (!x11_display->xids)
    return;

  windows = g_hash_table_get_values (x11_display->xids);
  g_list_foreach (windows, (GFunc) g_object_ref, NULL);

  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = META_WINDOW (l->data);

      if (!window->unmanaging)
        meta_window_unmanage (window, META_CURRENT_TIME);
    }
  g_list_free_full (windows, g_object_unref);
}

static void
meta_x11_event_filter_free (MetaX11EventFilter *filter)
{
  if (filter->destroy_notify && filter->user_data)
    filter->destroy_notify (filter->user_data);
  g_free (filter);
}

static void
meta_x11_display_dispose (GObject *object)
{
  MetaX11Display *x11_display = META_X11_DISPLAY (object);

  x11_display->closing = TRUE;

  g_clear_pointer (&x11_display->alarm_filters, g_ptr_array_unref);

  g_clear_list (&x11_display->event_funcs,
                (GDestroyNotify) meta_x11_event_filter_free);

  if (x11_display->frames_client_cancellable)
    {
      g_cancellable_cancel (x11_display->frames_client_cancellable);
      g_clear_object (&x11_display->frames_client_cancellable);
    }

  if (x11_display->frames_client)
    {
      g_subprocess_send_signal (x11_display->frames_client, SIGTERM);
      if (x11_display->display->closing)
        g_subprocess_wait (x11_display->frames_client, NULL, NULL);
      g_clear_object (&x11_display->frames_client);
    }

  if (x11_display->empty_region != None)
    {
      XFixesDestroyRegion (x11_display->xdisplay,
                           x11_display->empty_region);
      x11_display->empty_region = None;
    }

  meta_x11_startup_notification_release (x11_display);

  meta_prefs_remove_listener (prefs_changed_callback, x11_display);

  meta_x11_display_ungrab_keys (x11_display);

  g_clear_object (&x11_display->x11_stack);

  meta_x11_selection_shutdown (x11_display);
  meta_x11_display_unmanage_windows (x11_display);

  if (x11_display->no_focus_window != None)
    {
      XUnmapWindow (x11_display->xdisplay, x11_display->no_focus_window);
      XDestroyWindow (x11_display->xdisplay, x11_display->no_focus_window);

      x11_display->no_focus_window = None;
    }

  if (x11_display->composite_overlay_window != None)
    {
      XCompositeReleaseOverlayWindow (x11_display->xdisplay,
                                      x11_display->composite_overlay_window);

      x11_display->composite_overlay_window = None;
    }

  if (x11_display->wm_sn_selection_window != None)
    {
      XDestroyWindow (x11_display->xdisplay, x11_display->wm_sn_selection_window);
      x11_display->wm_sn_selection_window = None;
    }

  if (x11_display->timestamp_pinging_window != None)
    {
      XDestroyWindow (x11_display->xdisplay, x11_display->timestamp_pinging_window);
      x11_display->timestamp_pinging_window = None;
    }

  if (x11_display->leader_window != None)
    {
      XDestroyWindow (x11_display->xdisplay, x11_display->leader_window);
      x11_display->leader_window = None;
    }

  if (x11_display->guard_window != None)
    {
      XUnmapWindow (x11_display->xdisplay, x11_display->guard_window);
      XDestroyWindow (x11_display->xdisplay, x11_display->guard_window);
      x11_display->guard_window = None;
    }

  if (x11_display->prop_hooks)
    {
      meta_x11_display_free_window_prop_hooks (x11_display);
      x11_display->prop_hooks = NULL;
    }

  if (x11_display->group_prop_hooks)
    {
      meta_x11_display_free_group_prop_hooks (x11_display);
      x11_display->group_prop_hooks = NULL;
    }

  if (x11_display->xids)
    {
      /* Must be after all calls to meta_window_unmanage() since they
       * unregister windows
       */
      g_hash_table_destroy (x11_display->xids);
      x11_display->xids = NULL;
    }

  g_clear_pointer (&x11_display->alarms, g_hash_table_unref);

  if (x11_display->xroot != None)
    {
      unset_wm_check_hint (x11_display);

      mtk_x11_error_trap_push (x11_display->xdisplay);
      XSelectInput (x11_display->xdisplay, x11_display->xroot, 0);
      if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
        meta_warning ("Could not release screen %d on display \"%s\"",
                      DefaultScreen (x11_display->xdisplay),
                      x11_display->name);

      x11_display->xroot = None;
    }

  if (x11_display->xdisplay)
    {
      meta_x11_display_free_events (x11_display);

      XCloseDisplay (x11_display->xdisplay);
      x11_display->xdisplay = NULL;
    }

  g_clear_handle_id (&x11_display->display_close_idle, g_source_remove);

  meta_x11_display_remove_cursor_later (x11_display);

  g_free (x11_display->name);
  x11_display->name = NULL;

  g_free (x11_display->screen_name);
  x11_display->screen_name = NULL;

  G_OBJECT_CLASS (meta_x11_display_parent_class)->dispose (object);
}

static void
meta_x11_display_finalize (GObject *object)
{
  mtk_x11_errors_deinit ();

  G_OBJECT_CLASS (meta_x11_display_parent_class)->finalize (object);
}

static void
on_x11_display_opened (MetaX11Display *x11_display,
                       MetaDisplay    *display)
{
  meta_display_manage_all_xwindows (display);
  meta_x11_display_redirect_windows (x11_display, display);
}

static void
meta_x11_display_class_init (MetaX11DisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_x11_display_dispose;
  object_class->finalize = meta_x11_display_finalize;
}

static void
meta_x11_display_init (MetaX11Display *x11_display)
{
  quark_x11_display_logical_monitor_data =
    g_quark_from_static_string ("-meta-x11-display-logical-monitor-data");
}

static void
query_xsync_extension (MetaX11Display *x11_display)
{
  int major, minor;

  x11_display->have_xsync = FALSE;

  x11_display->xsync_error_base = 0;
  x11_display->xsync_event_base = 0;

  /* I don't think we really have to fill these in */
  major = SYNC_MAJOR_VERSION;
  minor = SYNC_MINOR_VERSION;

  if (!XSyncQueryExtension (x11_display->xdisplay,
                            &x11_display->xsync_event_base,
                            &x11_display->xsync_error_base) ||
      !XSyncInitialize (x11_display->xdisplay,
                        &major, &minor))
    {
      x11_display->xsync_error_base = 0;
      x11_display->xsync_event_base = 0;
    }
  else
    {
      x11_display->have_xsync = TRUE;
      XSyncSetPriority (x11_display->xdisplay, None, 10);
    }

  meta_verbose ("Attempted to init Xsync, found version %d.%d error base %d event base %d",
                major, minor,
                x11_display->xsync_error_base,
                x11_display->xsync_event_base);
}

static void
query_xshape_extension (MetaX11Display *x11_display)
{
  x11_display->have_shape = FALSE;

  x11_display->shape_error_base = 0;
  x11_display->shape_event_base = 0;

  if (!XShapeQueryExtension (x11_display->xdisplay,
                             &x11_display->shape_event_base,
                             &x11_display->shape_error_base))
    {
      x11_display->shape_error_base = 0;
      x11_display->shape_event_base = 0;
    }
  else
    x11_display->have_shape = TRUE;

  meta_verbose ("Attempted to init Shape, found error base %d event base %d",
                x11_display->shape_error_base,
                x11_display->shape_event_base);
}

static void
query_xcomposite_extension (MetaX11Display *x11_display)
{
  x11_display->have_composite = FALSE;

  x11_display->composite_error_base = 0;
  x11_display->composite_event_base = 0;

  if (!XCompositeQueryExtension (x11_display->xdisplay,
                                 &x11_display->composite_event_base,
                                 &x11_display->composite_error_base))
    {
      x11_display->composite_error_base = 0;
      x11_display->composite_event_base = 0;
    }
  else
    {
      x11_display->composite_major_version = 0;
      x11_display->composite_minor_version = 0;
      if (XCompositeQueryVersion (x11_display->xdisplay,
                                  &x11_display->composite_major_version,
                                  &x11_display->composite_minor_version))
        {
          x11_display->have_composite = TRUE;
        }
      else
        {
          x11_display->composite_major_version = 0;
          x11_display->composite_minor_version = 0;
        }
    }

  meta_verbose ("Attempted to init Composite, found error base %d event base %d "
                "extn ver %d %d",
                x11_display->composite_error_base,
                x11_display->composite_event_base,
                x11_display->composite_major_version,
                x11_display->composite_minor_version);
}

static void
query_xdamage_extension (MetaX11Display *x11_display)
{
  x11_display->have_damage = FALSE;

  x11_display->damage_error_base = 0;
  x11_display->damage_event_base = 0;

  if (!XDamageQueryExtension (x11_display->xdisplay,
                              &x11_display->damage_event_base,
                              &x11_display->damage_error_base))
    {
      x11_display->damage_error_base = 0;
      x11_display->damage_event_base = 0;
    }
  else
    x11_display->have_damage = TRUE;

  meta_verbose ("Attempted to init Damage, found error base %d event base %d",
                x11_display->damage_error_base,
                x11_display->damage_event_base);
}

static void
query_xfixes_extension (MetaX11Display *x11_display)
{
  x11_display->xfixes_error_base = 0;
  x11_display->xfixes_event_base = 0;

  if (XFixesQueryExtension (x11_display->xdisplay,
                            &x11_display->xfixes_event_base,
                            &x11_display->xfixes_error_base))
    {
      int xfixes_major, xfixes_minor;

      XFixesQueryVersion (x11_display->xdisplay, &xfixes_major, &xfixes_minor);

      if (xfixes_major * 100 + xfixes_minor < 500)
        meta_fatal ("Mutter requires XFixes 5.0");
    }
  else
    {
      meta_fatal ("Mutter requires XFixes 5.0");
    }

  meta_verbose ("Attempted to init XFixes, found error base %d event base %d",
                x11_display->xfixes_error_base,
                x11_display->xfixes_event_base);
}

static void
query_xi_extension (MetaX11Display *x11_display)
{
  int major = 2, minor = 3;
  gboolean has_xi = FALSE;

  if (XQueryExtension (x11_display->xdisplay,
                       "XInputExtension",
                       &x11_display->xinput_opcode,
                       &x11_display->xinput_error_base,
                       &x11_display->xinput_event_base))
    {
      if (XIQueryVersion (x11_display->xdisplay, &major, &minor) == Success)
        has_xi = TRUE;
    }

  if (!has_xi)
    meta_fatal ("X server doesn't have the XInput extension, version 2.2 or newer");
}

/*
 * Initialises the bell subsystem. This involves initialising
 * XKB (which, despite being a keyboard extension, is the
 * place to look for bell notifications), then asking it
 * to send us bell notifications, and then also switching
 * off the audible bell if we're using a visual one ourselves.
 *
 * \bug There is a line of code that's never run that tells
 * XKB to reset the bell status after we quit. Bill H said
 * (<http://bugzilla.gnome.org/show_bug.cgi?id=99886#c12>)
 * that XFree86's implementation is broken so we shouldn't
 * call it, but that was in 2002. Is it working now?
 */
static void
init_x11_bell (MetaX11Display *x11_display)
{
  int xkb_base_error_type, xkb_opcode;

  if (!XkbQueryExtension (x11_display->xdisplay, &xkb_opcode,
                          &x11_display->xkb_base_event_type,
                          &xkb_base_error_type,
                          NULL, NULL))
    {
      x11_display->xkb_base_event_type = -1;
      meta_warning ("could not find XKB extension.");
    }
  else
    {
      unsigned int mask = XkbBellNotifyMask;
      gboolean visual_bell_auto_reset = FALSE;
      /* TRUE if and when non-broken version is available */
      XkbSelectEvents (x11_display->xdisplay,
                       XkbUseCoreKbd,
                       XkbBellNotifyMask,
                       XkbBellNotifyMask);

      if (visual_bell_auto_reset)
        {
          XkbSetAutoResetControls (x11_display->xdisplay,
                                   XkbAudibleBellMask,
                                   &mask,
                                   &mask);
        }
    }

  /* We are playing sounds using libcanberra support, we handle the
   * bell whether its an audible bell or a visible bell */
  XkbChangeEnabledControls (x11_display->xdisplay,
                            XkbUseCoreKbd,
                            XkbAudibleBellMask,
                            0);
}

/*
 * \bug This is never called! If we had XkbSetAutoResetControls
 * enabled in meta_x11_bell_init(), this wouldn't be a problem,
 * but we don't.
 */
G_GNUC_UNUSED static void
shutdown_x11_bell (MetaX11Display *x11_display)
{
  /* TODO: persist initial bell state in display, reset here */
  XkbChangeEnabledControls (x11_display->xdisplay,
                            XkbUseCoreKbd,
                            XkbAudibleBellMask,
                            XkbAudibleBellMask);
}

static void
set_desktop_geometry_hint (MetaX11Display *x11_display)
{
  unsigned long data[2];
  int monitor_width, monitor_height;

  if (x11_display->display->closing > 0)
    return;

  meta_display_get_size (x11_display->display, &monitor_width, &monitor_height);

  data[0] = monitor_width;
  data[1] = monitor_height;

  meta_verbose ("Setting _NET_DESKTOP_GEOMETRY to %lu, %lu", data[0], data[1]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_DESKTOP_GEOMETRY,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
set_desktop_viewport_hint (MetaX11Display *x11_display)
{
  unsigned long data[2];

  if (x11_display->display->closing > 0)
    return;

  /*
   * Mutter does not implement viewports, so this is a fixed 0,0
   */
  data[0] = 0;
  data[1] = 0;

  meta_verbose ("Setting _NET_DESKTOP_VIEWPORT to 0, 0");

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_DESKTOP_VIEWPORT,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 2);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static int
set_wm_check_hint (MetaX11Display *x11_display)
{
  unsigned long data[1];

  g_return_val_if_fail (x11_display->leader_window != None, 0);

  data[0] = x11_display->leader_window;

  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);

  return Success;
}

static void
unset_wm_check_hint (MetaX11Display *x11_display)
{
  XDeleteProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_SUPPORTING_WM_CHECK);
}

static int
set_supported_hint (MetaX11Display *x11_display)
{
  Atom atoms[] = {
#define EWMH_ATOMS_ONLY
#define item(x)  x11_display->atom_##x,
#include "x11/atomnames.h"
#undef item
#undef EWMH_ATOMS_ONLY

    x11_display->atom__GTK_FRAME_EXTENTS,
    x11_display->atom__GTK_SHOW_WINDOW_MENU,
    x11_display->atom__GTK_EDGE_CONSTRAINTS,
    x11_display->atom__GTK_WORKAREAS,
  };

  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_SUPPORTED,
                   XA_ATOM,
                   32, PropModeReplace,
                   (guchar*) atoms, G_N_ELEMENTS(atoms));

  return Success;
}

static int
set_wm_icon_size_hint (MetaX11Display *x11_display)
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

  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom_WM_ICON_SIZE,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) vals, N_VALS);

  return Success;
#undef N_VALS
}

static Window
take_manager_selection (MetaX11Display *x11_display,
                        Window          xroot,
                        Atom            manager_atom,
                        int             timestamp,
                        gboolean        should_replace)
{
  Window current_owner, new_owner;

  current_owner = XGetSelectionOwner (x11_display->xdisplay, manager_atom);
  if (current_owner != None)
    {
      XSetWindowAttributes attrs;

      if (should_replace)
        {
          /* We want to find out when the current selection owner dies */
          mtk_x11_error_trap_push (x11_display->xdisplay);
          attrs.event_mask = StructureNotifyMask;
          XChangeWindowAttributes (x11_display->xdisplay, current_owner, CWEventMask, &attrs);
          if (mtk_x11_error_trap_pop_with_return (x11_display->xdisplay) != Success)
            current_owner = None; /* don't wait for it to die later on */
        }
      else
        {
          meta_warning (_("Display “%s” already has a window manager; try using the --replace option to replace the current window manager."),
                        x11_display->name);
          return None;
        }
    }

  /* We need SelectionClear and SelectionRequest events on the new owner,
   * but those cannot be masked, so we only need NoEventMask.
   */
  new_owner = meta_x11_display_create_offscreen_window (x11_display, xroot, NoEventMask);

  XSetSelectionOwner (x11_display->xdisplay, manager_atom, new_owner, timestamp);

  if (XGetSelectionOwner (x11_display->xdisplay, manager_atom) != new_owner)
    {
      meta_warning ("Could not acquire selection: %s", XGetAtomName (x11_display->xdisplay, manager_atom));
      return None;
    }

  {
    /* Send client message indicating that we are now the selection owner */
    XClientMessageEvent ev = { 0, };

    ev.type = ClientMessage;
    ev.window = xroot;
    ev.message_type = x11_display->atom_MANAGER;
    ev.format = 32;
    ev.data.l[0] = timestamp;
    ev.data.l[1] = manager_atom;

    XSendEvent (x11_display->xdisplay, xroot, False, StructureNotifyMask, (XEvent *) &ev);
  }

  /* Wait for old window manager to go away */
  if (current_owner != None)
    {
      XEvent event;

#ifdef HAVE_XWAYLAND
      g_return_val_if_fail (!meta_is_wayland_compositor (), new_owner);
#endif

      /* We sort of block infinitely here which is probably lame. */

      meta_verbose ("Waiting for old window manager to exit");
      do
        XWindowEvent (x11_display->xdisplay, current_owner, StructureNotifyMask, &event);
      while (event.type != DestroyNotify);
    }

  return new_owner;
}

/* Create the leader window here. Set its properties and
 * use the timestamp from one of the PropertyNotify events
 * that will follow.
 */
static void
init_leader_window (MetaX11Display *x11_display,
                    guint32        *timestamp)
{
  MetaContext *context = meta_display_get_context (x11_display->display);
  const char *gnome_wm_keybindings;
  gulong data[1];
  XEvent event;

  x11_display->leader_window =
    meta_x11_display_create_offscreen_window (x11_display,
                                              x11_display->xroot,
                                              PropertyChangeMask);

  meta_prop_set_utf8_string_hint (x11_display,
                                  x11_display->leader_window,
                                  x11_display->atom__NET_WM_NAME,
                                  meta_context_get_name (context));

  gnome_wm_keybindings = meta_context_get_gnome_wm_keybindings (context);
  meta_prop_set_utf8_string_hint (x11_display,
                                  x11_display->leader_window,
                                  x11_display->atom__GNOME_WM_KEYBINDINGS,
                                  gnome_wm_keybindings);

  meta_prop_set_utf8_string_hint (x11_display,
                                  x11_display->leader_window,
                                  x11_display->atom__MUTTER_VERSION,
                                  VERSION);

  data[0] = x11_display->leader_window;
  XChangeProperty (x11_display->xdisplay,
                   x11_display->leader_window,
                   x11_display->atom__NET_SUPPORTING_WM_CHECK,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);

  XWindowEvent (x11_display->xdisplay,
                x11_display->leader_window,
                PropertyChangeMask,
                &event);

  if (timestamp)
   *timestamp = event.xproperty.time;

  /* Make it painfully clear that we can't rely on PropertyNotify events on
   * this window, as per bug 354213.
   */
  XSelectInput (x11_display->xdisplay,
                x11_display->leader_window,
                NoEventMask);
}

static void
init_event_masks (MetaX11Display *x11_display)
{
  long event_mask;
  unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

  XISetMask (mask.mask, XI_Enter);
  XISetMask (mask.mask, XI_Leave);
  XISetMask (mask.mask, XI_FocusIn);
  XISetMask (mask.mask, XI_FocusOut);
  XISelectEvents (x11_display->xdisplay, x11_display->xroot, &mask, 1);

  event_mask = (SubstructureRedirectMask | SubstructureNotifyMask |
                StructureNotifyMask | ColormapChangeMask | PropertyChangeMask);
  XSelectInput (x11_display->xdisplay, x11_display->xroot, event_mask);
}

static void
set_active_workspace_hint (MetaWorkspaceManager *workspace_manager,
                           MetaX11Display       *x11_display)
{
  unsigned long data[1];

  /* this is because we destroy the spaces in order,
   * so we always end up setting a current desktop of
   * 0 when closing a screen, so lose the current desktop
   * on restart. By doing this we keep the current
   * desktop on restart.
   */
  if (x11_display->display->closing > 0)
    return;

  data[0] = meta_workspace_index (workspace_manager->active_workspace);

  meta_verbose ("Setting _NET_CURRENT_DESKTOP to %lu", data[0]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_CURRENT_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
set_number_of_spaces_hint (MetaWorkspaceManager *workspace_manager,
                           GParamSpec           *pspec,
                           gpointer              user_data)
{
  MetaX11Display *x11_display = user_data;
  unsigned long data[1];

  if (x11_display->display->closing > 0)
    return;

  data[0] = meta_workspace_manager_get_n_workspaces (workspace_manager);

  meta_verbose ("Setting _NET_NUMBER_OF_DESKTOPS to %lu", data[0]);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_NUMBER_OF_DESKTOPS,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
set_showing_desktop_hint (MetaWorkspaceManager *workspace_manager,
                          MetaX11Display       *x11_display)
{
  unsigned long data[1];

  data[0] = workspace_manager->active_workspace->showing_desktop ? 1 : 0;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_SHOWING_DESKTOP,
                   XA_CARDINAL,
                   32, PropModeReplace, (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
set_workspace_names (MetaX11Display *x11_display)
{
  MetaWorkspaceManager *workspace_manager;
  GString *flattened;
  int i;
  int n_spaces;

  workspace_manager = x11_display->display->workspace_manager;

  /* flatten to nul-separated list */
  n_spaces = meta_workspace_manager_get_n_workspaces (workspace_manager);
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

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_DESKTOP_NAMES,
                   x11_display->atom_UTF8_STRING,
                   8, PropModeReplace,
                   (unsigned char *)flattened->str, flattened->len);
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  g_string_free (flattened, TRUE);
}

static void
set_workspace_work_area_hint (MetaWorkspace  *workspace,
                              MetaX11Display *x11_display)
{
  MetaMonitorManager *monitor_manager;
  GList *logical_monitors;
  GList *l;
  int num_monitors;
  unsigned long *data;
  unsigned long *tmp;
  g_autofree char *workarea_name = NULL;
  Atom workarea_atom;

  monitor_manager =
    meta_backend_get_monitor_manager (backend_from_x11_display (x11_display));
  logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);
  num_monitors = meta_monitor_manager_get_num_logical_monitors (monitor_manager);

  data = g_new (unsigned long, num_monitors * 4);
  tmp = data;

  for (l = logical_monitors; l; l = l->next)
    {
      MtkRectangle area;

      meta_workspace_get_work_area_for_logical_monitor (workspace, l->data, &area);

      tmp[0] = area.x;
      tmp[1] = area.y;
      tmp[2] = area.width;
      tmp[3] = area.height;

      tmp += 4;
    }

  workarea_name = g_strdup_printf ("_GTK_WORKAREAS_D%d",
                                   meta_workspace_index (workspace));

  workarea_atom = XInternAtom (x11_display->xdisplay, workarea_name, False);

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   workarea_atom,
                   XA_CARDINAL, 32, PropModeReplace,
                   (guchar*) data, num_monitors * 4);
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  g_free (data);
}

static void
set_work_area_hint (MetaDisplay    *display,
                    MetaX11Display *x11_display)
{
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;
  int num_workspaces;
  GList *l;
  unsigned long *data, *tmp;
  MtkRectangle area;

  num_workspaces = meta_workspace_manager_get_n_workspaces (workspace_manager);
  data = g_new (unsigned long, num_workspaces * 4);
  tmp = data;

  for (l = workspace_manager->workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;

      meta_workspace_get_work_area_all_monitors (workspace, &area);
      set_workspace_work_area_hint (workspace, x11_display);

      tmp[0] = area.x;
      tmp[1] = area.y;
      tmp[2] = area.width;
      tmp[3] = area.height;

      tmp += 4;
    }

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_WORKAREA,
                   XA_CARDINAL, 32, PropModeReplace,
                   (guchar*) data, num_workspaces*4);
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  g_free (data);
}

static const char *
get_display_name (MetaDisplay *display)
{
#ifdef HAVE_XWAYLAND
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);

  if (compositor)
    return meta_wayland_get_private_xwayland_display_name (compositor);
  else
#endif
    return g_getenv ("DISPLAY");
}

static Display *
open_x_display (MetaDisplay  *display,
                GError      **error)
{
  const char *xdisplay_name;
  Display *xdisplay;

  xdisplay_name = get_display_name (display);
  if (!xdisplay_name)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unable to open display, DISPLAY not set");
      return NULL;
    }

  meta_verbose ("Opening display '%s'", xdisplay_name);

  xdisplay = XOpenDisplay (xdisplay_name);

  if (xdisplay == NULL)
    {
      meta_warning (_("Failed to open X Window System display “%s”"),
                    xdisplay_name);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to open X11 display");

      return NULL;
    }

  return xdisplay;
}

static void
on_frames_client_died (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  MetaX11Display *x11_display = user_data;
  GSubprocess *proc = G_SUBPROCESS (source);
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (proc, result, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Error obtaining frames client exit status: %s\n", error->message);
    }

  g_clear_object (&x11_display->frames_client_cancellable);
  g_clear_object (&x11_display->frames_client);

  if (g_subprocess_get_if_signaled (proc))
    {
      int signum;

      signum = g_subprocess_get_term_sig (proc);

      /* Bring it up again, unless it was forcibly closed */
      if (signum != SIGTERM && signum != SIGKILL)
        meta_x11_display_init_frames_client (x11_display);
    }
}

#ifdef HAVE_X11
static gboolean
stage_is_focused (MetaX11Display *x11_display)
{
  MetaDisplay *display = x11_display->display;
  ClutterStage *stage = CLUTTER_STAGE (meta_get_stage_for_display (display));
  Window xwindow = meta_x11_get_stage_window (stage);

  return x11_display->focus_xwindow == xwindow;
}

static gboolean
stage_has_focus_actor (MetaX11Display *x11_display)
{
  MetaDisplay *display = x11_display->display;
  ClutterStage *stage = CLUTTER_STAGE (meta_get_stage_for_display (display));
  ClutterActor *key_focus;

  key_focus = clutter_stage_get_key_focus (stage);

  return key_focus != CLUTTER_ACTOR (stage);
}

static void
on_stage_key_focus_changed (MetaX11Display *x11_display)
{
  MetaDisplay *display = x11_display->display;
  uint32_t timestamp;
  gboolean has_actor_focus, has_stage_focus;

  has_actor_focus = stage_has_focus_actor (x11_display);
  has_stage_focus = stage_is_focused (x11_display);
  if (has_actor_focus == has_stage_focus)
    return;

  timestamp = meta_display_get_current_time_roundtrip (display);

  if (has_actor_focus)
    meta_display_unset_input_focus (display, timestamp);
  else
    meta_display_focus_default_window (display, timestamp);
}
#endif

static void
focus_window_cb (MetaX11Display *x11_display,
                 MetaWindow     *window,
                 int64_t         timestamp_us)
{
  meta_x11_display_set_input_focus (x11_display, window,
                                    us2ms (timestamp_us));
}

static void
meta_x11_display_init_frames_client (MetaX11Display *x11_display)
{
  const char *display_name;

  display_name = get_display_name (x11_display->display);
  x11_display->frames_client_cancellable = g_cancellable_new ();
  x11_display->frames_client = meta_frame_launch_client (x11_display,
                                                         display_name);
  g_subprocess_wait_async (x11_display->frames_client,
                           x11_display->frames_client_cancellable,
                           on_frames_client_died, x11_display);
}

/**
 * meta_x11_display_new:
 *
 * Opens a new X11 display, sets it up, initialises all the X extensions
 * we will need.
 *
 * Returns: #MetaX11Display if the display was opened successfully,
 * and %NULL otherwise-- that is, if the display doesn't exist or
 * it already has a window manager, and sets the error appropriately.
 */
MetaX11Display *
meta_x11_display_new (MetaDisplay  *display,
                      GError      **error)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (MetaX11Display) x11_display = NULL;
  Display *xdisplay;
  Screen *xscreen;
  Window xroot;
  int i, number;
  Window new_wm_sn_owner;
  gboolean replace_current_wm;
  Atom wm_sn_atom;
  Atom wm_cm_atom;
  char buf[128];
  guint32 timestamp;
  Atom atom_restart_helper;
  Window restart_helper_window = None;
  gboolean is_restart = FALSE;

  /* A list of all atom names, so that we can intern them in one go. */
  const char *atom_names[] = {
#define item(x) #x,
#include "x11/atomnames.h"
#undef item
  };
  Atom atoms[G_N_ELEMENTS(atom_names)];

  xdisplay = open_x_display (display, error);
  if (!xdisplay)
    return NULL;

  XSynchronize (xdisplay, meta_context_is_x11_sync (context));

#ifdef HAVE_XWAYLAND
  if (meta_is_wayland_compositor ())
    {
      MetaWaylandCompositor *compositor =
        meta_context_get_wayland_compositor (context);

      meta_xwayland_setup_xdisplay (&compositor->xwayland_manager, xdisplay);
    }
#endif

  replace_current_wm =
    meta_context_is_replacing (meta_backend_get_context (backend));

  number = DefaultScreen (xdisplay);

  xroot = RootWindow (xdisplay, number);

  /* FVWM checks for None here, I don't know if this
   * ever actually happens
   */
  if (xroot == None)
    {
      meta_warning (_("Screen %d on display “%s” is invalid"),
                    number, XDisplayName (NULL));

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to open default X11 screen");

      XFlush (xdisplay);
      XCloseDisplay (xdisplay);

      return NULL;
    }

  xscreen = ScreenOfDisplay (xdisplay, number);

  atom_restart_helper = XInternAtom (xdisplay, "_MUTTER_RESTART_HELPER", False);
  restart_helper_window = XGetSelectionOwner (xdisplay, atom_restart_helper);
  if (restart_helper_window)
    {
      is_restart = TRUE;
      meta_set_is_restart (TRUE);
    }

  x11_display = g_object_new (META_TYPE_X11_DISPLAY, NULL);
  x11_display->display = display;

  /* here we use XDisplayName which is what the user
   * probably put in, vs. DisplayString(display) which is
   * canonicalized by XOpenDisplay()
   */
  x11_display->xdisplay = xdisplay;
  x11_display->xroot = xroot;

  x11_display->name = g_strdup (XDisplayName (NULL));
  x11_display->screen_name = get_screen_name (xdisplay, number);
  x11_display->default_xvisual = DefaultVisualOfScreen (xscreen);
  x11_display->default_depth = DefaultDepthOfScreen (xscreen);

  meta_verbose ("Creating %d atoms", (int) G_N_ELEMENTS (atom_names));
  XInternAtoms (xdisplay, (char **)atom_names, G_N_ELEMENTS (atom_names),
                False, atoms);

  i = 0;
#define item(x) x11_display->atom_##x = atoms[i++];
#include "x11/atomnames.h"
#undef item

  mtk_x11_errors_init ();

  query_xsync_extension (x11_display);
  query_xshape_extension (x11_display);
  query_xcomposite_extension (x11_display);
  query_xdamage_extension (x11_display);
  query_xfixes_extension (x11_display);
  query_xi_extension (x11_display);

  g_signal_connect_object (display,
                           "cursor-updated",
                           G_CALLBACK (update_cursor_theme),
                           x11_display,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (display,
                           "x11-display-opened",
                           G_CALLBACK (on_x11_display_opened),
                           x11_display,
                           G_CONNECT_SWAPPED);
  update_cursor_theme (x11_display);

  g_signal_connect_object (display,
                           "focus-window",
                           G_CALLBACK (focus_window_cb),
                           x11_display,
                           G_CONNECT_SWAPPED);

#ifdef HAVE_XWAYLAND
  if (!meta_is_wayland_compositor ())
#endif
    {
      ClutterStage *stage =
        CLUTTER_STAGE (meta_get_stage_for_display (display));

      g_signal_connect_object (stage,
                               "notify::key-focus",
                               G_CALLBACK (on_stage_key_focus_changed),
                               x11_display,
                               G_CONNECT_SWAPPED);
    }

  x11_display->xids = g_hash_table_new (meta_unsigned_long_hash,
                                        meta_unsigned_long_equal);
  x11_display->alarms = g_hash_table_new (meta_unsigned_long_hash,
                                          meta_unsigned_long_equal);

  x11_display->groups_by_leader = NULL;
  x11_display->composite_overlay_window = None;
  x11_display->guard_window = None;
  x11_display->leader_window = None;
  x11_display->timestamp_pinging_window = None;
  x11_display->wm_sn_selection_window = None;

  x11_display->display_close_idle = 0;
  x11_display->xselectionclear_timestamp = 0;

  x11_display->last_bell_time = 0;
  x11_display->focus_serial = 0;
  x11_display->server_focus_window = None;
  x11_display->server_focus_serial = 0;

  x11_display->prop_hooks = NULL;
  meta_x11_display_init_window_prop_hooks (x11_display);
  x11_display->group_prop_hooks = NULL;
  meta_x11_display_init_group_prop_hooks (x11_display);

  g_signal_connect_object (monitor_manager,
                           "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed_internal),
                           x11_display,
                           0);

  init_leader_window (x11_display, &timestamp);
  x11_display->timestamp = timestamp;

  /* Make a little window used only for pinging the server for timestamps; note
   * that meta_create_offscreen_window already selects for PropertyChangeMask.
   */
  x11_display->timestamp_pinging_window =
    meta_x11_display_create_offscreen_window (x11_display,
                                              xroot,
                                              PropertyChangeMask);

  /* Select for cursor changes so the cursor tracker is up to date. */
  XFixesSelectCursorInput (xdisplay, xroot, XFixesDisplayCursorNotifyMask);

  /* If we're a Wayland compositor, then we don't grab the COW, since it
   * will map it. */
  if (!meta_is_wayland_compositor ())
    x11_display->composite_overlay_window = XCompositeGetOverlayWindow (xdisplay, xroot);

  /* Now that we've gotten taken a reference count on the COW, we
   * can close the helper that is holding on to it */
  if (is_restart)
    XSetSelectionOwner (xdisplay, atom_restart_helper, None, META_CURRENT_TIME);

  /* Handle creating a no_focus_window for this screen */
  x11_display->no_focus_window =
    meta_x11_display_create_offscreen_window (x11_display,
                                              xroot,
                                              FocusChangeMask |
                                              KeyPressMask | KeyReleaseMask);
  XMapWindow (xdisplay, x11_display->no_focus_window);
  /* Done with no_focus_window stuff */

  meta_x11_display_init_events (x11_display);

  set_wm_icon_size_hint (x11_display);
  set_supported_hint (x11_display);
  set_wm_check_hint (x11_display);
  set_desktop_viewport_hint (x11_display);
  set_desktop_geometry_hint (x11_display);

  x11_display->x11_stack = meta_x11_stack_new (x11_display);

  x11_display->keys_grabbed = FALSE;
  meta_x11_display_grab_keys (x11_display);

  meta_x11_display_update_workspace_layout (x11_display);

  if (meta_prefs_get_dynamic_workspaces ())
    {
      int num = 0;
      int n_items = 0;
      uint32_t *list = NULL;

      if (meta_prop_get_cardinal_list (x11_display,
                                       x11_display->xroot,
                                       x11_display->atom__NET_NUMBER_OF_DESKTOPS,
                                       &list, &n_items))
        {
          num = list[0];
          g_free (list);
        }

      if (num >
          meta_workspace_manager_get_n_workspaces (display->workspace_manager))
        {
          meta_workspace_manager_update_num_workspaces (
            display->workspace_manager, timestamp, num);
        }
    }

  g_signal_connect_object (display->workspace_manager, "active-workspace-changed",
                           G_CALLBACK (set_active_workspace_hint),
                           x11_display, 0);

  set_number_of_spaces_hint (display->workspace_manager, NULL, x11_display);

  g_signal_connect_object (display->workspace_manager, "notify::n-workspaces",
                           G_CALLBACK (set_number_of_spaces_hint),
                           x11_display, 0);

  set_showing_desktop_hint (display->workspace_manager, x11_display);

  g_signal_connect_object (display->workspace_manager, "showing-desktop-changed",
                           G_CALLBACK (set_showing_desktop_hint),
                           x11_display, 0);

  set_workspace_names (x11_display);

  meta_prefs_add_listener (prefs_changed_callback, x11_display);

  set_work_area_hint (display, x11_display);

  g_signal_connect_object (display, "workareas-changed",
                           G_CALLBACK (set_work_area_hint),
                           x11_display, 0);

  init_x11_bell (x11_display);

  meta_x11_startup_notification_init (x11_display);
  meta_x11_selection_init (x11_display);

#ifdef HAVE_X11
  if (!meta_is_wayland_compositor ())
    meta_dnd_init_xdnd (x11_display);
#endif

  sprintf (buf, "WM_S%d", number);

  wm_sn_atom = XInternAtom (xdisplay, buf, False);
  new_wm_sn_owner = take_manager_selection (x11_display,
                                            xroot,
                                            wm_sn_atom,
                                            timestamp,
                                            replace_current_wm);
  if (new_wm_sn_owner == None)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to acquire window manager ownership");

      g_object_run_dispose (G_OBJECT (x11_display));
      return NULL;
    }

  x11_display->wm_sn_selection_window = new_wm_sn_owner;
  x11_display->wm_sn_atom = wm_sn_atom;
  x11_display->wm_sn_timestamp = timestamp;

  g_snprintf (buf, sizeof (buf), "_NET_WM_CM_S%d", number);
  wm_cm_atom = XInternAtom (x11_display->xdisplay, buf, False);

  x11_display->wm_cm_selection_window =
    take_manager_selection (x11_display, xroot, wm_cm_atom, timestamp,
                            replace_current_wm);

  if (x11_display->wm_cm_selection_window == None)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to acquire compositor ownership");

      g_object_run_dispose (G_OBJECT (x11_display));
      return NULL;
    }

  init_event_masks (x11_display);

  meta_x11_display_init_frames_client (x11_display);

  return g_steal_pointer (&x11_display);
}

void
meta_x11_display_restore_active_workspace (MetaX11Display *x11_display)
{
  MetaDisplay *display;
  MetaWorkspace *current_workspace;
  uint32_t current_workspace_index = 0;
  guint32 timestamp;

  g_return_if_fail (META_IS_X11_DISPLAY (x11_display));

  display = x11_display->display;
  timestamp = x11_display->timestamp;

  /* Get current workspace */
  if (meta_prop_get_cardinal (x11_display,
                              x11_display->xroot,
                              x11_display->atom__NET_CURRENT_DESKTOP,
                              &current_workspace_index))
    {
      meta_verbose ("Read existing _NET_CURRENT_DESKTOP = %d",
                    (int) current_workspace_index);

      /* Switch to the _NET_CURRENT_DESKTOP workspace */
      current_workspace = meta_workspace_manager_get_workspace_by_index (display->workspace_manager,
                                                                         current_workspace_index);

      if (current_workspace != NULL)
        meta_workspace_activate (current_workspace, timestamp);
    }
  else
    {
      meta_verbose ("No _NET_CURRENT_DESKTOP present");
    }

  set_active_workspace_hint (display->workspace_manager, x11_display);
}

int
meta_x11_display_get_screen_number (MetaX11Display *x11_display)
{
  return DefaultScreen (x11_display->xdisplay);
}

MetaDisplay *
meta_x11_display_get_display (MetaX11Display *x11_display)
{
  return x11_display->display;
}

/**
 * meta_x11_display_get_xdisplay: (skip)
 * @x11_display: a #MetaX11Display
 *
 */
Display *
meta_x11_display_get_xdisplay (MetaX11Display *x11_display)
{
  return x11_display->xdisplay;
}

/**
 * meta_x11_display_get_xroot: (skip)
 * @x11_display: A #MetaX11Display
 *
 */
Window
meta_x11_display_get_xroot (MetaX11Display *x11_display)
{
  return x11_display->xroot;
}

int
meta_x11_display_get_damage_event_base (MetaX11Display *x11_display)
{
  return x11_display->damage_event_base;
}

Window
meta_x11_display_create_offscreen_window (MetaX11Display *x11_display,
                                          Window          parent,
                                          long            valuemask)
{
  XSetWindowAttributes attrs;

  /* we want to be override redirect because sometimes we
   * create a window on a screen we aren't managing.
   * (but on a display we are managing at least one screen for)
   */
  attrs.override_redirect = True;
  attrs.event_mask = valuemask;

  return XCreateWindow (x11_display->xdisplay,
                        parent,
                        -100, -100, 1, 1,
                        0,
                        CopyFromParent,
                        CopyFromParent,
                        (Visual *)CopyFromParent,
                        CWOverrideRedirect | CWEventMask,
                        &attrs);
}

static char *
get_screen_name (Display *xdisplay,
                 int      number)
{
  char *p;
  char *dname;
  char *scr;

  /* DisplayString gives us a sort of canonical display,
   * vs. the user-entered name from XDisplayName()
   */
  dname = g_strdup (DisplayString (xdisplay));

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

static void
meta_x11_display_reload_cursor (MetaX11Display *x11_display)
{
  Cursor xcursor;
  /* Set a cursor for X11 applications that don't specify their own */
  xcursor = XcursorLibraryLoadCursor (x11_display->xdisplay,
                                      meta_cursor_get_name (META_CURSOR_DEFAULT));

  XDefineCursor (x11_display->xdisplay, x11_display->xroot, xcursor);
  XFlush (x11_display->xdisplay);

  if (xcursor)
    XFreeCursor (x11_display->xdisplay, xcursor);
}

static void
set_cursor_theme (Display     *xdisplay,
                  MetaBackend *backend)
{
  MetaSettings *settings = meta_backend_get_settings (backend);
  int scale;

  scale = meta_settings_get_ui_scaling_factor (settings);
  XcursorSetTheme (xdisplay, meta_prefs_get_cursor_theme ());
  XcursorSetDefaultSize (xdisplay,
                         meta_prefs_get_cursor_size () * scale);
}

static void
meta_x11_display_remove_cursor_later (MetaX11Display *x11_display)
{
  if (x11_display->reload_x11_cursor_later)
    {
      MetaDisplay *display = x11_display->display;

      /* May happen during destruction */
      if (display->compositor)
        {
          MetaLaters *laters = meta_compositor_get_laters (display->compositor);
          meta_laters_remove (laters, x11_display->reload_x11_cursor_later);
        }

      x11_display->reload_x11_cursor_later = 0;
    }
}

static gboolean
reload_x11_cursor_later (gpointer user_data)
{
  MetaX11Display *x11_display = user_data;

  x11_display->reload_x11_cursor_later = 0;
  meta_x11_display_reload_cursor (x11_display);

  return G_SOURCE_REMOVE;
}

static void
schedule_reload_x11_cursor (MetaX11Display *x11_display)
{
  MetaDisplay *display = x11_display->display;
  MetaLaters *laters = meta_compositor_get_laters (display->compositor);

  if (x11_display->reload_x11_cursor_later)
    return;

  x11_display->reload_x11_cursor_later =
    meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                     reload_x11_cursor_later,
                     x11_display,
                     NULL);
}

static void
update_cursor_theme (MetaX11Display *x11_display)
{
  MetaBackend *backend = backend_from_x11_display (x11_display);

  set_cursor_theme (x11_display->xdisplay, backend);
  schedule_reload_x11_cursor (x11_display);

  if (META_IS_BACKEND_X11 (backend))
    {
      MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
      Display *xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

      set_cursor_theme (xdisplay, backend);
      meta_backend_x11_reload_cursor (backend_x11);
    }
}

MetaWindow *
meta_x11_display_lookup_x_window (MetaX11Display *x11_display,
                                  Window          xwindow)
{
  return g_hash_table_lookup (x11_display->xids, &xwindow);
}

void
meta_x11_display_register_x_window (MetaX11Display *x11_display,
                                    Window         *xwindowp,
                                    MetaWindow     *window)
{
  g_return_if_fail (g_hash_table_lookup (x11_display->xids, xwindowp) == NULL);

  g_hash_table_insert (x11_display->xids, xwindowp, window);
}

void
meta_x11_display_unregister_x_window (MetaX11Display *x11_display,
                                      Window          xwindow)
{
  g_return_if_fail (g_hash_table_lookup (x11_display->xids, &xwindow) != NULL);

  g_hash_table_remove (x11_display->xids, &xwindow);
}

MetaSyncCounter *
meta_x11_display_lookup_sync_alarm (MetaX11Display *x11_display,
                                    XSyncAlarm      alarm)
{
  return g_hash_table_lookup (x11_display->alarms, &alarm);
}

void
meta_x11_display_register_sync_alarm (MetaX11Display  *x11_display,
                                      XSyncAlarm      *alarmp,
                                      MetaSyncCounter *sync_counter)
{
  g_return_if_fail (g_hash_table_lookup (x11_display->alarms, alarmp) == NULL);

  g_hash_table_insert (x11_display->alarms, alarmp, sync_counter);
}

void
meta_x11_display_unregister_sync_alarm (MetaX11Display *x11_display,
                                        XSyncAlarm      alarm)
{
  g_return_if_fail (g_hash_table_lookup (x11_display->alarms, &alarm) != NULL);

  g_hash_table_remove (x11_display->alarms, &alarm);
}

MetaX11AlarmFilter *
meta_x11_display_add_alarm_filter (MetaX11Display  *x11_display,
                                   MetaAlarmFilter  filter,
                                   gpointer         user_data)
{
  MetaX11AlarmFilter *alarm_filter;

  if (!x11_display->alarm_filters)
    x11_display->alarm_filters = g_ptr_array_new_with_free_func (g_free);

  alarm_filter = g_new0 (MetaX11AlarmFilter, 1);
  alarm_filter->filter = filter;
  alarm_filter->user_data = user_data;
  g_ptr_array_add (x11_display->alarm_filters, alarm_filter);

  return alarm_filter;
}

void
meta_x11_display_remove_alarm_filter (MetaX11Display     *x11_display,
                                      MetaX11AlarmFilter *alarm_filter)
{
  g_ptr_array_remove (x11_display->alarm_filters, alarm_filter);
}

/* The guard window allows us to leave minimized windows mapped so
 * that compositor code may provide live previews of them.
 * Instead of being unmapped/withdrawn, they get pushed underneath
 * the guard window. We also select events on the guard window, which
 * should effectively be forwarded to events on the background actor,
 * providing that the scene graph is set up correctly.
 */
static Window
create_guard_window (MetaX11Display *x11_display)
{
  XSetWindowAttributes attributes;
  Window guard_window;
  gulong create_serial;
  int display_width, display_height;

  meta_display_get_size (x11_display->display,
                         &display_width,
                         &display_height);

  attributes.event_mask = NoEventMask;
  attributes.override_redirect = True;

  /* We have to call record_add() after we have the new window ID,
   * so save the serial for the CreateWindow request until then */
  create_serial = XNextRequest (x11_display->xdisplay);
  guard_window =
    XCreateWindow (x11_display->xdisplay,
                   x11_display->xroot,
                   0, /* x */
                   0, /* y */
                   display_width,
                   display_height,
                   0, /* border width */
                   0, /* depth */
                   InputOnly, /* class */
                   CopyFromParent, /* visual */
                   CWEventMask | CWOverrideRedirect,
                   &attributes);

  /* https://bugzilla.gnome.org/show_bug.cgi?id=710346 */
  XStoreName (x11_display->xdisplay, guard_window, "mutter guard window");

  {
    if (!meta_is_wayland_compositor ())
      {
        MetaBackendX11 *backend =
          META_BACKEND_X11 (backend_from_x11_display (x11_display));
        Display *backend_xdisplay = meta_backend_x11_get_xdisplay (backend);
        unsigned char mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
        XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

        XISetMask (mask.mask, XI_ButtonPress);
        XISetMask (mask.mask, XI_ButtonRelease);
        XISetMask (mask.mask, XI_Motion);

        /* Sync on the connection we created the window on to
         * make sure it's created before we select on it on the
         * backend connection. */
        XSync (x11_display->xdisplay, False);

        XISelectEvents (backend_xdisplay, guard_window, &mask, 1);
      }
  }

  meta_stack_tracker_record_add (x11_display->display->stack_tracker,
                                 guard_window,
                                 create_serial);

  meta_stack_tracker_lower (x11_display->display->stack_tracker,
                            guard_window);

  XMapWindow (x11_display->xdisplay, guard_window);
  return guard_window;
}

void
meta_x11_display_create_guard_window (MetaX11Display *x11_display)
{
  if (x11_display->guard_window == None)
    x11_display->guard_window = create_guard_window (x11_display);
}

static void
on_monitors_changed_internal (MetaMonitorManager *monitor_manager,
                              MetaX11Display     *x11_display)
{
  int display_width, display_height;

  meta_monitor_manager_get_screen_size (monitor_manager,
                                        &display_width,
                                        &display_height);

  set_desktop_geometry_hint (x11_display);

  /* Resize the guard window to fill the screen again. */
  if (x11_display->guard_window != None)
    {
      XWindowChanges changes;

      changes.x = 0;
      changes.y = 0;
      changes.width = display_width;
      changes.height = display_height;

      XConfigureWindow (x11_display->xdisplay,
                        x11_display->guard_window,
                        CWX | CWY | CWWidth | CWHeight,
                        &changes);
    }

  x11_display->has_xinerama_indices = FALSE;
}

static Bool
find_timestamp_predicate (Display  *xdisplay,
                          XEvent   *ev,
                          XPointer  arg)
{
  MetaX11Display *x11_display = (MetaX11Display *) arg;

  return (ev->type == PropertyNotify &&
          ev->xproperty.atom == x11_display->atom__MUTTER_TIMESTAMP_PING);
}

/* Get a timestamp, even if it means a roundtrip */
guint32
meta_x11_display_get_current_time_roundtrip (MetaX11Display *x11_display)
{
  guint32 timestamp;

  timestamp = meta_display_get_current_time (x11_display->display);
  if (timestamp == META_CURRENT_TIME)
    {
      XEvent property_event;

      XChangeProperty (x11_display->xdisplay,
                       x11_display->timestamp_pinging_window,
                       x11_display->atom__MUTTER_TIMESTAMP_PING,
                       XA_STRING, 8, PropModeAppend, NULL, 0);
      XIfEvent (x11_display->xdisplay,
                &property_event,
                find_timestamp_predicate,
                (XPointer) x11_display);
      timestamp = property_event.xproperty.time;
    }

  meta_display_sanity_check_timestamps (x11_display->display, timestamp);

  return timestamp;
}

/**
 * meta_x11_display_xwindow_is_a_no_focus_window:
 * @x11_display: A #MetaX11Display
 * @xwindow: An X11 window
 *
 * Returns: %TRUE iff window is one of mutter's internal "no focus" windows
 * which will have the focus when there is no actual client window focused.
 */
gboolean
meta_x11_display_xwindow_is_a_no_focus_window (MetaX11Display *x11_display,
                                               Window xwindow)
{
  return xwindow == x11_display->no_focus_window;
}

static void
meta_x11_display_update_active_window_hint (MetaX11Display *x11_display)
{
  MetaWindow *focus_window;
  gulong data[1];

  if (x11_display->display->closing)
    return; /* Leave old value for a replacement */

  focus_window = meta_x11_display_lookup_x_window (x11_display,
                                                   x11_display->focus_xwindow);

  if (focus_window)
    data[0] = meta_window_x11_get_xwindow (focus_window);
  else
    data[0] = None;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  XChangeProperty (x11_display->xdisplay,
                   x11_display->xroot,
                   x11_display->atom__NET_ACTIVE_WINDOW,
                   XA_WINDOW,
                   32, PropModeReplace, (guchar*) data, 1);
  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

void
meta_x11_display_update_focus_window (MetaX11Display *x11_display,
                                      Window          xwindow,
                                      gulong          serial,
                                      gboolean        focused_by_us)
{
  x11_display->focus_serial = serial;
  x11_display->focused_by_us = !!focused_by_us;

  if (x11_display->focus_xwindow == xwindow)
    return;

  meta_topic (META_DEBUG_FOCUS, "Updating X11 focus window from 0x%lx to 0x%lx",
              x11_display->focus_xwindow, xwindow);

  x11_display->focus_xwindow = xwindow;
  meta_x11_display_update_active_window_hint (x11_display);
}

static void
meta_x11_display_set_input_focus_internal (MetaX11Display *x11_display,
                                           Window          xwindow,
                                           uint32_t        timestamp)
{
  mtk_x11_error_trap_push (x11_display->xdisplay);

  /* In order for mutter to know that the focus request succeeded, we track
   * the serial of the "focus request" we made, but if we take the serial
   * of the XSetInputFocus request, then there's no way to determine the
   * difference between focus events as a result of the SetInputFocus and
   * focus events that other clients send around the same time. Ensure that
   * we know which is which by making two requests that the server will
   * process at the same time.
   */
  XGrabServer (x11_display->xdisplay);

  XSetInputFocus (x11_display->xdisplay,
                  xwindow,
                  RevertToPointerRoot,
                  timestamp);

  XChangeProperty (x11_display->xdisplay,
                   x11_display->timestamp_pinging_window,
                   x11_display->atom__MUTTER_FOCUS_SET,
                   XA_STRING, 8, PropModeAppend, NULL, 0);

  XUngrabServer (x11_display->xdisplay);
  XFlush (x11_display->xdisplay);

  mtk_x11_error_trap_pop (x11_display->xdisplay);
}

static void
meta_x11_display_set_input_focus (MetaX11Display *x11_display,
                                  MetaWindow     *window,
                                  uint32_t        timestamp)
{
  Window xwindow = x11_display->no_focus_window;
  gulong serial;
#ifdef HAVE_X11
  MetaDisplay *display = x11_display->display;
  ClutterStage *stage = CLUTTER_STAGE (meta_get_stage_for_display (display));
#endif

  if (window && META_IS_WINDOW_X11 (window))
    {
      /* For output-only windows, focus the frame.
       * This seems to result in the client window getting key events
       * though, so I don't know if it's icccm-compliant.
       *
       * Still, we have to do this or keynav breaks for these windows.
       */
      if (window->frame && !meta_window_is_focusable (window))
        xwindow = window->frame->xwindow;
      else
        xwindow = meta_window_x11_get_xwindow (window);
    }
#ifdef HAVE_X11
  else if (!meta_is_wayland_compositor () &&
           stage_has_focus_actor (x11_display))
    {
      /* If we expect keyboard focus (e.g. there is a focused actor, keep
       * focus on the stage window, otherwise focus the no focus window.
       */
      xwindow = meta_x11_get_stage_window (stage);
    }
#endif

  meta_topic (META_DEBUG_FOCUS, "Setting X11 input focus for window %s to 0x%lx",
              window ? window->desc : "none", xwindow);

  if (x11_display->is_server_focus)
    {
      serial = x11_display->server_focus_serial;
    }
  else
    {
      meta_x11_display_set_input_focus_internal (x11_display, xwindow, timestamp);
      mtk_x11_error_trap_push (x11_display->xdisplay);
      serial = XNextRequest (x11_display->xdisplay);
      mtk_x11_error_trap_pop (x11_display->xdisplay);
    }

  meta_x11_display_update_focus_window (x11_display, xwindow, serial,
                                        !x11_display->is_server_focus);

#ifdef HAVE_X11
  if (window && !meta_is_wayland_compositor ())
    clutter_stage_set_key_focus (stage, NULL);
#endif
}

static MetaX11DisplayLogicalMonitorData *
get_x11_display_logical_monitor_data (MetaLogicalMonitor *logical_monitor)
{
  return g_object_get_qdata (G_OBJECT (logical_monitor),
                             quark_x11_display_logical_monitor_data);
}

static MetaX11DisplayLogicalMonitorData *
ensure_x11_display_logical_monitor_data (MetaLogicalMonitor *logical_monitor)
{
  MetaX11DisplayLogicalMonitorData *data;

  data = get_x11_display_logical_monitor_data (logical_monitor);
  if (data)
    return data;

  data = g_new0 (MetaX11DisplayLogicalMonitorData, 1);
  g_object_set_qdata_full (G_OBJECT (logical_monitor),
                           quark_x11_display_logical_monitor_data,
                           data,
                           g_free);

  return data;
}

static void
meta_x11_display_ensure_xinerama_indices (MetaX11Display *x11_display)
{
  MetaBackend *backend = backend_from_x11_display (x11_display);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;
  XineramaScreenInfo *infos;
  int n_infos, j;

  if (x11_display->has_xinerama_indices)
    return;

  x11_display->has_xinerama_indices = TRUE;

  if (!XineramaIsActive (x11_display->xdisplay))
    return;

  infos = XineramaQueryScreens (x11_display->xdisplay,
                                &n_infos);
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
              MetaX11DisplayLogicalMonitorData *logical_monitor_data;

              logical_monitor_data =
                ensure_x11_display_logical_monitor_data (logical_monitor);
              logical_monitor_data->xinerama_index = j;
            }
        }
    }

  meta_XFree (infos);
}

int
meta_x11_display_logical_monitor_to_xinerama_index (MetaX11Display     *x11_display,
                                                    MetaLogicalMonitor *logical_monitor)
{
  MetaX11DisplayLogicalMonitorData *logical_monitor_data;

  g_return_val_if_fail (logical_monitor, -1);

  meta_x11_display_ensure_xinerama_indices (x11_display);

  logical_monitor_data = get_x11_display_logical_monitor_data (logical_monitor);

  return logical_monitor_data->xinerama_index;
}

MetaLogicalMonitor *
meta_x11_display_xinerama_index_to_logical_monitor (MetaX11Display *x11_display,
                                                    int             xinerama_index)
{
  MetaBackend *backend = backend_from_x11_display (x11_display);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors, *l;

  meta_x11_display_ensure_xinerama_indices (x11_display);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaX11DisplayLogicalMonitorData *logical_monitor_data;

      logical_monitor_data =
        ensure_x11_display_logical_monitor_data (logical_monitor);

      if (logical_monitor_data->xinerama_index == xinerama_index)
        return logical_monitor;
    }

  return NULL;
}

void
meta_x11_display_update_workspace_names (MetaX11Display *x11_display)
{
  char **names;
  int n_names;
  int i;

  /* this updates names in prefs when the root window property changes,
   * iff the new property contents don't match what's already in prefs
   */

  names = NULL;
  n_names = 0;
  if (!meta_prop_get_utf8_list (x11_display,
                                x11_display->xroot,
                                x11_display->atom__NET_DESKTOP_NAMES,
                                &names, &n_names))
    {
      meta_verbose ("Failed to get workspace names from root window");
      return;
    }

  i = 0;
  while (i < n_names)
    {
      meta_topic (META_DEBUG_PREFS,
                  "Setting workspace %d name to \"%s\" due to _NET_DESKTOP_NAMES change",
                  i, names[i] ? names[i] : "null");
      meta_prefs_change_workspace_name (i, names[i]);

      ++i;
    }

  g_strfreev (names);
}

#define _NET_WM_ORIENTATION_HORZ 0
#define _NET_WM_ORIENTATION_VERT 1

#define _NET_WM_TOPLEFT     0
#define _NET_WM_TOPRIGHT    1
#define _NET_WM_BOTTOMRIGHT 2
#define _NET_WM_BOTTOMLEFT  3

void
meta_x11_display_update_workspace_layout (MetaX11Display *x11_display)
{
  MetaWorkspaceManager *workspace_manager = x11_display->display->workspace_manager;
  gboolean vertical_layout = FALSE;
  int n_rows = 1;
  int n_columns = -1;
  MetaDisplayCorner starting_corner = META_DISPLAY_TOPLEFT;
  uint32_t *list;
  int n_items;

  if (workspace_manager->workspace_layout_overridden)
    return;

  list = NULL;
  n_items = 0;

  if (meta_prop_get_cardinal_list (x11_display,
                                   x11_display->xroot,
                                   x11_display->atom__NET_DESKTOP_LAYOUT,
                                   &list, &n_items))
    {
      if (n_items == 3 || n_items == 4)
        {
          int cols, rows;

          switch (list[0])
            {
            case _NET_WM_ORIENTATION_HORZ:
              vertical_layout = FALSE;
              break;
            case _NET_WM_ORIENTATION_VERT:
              vertical_layout = TRUE;
              break;
            default:
              meta_warning ("Someone set a weird orientation in _NET_DESKTOP_LAYOUT");
              break;
            }

          cols = list[1];
          rows = list[2];

          if (rows <= 0 && cols <= 0)
            {
              meta_warning ("Columns = %d rows = %d in _NET_DESKTOP_LAYOUT makes no sense", rows, cols);
            }
          else
            {
              if (rows > 0)
                n_rows = rows;
              else
                n_rows = -1;

              if (cols > 0)
                n_columns = cols;
              else
                n_columns = -1;
            }

          if (n_items == 4)
            {
              switch (list[3])
                {
                case _NET_WM_TOPLEFT:
                  starting_corner = META_DISPLAY_TOPLEFT;
                  break;
                case _NET_WM_TOPRIGHT:
                  starting_corner = META_DISPLAY_TOPRIGHT;
                  break;
                case _NET_WM_BOTTOMRIGHT:
                  starting_corner = META_DISPLAY_BOTTOMRIGHT;
                  break;
                case _NET_WM_BOTTOMLEFT:
                  starting_corner = META_DISPLAY_BOTTOMLEFT;
                  break;
                default:
                  meta_warning ("Someone set a weird starting corner in _NET_DESKTOP_LAYOUT");
                  break;
                }
            }
        }
      else
        {
          meta_warning ("Someone set _NET_DESKTOP_LAYOUT to %d integers instead of 4 "
                        "(3 is accepted for backwards compat)", n_items);
        }

      g_free (list);

      meta_workspace_manager_update_workspace_layout (workspace_manager,
                                                      starting_corner,
                                                      vertical_layout,
                                                      n_rows,
                                                      n_columns);
    }
}

static void
prefs_changed_callback (MetaPreference pref,
                        void          *data)
{
  MetaX11Display *x11_display = data;

  if (pref == META_PREF_WORKSPACE_NAMES)
    {
      set_workspace_names (x11_display);
    }
}

void
meta_x11_display_set_stage_input_region (MetaX11Display *x11_display,
                                         XserverRegion   region)
{
  Display *xdisplay = x11_display->xdisplay;
  MetaBackend *backend = backend_from_x11_display (x11_display);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  Window stage_xwindow;

  g_return_if_fail (!meta_is_wayland_compositor ());

  stage_xwindow = meta_x11_get_stage_window (stage);
  XFixesSetWindowShapeRegion (xdisplay, stage_xwindow,
                              ShapeInput, 0, 0, region);
  XFixesSetWindowShapeRegion (xdisplay,
                              x11_display->composite_overlay_window,
                              ShapeInput, 0, 0, region);
}

void
meta_x11_display_clear_stage_input_region (MetaX11Display *x11_display)
{
  if (x11_display->empty_region == None)
    {
      x11_display->empty_region = XFixesCreateRegion (x11_display->xdisplay,
                                                      NULL, 0);
    }

  meta_x11_display_set_stage_input_region (x11_display,
                                           x11_display->empty_region);
}

/**
 * meta_x11_display_add_event_func: (skip):
 **/
unsigned int
meta_x11_display_add_event_func (MetaX11Display          *x11_display,
                                 MetaX11DisplayEventFunc  event_func,
                                 gpointer                 user_data,
                                 GDestroyNotify           destroy_notify)
{
  MetaX11EventFilter *filter;
  static unsigned int id = 0;

  filter = g_new0 (MetaX11EventFilter, 1);
  filter->func = event_func;
  filter->user_data = user_data;
  filter->destroy_notify = destroy_notify;
  filter->id = ++id;

  x11_display->event_funcs = g_list_prepend (x11_display->event_funcs, filter);

  return filter->id;
}

/**
 * meta_x11_display_remove_event_func: (skip):
 **/
void
meta_x11_display_remove_event_func (MetaX11Display *x11_display,
                                    unsigned int    id)
{
  MetaX11EventFilter *filter;
  GList *l;

  for (l = x11_display->event_funcs; l; l = l->next)
    {
      filter = l->data;

      if (filter->id != id)
        continue;

      x11_display->event_funcs =
        g_list_delete_link (x11_display->event_funcs, l);
      meta_x11_event_filter_free (filter);
      break;
    }
}

void
meta_x11_display_run_event_funcs (MetaX11Display *x11_display,
                                  XEvent         *xevent)
{
  MetaX11EventFilter *filter;
  GList *next, *l = x11_display->event_funcs;

  while (l)
    {
      filter = l->data;
      next = l->next;

      filter->func (x11_display, xevent, filter->user_data);
      l = next;
    }
}

void
meta_x11_display_redirect_windows (MetaX11Display *x11_display,
                                   MetaDisplay    *display)
{
  MetaContext *context = meta_display_get_context (display);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  Window xroot = meta_x11_display_get_xroot (x11_display);
  int screen_number = meta_x11_display_get_screen_number (x11_display);
  guint n_retries;
  guint max_retries;

  if (meta_context_is_replacing (context))
    max_retries = 5;
  else
    max_retries = 1;

  n_retries = 0;

  /* Some compositors (like old versions of Mutter) might not properly unredirect
   * subwindows before destroying the WM selection window; so we wait a while
   * for such a compositor to exit before giving up.
   */
  while (TRUE)
    {
      mtk_x11_error_trap_push (x11_display->xdisplay);
      XCompositeRedirectSubwindows (xdisplay, xroot, CompositeRedirectManual);
      XSync (xdisplay, FALSE);

      if (!mtk_x11_error_trap_pop_with_return (x11_display->xdisplay))
        break;

      if (n_retries == max_retries)
        {
          /* This probably means that a non-WM compositor like xcompmgr is running;
           * we have no way to get it to exit */
          meta_fatal (_("Another compositing manager is already running on screen %i on display “%s”."),
                      screen_number, x11_display->name);
        }

      n_retries++;
      g_usleep (G_USEC_PER_SEC);
    }
}

Window
meta_x11_display_lookup_xwindow (MetaX11Display *x11_display,
                                 MetaWindow     *window)
{
  g_return_val_if_fail (META_IS_X11_DISPLAY (x11_display), None);
  g_return_val_if_fail (META_IS_WINDOW (window), None);

  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11)
    return meta_window_x11_get_xwindow (window);

  return None;
}
