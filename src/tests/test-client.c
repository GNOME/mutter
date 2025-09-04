/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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

#include "config.h"

#include <gio/gunixinputstream.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/sync.h>

#include "core/events.h"

const char *client_id = "0";
static gboolean wayland;
static gboolean dont_exit_on_eof;
static gboolean verbose;
GHashTable *windows;
GQuark event_source_quark;
GQuark event_handlers_quark;
GQuark can_take_focus_quark;
gboolean sync_after_lines = -1;
gboolean is_sleeping;

typedef void (*XEventHandler) (GtkWidget *window, XEvent *event);

static void read_next_line (GDataInputStream *in);

static void
window_export_handle_cb (GdkWindow  *window,
                         const char *handle_str,
                         gpointer    user_data)
{
  GdkWindow *gdk_window = gtk_widget_get_window (GTK_WIDGET (user_data));

  if (!gdk_wayland_window_set_transient_for_exported (gdk_window,
                                                      (gchar *) handle_str))
    g_print ("Fail to set transient_for exported window handle %s\n", handle_str);
  gdk_window_set_modal_hint (gdk_window, TRUE);
}

static GtkWidget *
lookup_window (const char *window_id)
{
  GtkWidget *window = g_hash_table_lookup (windows, window_id);
  if (!window)
    g_print ("Window %s doesn't exist\n", window_id);

  return window;
}

typedef struct {
  GSource base;
  GSource **self_ref;
  GPollFD event_poll_fd;
  Display *xdisplay;
} XClientEventSource;

static gboolean
x_event_source_prepare (GSource *source,
                        int     *timeout)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *timeout = -1;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_check (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  return XPending (x_source->xdisplay);
}

static gboolean
x_event_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  while (XPending (x_source->xdisplay))
    {
      GHashTableIter iter;
      XEvent event;
      gpointer value;

      XNextEvent (x_source->xdisplay, &event);

      g_hash_table_iter_init (&iter, windows);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          GList *l;
          GtkWidget *window = value;
          GList *handlers =
            g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

          for (l = handlers; l; l = l->next)
            {
              XEventHandler handler = l->data;
              handler (window, &event);
            }
        }
    }

  return TRUE;
}

static void
x_event_source_finalize (GSource *source)
{
  XClientEventSource *x_source = (XClientEventSource *) source;

  *x_source->self_ref = NULL;
}

static GSourceFuncs x_event_funcs = {
  x_event_source_prepare,
  x_event_source_check,
  x_event_source_dispatch,
  x_event_source_finalize,
};

static GSource*
ensure_xsource_handler (GdkDisplay *gdkdisplay)
{
  static GSource *source = NULL;
  Display *xdisplay = GDK_DISPLAY_XDISPLAY (gdkdisplay);
  XClientEventSource *x_source;

  if (source)
    return g_source_ref (source);

  source = g_source_new (&x_event_funcs, sizeof (XClientEventSource));
  x_source = (XClientEventSource *) source;
  x_source->self_ref = &source;
  x_source->xdisplay = xdisplay;
  x_source->event_poll_fd.fd = ConnectionNumber (xdisplay);
  x_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x_source->event_poll_fd);

  g_source_set_priority (source, META_PRIORITY_EVENTS - 1);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return source;
}

static gboolean
window_has_x11_event_handler (GtkWidget     *window,
                              XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_val_if_fail (handler, FALSE);
  g_return_val_if_fail (!wayland, FALSE);

  return g_list_find (handlers, handler) != NULL;
}

static void
unref_and_maybe_destroy_gsource (GSource *source)
{
  g_source_unref (source);

  if (source->ref_count == 1)
    g_source_destroy (source);
}

static void
window_add_x11_event_handler (GtkWidget     *window,
                              XEventHandler  handler)
{
  GSource *source;
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (!window_has_x11_event_handler (window, handler));

  source = ensure_xsource_handler (gtk_widget_get_display (window));
  g_object_set_qdata_full (G_OBJECT (window), event_source_quark, source,
                           (GDestroyNotify) unref_and_maybe_destroy_gsource);

  handlers = g_list_append (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
window_remove_x11_event_handler (GtkWidget     *window,
                                 XEventHandler  handler)
{
  GList *handlers =
    g_object_get_qdata (G_OBJECT (window), event_handlers_quark);

  g_return_if_fail (window_has_x11_event_handler (window, handler));

  g_object_set_qdata (G_OBJECT (window), event_source_quark, NULL);

  handlers = g_list_remove (handlers, handler);
  g_object_set_qdata (G_OBJECT (window), event_handlers_quark, handlers);
}

static void
handle_take_focus (GtkWidget *window,
                   XEvent    *xevent)
{
  GdkWindow *gdkwindow = gtk_widget_get_window (window);
  GdkDisplay *display = gtk_widget_get_display (window);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  Atom wm_protocols =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_PROTOCOLS");
  Atom wm_take_focus =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");
  G_GNUC_END_IGNORE_DEPRECATIONS

  if (xevent->xany.type != ClientMessage ||
      xevent->xany.window != GDK_WINDOW_XID (gdkwindow))
    return;

  if (xevent->xclient.message_type == wm_protocols &&
      xevent->xclient.data.l[0] == wm_take_focus)
    {
      XSetInputFocus (xevent->xany.display,
                      GDK_WINDOW_XID (gdkwindow),
                      RevertToParent,
                      xevent->xclient.data.l[1]);
    }
}

static int
calculate_titlebar_height (GtkWindow *window)
{
  GtkWidget *titlebar;
  GdkWindow *gdk_window;

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  if (gdk_window_get_state (gdk_window) & GDK_WINDOW_STATE_FULLSCREEN)
    return 0;

  titlebar = gtk_window_get_titlebar (window);
  if (!titlebar)
    return 0;

  return gtk_widget_get_allocated_height (titlebar);
}

static void
text_get_func (GtkClipboard     *clipboard,
               GtkSelectionData *selection_data,
               unsigned int      info,
               gpointer          data)
{
  gtk_selection_data_set_text (selection_data, data, -1);
}

static void
text_clear_func (GtkClipboard *clipboard,
                 gpointer      data)
{
  g_free (data);
}

static void
calculate_anchors (const char *position,
                   GdkGravity *rect_anchor,
                   GdkGravity *window_anchor)
{
  if (g_strcmp0 (position, "center") == 0)
    {
      *rect_anchor = GDK_GRAVITY_CENTER;
      *window_anchor = GDK_GRAVITY_CENTER;
    }
  else if (g_strcmp0 (position, "top") == 0)
    {
      *rect_anchor = GDK_GRAVITY_NORTH;
      *window_anchor = GDK_GRAVITY_SOUTH;
    }
  else if (g_strcmp0 (position, "bottom") == 0)
    {
      *rect_anchor = GDK_GRAVITY_SOUTH;
      *window_anchor = GDK_GRAVITY_NORTH;
    }
  else if (g_strcmp0 (position, "left") == 0)
    {
      *rect_anchor = GDK_GRAVITY_WEST;
      *window_anchor = GDK_GRAVITY_EAST;
    }
  else if (g_strcmp0 (position, "right") == 0)
    {
      *rect_anchor = GDK_GRAVITY_EAST;
      *window_anchor = GDK_GRAVITY_WEST;
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
prepare_popup_window (GdkSeat   *seat,
                      GdkWindow *window,
                      gpointer   user_data)
{
  GtkWidget *popup = user_data;

  gtk_widget_show (popup);
}

typedef enum _PopupAtFlags
{
  POPUP_AT_FLAG_NONE = 0,
  POPUP_AT_FLAG_GRAB = 1 << 0,
  POPUP_AT_FLAG_RESIZE = 1 << 1,
  POPUP_AT_FLAG_FLIP = 1 << 2,
} PopupAtFlags;

static void
popup_at (GtkWidget    *parent,
          const char   *popup_id,
          const char   *position,
          int           width,
          int           height,
          PopupAtFlags  flags)
{
  GtkWidget *popup;
  g_autofree char *title;
  GdkWindow *gdk_window;
  GdkRectangle window_rect;
  GdkGravity rect_anchor, window_anchor;
  GdkAnchorHints anchor_hints = 0;

  popup = g_object_new (GTK_TYPE_WINDOW,
                        "type", GTK_WINDOW_POPUP,
                        "type-hint", GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                        NULL);

  title = g_strdup_printf ("test/%s/%s", client_id, popup_id);
  gtk_window_set_transient_for (GTK_WINDOW (popup), GTK_WINDOW (parent));
  gtk_window_set_title (GTK_WINDOW (popup), title);
  g_hash_table_insert (windows, g_strdup (popup_id), popup);

  gtk_window_resize (GTK_WINDOW (popup), width, height);

  gtk_widget_realize (popup);
  gdk_window = gtk_widget_get_window (popup);

  gtk_widget_get_allocation (popup, &window_rect);

  calculate_anchors (position, &rect_anchor, &window_anchor);

  if (flags & POPUP_AT_FLAG_RESIZE)
    anchor_hints |= GDK_ANCHOR_RESIZE;
  if (flags & POPUP_AT_FLAG_FLIP)
    anchor_hints |= GDK_ANCHOR_FLIP;

  gdk_window_move_to_rect (gdk_window,
                           &window_rect,
                           rect_anchor,
                           window_anchor,
                           anchor_hints,
                           0, 0);

  if (flags & POPUP_AT_FLAG_GRAB)
    {
      GdkSeat *seat =
        gdk_display_get_default_seat (gtk_widget_get_display (popup));
      GdkGrabStatus grab_status;

      grab_status = gdk_seat_grab (seat, gdk_window,
                                   (GDK_SEAT_CAPABILITY_POINTER |
                                    GDK_SEAT_CAPABILITY_TABLET_STYLUS |
                                    GDK_SEAT_CAPABILITY_KEYBOARD),
                                   TRUE,
                                   NULL, NULL,
                                   prepare_popup_window, popup);
      g_assert_cmpint (grab_status, ==, GDK_GRAB_SUCCESS);
    }
  else
    {
      gtk_widget_show (popup);
    }
}

static int
find_monitor_from_connector (const char *connector)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkScreen *screen = gdk_screen_get_default ();
  int i;

  for (i = 0; i < gdk_display_get_n_monitors (display); i++)
    {
      g_autofree char *monitor_connector = NULL;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      monitor_connector = gdk_screen_get_monitor_plug_name (screen, i);
      G_GNUC_END_IGNORE_DEPRECATIONS
      if (g_strcmp0 (connector, monitor_connector) == 0)
        return i;
    }

  return -1;
}

static void
sleep_timeout_cb (gpointer user_data)
{
  GDataInputStream *in = G_DATA_INPUT_STREAM (user_data);

  is_sleeping = FALSE;
  read_next_line (in);
}

static void
process_line (const char       *line,
              GDataInputStream *in)
{
  GdkDisplay *display = gdk_display_get_default ();
  GError *error = NULL;
  int argc;
  char **argv;
  static int line_count = 0;

  line_count++;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    {
      g_print ("error parsing command: %s\n", error->message);
      g_error_free (error);
      return;
    }

  if (argc < 1)
    {
      g_print ("Empty command\n");
      goto out;
    }

  if (verbose)
    g_printerr ("%d %s\n", line_count, line);

  if (strcmp (argv[0], "create") == 0)
    {
      int i;

      if (argc  < 2)
        {
          g_print ("usage: create <id> [override|csd]\n");
          goto out;
        }

      if (g_hash_table_lookup (windows, argv[1]))
        {
          g_print ("window %s already exists\n", argv[1]);
          goto out;
        }

      gboolean override = FALSE;
      gboolean csd = FALSE;
      for (i = 2; i < argc; i++)
        {
          if (strcmp (argv[i], "override") == 0)
            override = TRUE;
          if (strcmp (argv[i], "csd") == 0)
            csd = TRUE;
        }

      if (override && csd)
        {
          g_print ("override and csd keywords are exclusive\n");
          goto out;
        }

      GtkWidget *window = gtk_window_new (override ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);
      g_hash_table_insert (windows, g_strdup (argv[1]), window);

      if (csd)
        {
          GtkWidget *headerbar = gtk_header_bar_new ();
          gtk_window_set_titlebar (GTK_WINDOW (window), headerbar);
          gtk_widget_show (headerbar);
        }

      gtk_window_set_default_size (GTK_WINDOW (window), 100, 100);

      gchar *title = g_strdup_printf ("test/%s/%s", client_id, argv[1]);
      gtk_window_set_title (GTK_WINDOW (window), title);
      g_free (title);

      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (TRUE));

      gtk_widget_realize (window);

      if (!wayland)
        {
          /* The cairo xlib backend creates a window when initialized, which
           * confuses our testing if it happens asynchronously the first
           * time a window is painted. By creating an Xlib surface and
           * destroying it, we force initialization at a more predictable time.
           */
          GdkWindow *window_gdk = gtk_widget_get_window (window);
          cairo_surface_t *surface = gdk_window_create_similar_surface (window_gdk,
                                                                        CAIRO_CONTENT_COLOR,
                                                                        1, 1);
          cairo_surface_destroy (surface);
        }

    }
  else if (strcmp (argv[0], "set_parent") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent <window-id> <parent-id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      GtkWidget *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s\n", argv[2]);
          goto out;
        }

      gtk_window_set_transient_for (GTK_WINDOW (window),
                                    GTK_WINDOW (parent_window));
    }
  else if (strcmp (argv[0], "set_parent_exported") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: set_parent_exported <window-id> <parent-id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      GtkWidget *parent_window = lookup_window (argv[2]);
      if (!parent_window)
        {
          g_print ("unknown parent window %s\n", argv[2]);
          goto out;
        }

      GdkWindow *parent_gdk_window = gtk_widget_get_window (parent_window);
      if (!gdk_wayland_window_export_handle (parent_gdk_window,
                                             window_export_handle_cb,
                                             window,
                                             NULL))
        g_print ("Fail to export handle for window id %s\n", argv[2]);
    }
  else if (strcmp (argv[0], "accept_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (!wayland &&
          window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to use %s for windows accepting take focus\n",
                   argv[1]);
          goto out;
        }

      gboolean enabled = g_ascii_strcasecmp (argv[2], "true") == 0;
      gtk_window_set_accept_focus (GTK_WINDOW (window), enabled);
    }
  else if (strcmp (argv[0], "can_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland\n", argv[0]);
          goto out;
        }

      if (window_has_x11_event_handler (window, handle_take_focus))
        {
          g_print ("Impossible to change %s for windows accepting take focus\n",
                   argv[1]);
          goto out;
        }

      GdkWindow *gdkwindow = gtk_widget_get_window (window);
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      Display *xdisplay = gdk_x11_display_get_xdisplay (display);
      Window xwindow = GDK_WINDOW_XID (gdkwindow);
      Atom wm_take_focus = gdk_x11_get_xatom_by_name_for_display (display, "WM_TAKE_FOCUS");
      G_GNUC_END_IGNORE_DEPRECATIONS
      gboolean add = g_ascii_strcasecmp(argv[2], "true") == 0;
      Atom *protocols = NULL;
      Atom *new_protocols;
      int n_protocols = 0;
      int i, n = 0;

      gdk_display_sync (display);
      XGetWMProtocols (xdisplay, xwindow, &protocols, &n_protocols);
      new_protocols = g_new0 (Atom, n_protocols + (add ? 1 : 0));

      for (i = 0; i < n_protocols; ++i)
        {
          if (protocols[i] != wm_take_focus)
            new_protocols[n++] = protocols[i];
        }

      if (add)
        new_protocols[n++] = wm_take_focus;

      XSetWMProtocols (xdisplay, xwindow, new_protocols, n);
      g_object_set_qdata (G_OBJECT (window), can_take_focus_quark,
                          GUINT_TO_POINTER (add));

      XFree (new_protocols);
      XFree (protocols);
    }
  else if (strcmp (argv[0], "accept_take_focus") == 0)
    {
      if (argc != 3)
        {
          g_print ("usage: %s <window-id> [true|false]\n", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        {
          g_print ("unknown window %s\n", argv[1]);
          goto out;
        }

      if (wayland)
        {
          g_print ("%s not supported under wayland\n", argv[0]);
          goto out;
        }

      if (gtk_window_get_accept_focus (GTK_WINDOW (window)))
        {
          g_print ("%s not supported for input windows\n", argv[0]);
          goto out;
        }

      if (!g_object_get_qdata (G_OBJECT (window), can_take_focus_quark))
        {
          g_print ("%s not supported for windows with no WM_TAKE_FOCUS set\n",
                   argv[0]);
          goto out;
        }

      if (g_ascii_strcasecmp (argv[2], "true") == 0)
        window_add_x11_event_handler (window, handle_take_focus);
      else
        window_remove_x11_event_handler (window, handle_take_focus);
    }
  else if (strcmp (argv[0], "show") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: show <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_widget_show (window);
      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "hide") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: hide <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_widget_hide (window);
    }
  else if (strcmp (argv[0], "activate") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: activate <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_present (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "resize") == 0 ||
           strcmp (argv[0], "resize_ignore_titlebar") == 0)
    {
      int titlebar_height;

      if (argc != 4)
        {
          g_print ("usage: %s <id> <width> <height>\n", argv[0]);
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      int width = atoi (argv[2]);
      int height = atoi (argv[3]);

      if (strcmp (argv[0], "resize_ignore_titlebar") == 0)
        titlebar_height = 0;
      else
        titlebar_height = calculate_titlebar_height (GTK_WINDOW (window));

      gtk_window_resize (GTK_WINDOW (window),
                         width,
                         height - titlebar_height);
    }
  else if (strcmp (argv[0], "x11_geometry") == 0)
    {
      GtkWidget *window;

      window = lookup_window (argv[1]);
      if (!window)
        goto out;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_window_parse_geometry (GTK_WINDOW (window), argv[2]);
      G_GNUC_END_IGNORE_DEPRECATIONS
    }
  else if (strcmp (argv[0], "raise") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: raise <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_raise (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "lower") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: lower <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_lower (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "destroy") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: destroy <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      g_hash_table_remove (windows, argv[1]);
      gtk_widget_destroy (window);
    }
  else if (strcmp (argv[0], "destroy_all") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: destroy_all\n");
          goto out;
        }

      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, windows);
      while (g_hash_table_iter_next (&iter, &key, &value))
        gtk_widget_destroy (value);

      g_hash_table_remove_all (windows);
    }
  else if (strcmp (argv[0], "sync") == 0)
    {
      if (argc != 1)
        {
          g_print ("usage: sync\n");
          goto out;
        }

      gdk_display_sync (gdk_display_get_default ());
    }
  else if (strcmp (argv[0], "set_counter") == 0)
    {
      XSyncCounter counter;
      int value;

      if (argc != 3)
        {
          g_print ("usage: set_counter <counter> <value>\n");
          goto out;
        }

      if (wayland)
        {
          g_print ("usage: set_counter can only be used for X11\n");
          goto out;
        }

      counter = strtoul(argv[1], NULL, 10);
      value = atoi(argv[2]);
      XSyncValue sync_value;
      XSyncIntToValue (&sync_value, value);

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      XSyncSetCounter (gdk_x11_display_get_xdisplay (gdk_display_get_default ()),
                       counter, sync_value);
      G_GNUC_END_IGNORE_DEPRECATIONS
    }
  else if (strcmp (argv[0], "minimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: minimize <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_iconify (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "unminimize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unminimize <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_deiconify (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "maximize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: maximize <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_maximize (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "unmaximize") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unmaximize <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_unmaximize (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "set_modal") == 0)
    {
      GtkWidget *window;

      if (argc != 2)
        {
          g_print ("usage: set_modal <id>\n");
          goto out;
        }

      window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_set_modal (GTK_WINDOW (window), TRUE);
    }
  else if (strcmp (argv[0], "unset_modal") == 0)
    {
      GtkWidget *window;

      if (argc != 2)
        {
          g_print ("usage: unset_modal <id>\n");
          goto out;
        }

      window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_set_modal (GTK_WINDOW (window), FALSE);
    }
  else if (strcmp (argv[0], "fullscreen") == 0)
    {
      if (argc != 2 && argc != 3)
        {
          g_print ("usage: fullscreen <id> [<connector>]\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      if (argc == 3)
        {
          GdkScreen *screen = gdk_screen_get_default ();
          int monitor;

          monitor = find_monitor_from_connector (argv[2]);
          if (monitor == -1)
            {
              g_printerr ("Unknown monitor %s\n", argv[2]);
              goto out;
            }

          gtk_window_fullscreen_on_monitor (GTK_WINDOW (window),
                                            screen,
                                            monitor);
        }
      else
        {
          gtk_window_fullscreen (GTK_WINDOW (window));
        }
    }
  else if (strcmp (argv[0], "unfullscreen") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: unfullscreen <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_unfullscreen (GTK_WINDOW (window));
    }
  else if (strcmp (argv[0], "freeze") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: freeze <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_freeze_updates (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "thaw") == 0)
    {
      if (argc != 2)
        {
          g_print ("usage: thaw <id>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gdk_window_thaw_updates (gtk_widget_get_window (window));
    }
  else if (strcmp (argv[0], "assert_size") == 0)
    {
      int expected_width;
      int expected_height;
      int width;
      int height;

      if (argc != 4)
        {
          g_print ("usage: assert_size <id> <width> <height>\n");
          goto out;
        }

      GtkWidget *window = lookup_window (argv[1]);
      if (!window)
        goto out;

      gtk_window_get_size (GTK_WINDOW (window), &width, &height);
      height += calculate_titlebar_height (GTK_WINDOW (window));

      expected_width = atoi (argv[2]);
      expected_height = atoi (argv[3]);
      if (expected_width != width || expected_height != height)
        {
          g_print ("Expected size %dx%d didn't match actual size %dx%d\n",
                   expected_width, expected_height,
                   width, height);
          goto out;
        }
    }
  else if (strcmp (argv[0], "assert_primary_monitor") == 0)
    {
      GdkWindow *root_window = gdk_screen_get_root_window ((gdk_screen_get_default ()));
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      Display *xdisplay = gdk_x11_display_get_xdisplay (display);
      Window root_xwindow = gdk_x11_window_get_xid (root_window);
      G_GNUC_END_IGNORE_DEPRECATIONS
      XRRScreenResources *resources;
      RROutput primary_output;
      XRROutputInfo *output_info;
      char *expected_name;

      if (wayland)
        {
          g_print ("Can only assert primary monitor on X11\n");
          goto out;
        }

      if (argc != 2)
        {
          g_print ("usage: %s <monitor-name>\n", argv[0]);
          goto out;
        }

      expected_name = argv[1];

      gdk_display_sync (gdk_display_get_default ());

      resources = XRRGetScreenResourcesCurrent (xdisplay, root_xwindow);
      if (!resources)
        {
          g_print ("Failed to retrieve XRANDR resources\n");
          goto out;
        }

      primary_output = XRRGetOutputPrimary (xdisplay, root_xwindow);
      if (!primary_output)
        {
          if (g_strcmp0 (expected_name, "(none)") != 0)
            {
              g_print ("Failed to retrieve primary XRANDR output (expected %s)\n", expected_name);
              goto out;
            }
        }
      else
        {
          output_info = XRRGetOutputInfo (xdisplay, resources, primary_output);
          if (!output_info)
            {
              g_print ("Failed to retrieve primary XRANDR output info\n");
              goto out;
            }

          if (g_strcmp0 (expected_name, output_info->name) != 0)
            {
              XRRFreeOutputInfo (output_info);
              g_print ("XRANDR output %s primary, expected %s\n",
                       output_info->name, expected_name);
              goto out;
            }
          XRRFreeOutputInfo (output_info);
        }
    }
  else if (strcmp (argv[0], "stop_after_next") == 0)
    {
      if (sync_after_lines != -1)
        {
          g_print ("Can't invoke 'stop_after_next' while already stopped");
          goto out;
        }

      sync_after_lines = 1;
    }
  else if (strcmp (argv[0], "continue") == 0)
    {
      if (sync_after_lines != 0)
        {
          g_print ("Can only invoke 'continue' while stopped");
          goto out;
        }

      sync_after_lines = -1;
    }
  else if (strcmp (argv[0], "clipboard-set") == 0)
    {
      GtkClipboard *clipboard;
      GdkAtom atom;
      GtkTargetList *target_list;
      GtkTargetEntry *targets;
      int n_targets;

      if (argc != 3)
        {
          g_print ("usage: clipboard-set <mimetype> <text>\n");
          goto out;
        }

      clipboard = gtk_clipboard_get_for_display (display,
                                                 GDK_SELECTION_CLIPBOARD);

      atom = gdk_atom_intern (argv[1], FALSE);
      target_list = gtk_target_list_new (NULL, 0);
      gtk_target_list_add (target_list, atom, 0, 0);

      targets = gtk_target_table_new_from_list (target_list, &n_targets);
      gtk_target_list_unref (target_list);

      gtk_clipboard_set_with_data (clipboard,
                                   targets, n_targets,
                                   text_get_func, text_clear_func,
                                   g_strdup (argv[2]));
      gtk_target_table_free (targets, n_targets);
    }
  else if (strcmp (argv[0], "popup_at") == 0)
    {
      GtkWidget *parent;
      int width, height;
      PopupAtFlags flags = POPUP_AT_FLAG_NONE;
      int i;

      if (argc < 6)
        {
          g_print ("usage: popup_at <popup-id> <parent-id> "
                   "<top|bottom|left|right|center> "
                   "<width> <height> [<grab>,<resize>,<flip>]\n");
          goto out;
        }

      parent = lookup_window (argv[2]);
      if (!parent)
        {
          g_print ("Parent not found\n");
          goto out;
        }

      width = atoi (argv[4]);
      height = atoi (argv[5]);

      for (i = 6; i < argc; i++)
        {
          if (g_strcmp0 (argv[i], "grab") == 0)
            {
              flags |= POPUP_AT_FLAG_GRAB;
            }
          else if (g_strcmp0 (argv[i], "resize") == 0)
            {
              flags |= POPUP_AT_FLAG_RESIZE;
            }
          else if (g_strcmp0 (argv[i], "flip") == 0)
            {
              flags |= POPUP_AT_FLAG_FLIP;
            }
          else
            {
              g_print ("Unknown argument '%s'", argv[6]);
              goto out;
            }
        }

      popup_at (parent, argv[1], argv[3], width, height, flags);
    }
  else if (strcmp (argv[0], "popup") == 0)
    {
      GtkWidget *parent;

      if (argc != 3)
        {
          g_print ("usage: popup <popup-id> <parent-id>\n");
          goto out;
        }

      parent = lookup_window (argv[2]);
      if (!parent)
        {
          g_print ("Parent not found\n");
          goto out;
        }

      popup_at (parent, argv[1], "center", 100, 100, POPUP_AT_FLAG_NONE);
    }
  else if (strcmp (argv[0], "dismiss") == 0)
    {
      GtkWidget *popup;

      if (argc != 2)
        {
          g_print ("usage: popup <popup-id>\n");
          goto out;
        }

      popup = lookup_window (argv[1]);
      if (!popup)
        goto out;

      g_hash_table_remove (windows, argv[1]);
      gtk_widget_destroy (popup);
    }
  else if (strcmp (argv[0], "sleep") == 0)
    {
      int64_t sleep_ms;

      if (argc != 2)
        {
          g_print ("usage: sleep <milliseconds>\n");
          goto out;
        }

      sleep_ms = atoi (argv[1]);
      is_sleeping = TRUE;
      g_timeout_add_once (sleep_ms, sleep_timeout_cb, in);
    }
  else
    {
      g_print ("Unknown command %s\n", argv[0]);
      goto out;
    }

  g_print ("OK\n");

 out:
  g_strfreev (argv);
}

static void
maybe_read_next_line (GDataInputStream *in)
{
  if (!is_sleeping)
    read_next_line (in);
}

static void
on_line_received (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  GDataInputStream *in = G_DATA_INPUT_STREAM (source);
  GError *error = NULL;
  gsize length;
  char *line = g_data_input_stream_read_line_finish_utf8 (in, result, &length, &error);

  if (line == NULL)
    {
      if (error != NULL)
        g_printerr ("Error reading from stdin: %s\n", error->message);
      if (!dont_exit_on_eof)
        gtk_main_quit ();
      return;
    }

  process_line (line, in);
  g_free (line);
  maybe_read_next_line (in);
}

static void
read_next_line (GDataInputStream *in)
{
  while (sync_after_lines == 0)
    {
      GdkDisplay *display = gdk_display_get_default ();
      g_autoptr (GError) error = NULL;
      g_autofree char *line = NULL;
      size_t length;

      gdk_display_flush (display);

      line = g_data_input_stream_read_line (in, &length, NULL, &error);
      if (!line)
        {
          if (error)
            g_printerr ("Error reading from stdin: %s\n", error->message);
          if (!dont_exit_on_eof)
            gtk_main_quit ();
          return;
        }

      process_line (line, in);
      if (is_sleeping)
        return;
    }

  if (sync_after_lines >= 0)
    sync_after_lines--;

  g_data_input_stream_read_line_async (in, G_PRIORITY_DEFAULT, NULL,
                                       on_line_received, NULL);
}

const GOptionEntry options[] = {
  {
    "wayland", 0, 0, G_OPTION_ARG_NONE,
    &wayland,
    "Create a wayland client, not an X11 one",
    NULL
  },
  {
    "dont-exit-on-eof", 0, 0, G_OPTION_ARG_NONE,
    &dont_exit_on_eof,
    "Don't terminate client when reaching end of file",
    NULL
  },
  {
    "client-id", 0, 0, G_OPTION_ARG_STRING,
    &client_id,
    "Identifier used in Window titles for this client",
    "CLIENT_ID",
  },
  {
    "verbose", 'v', 0, G_OPTION_ARG_NONE,
    &verbose,
    "Verbose",
    NULL,
  },
  { NULL }
};

int
main(int    argc,
     char **argv)
{
  GOptionContext *context = g_option_context_new (NULL);
  GdkScreen *screen;
  GtkCssProvider *provider;
  GError *error = NULL;

  g_log_writer_default_set_use_stderr (TRUE);

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context,
                               &argc, &argv, &error))
    {
      g_printerr ("%s", error->message);
      return 1;
    }

  if (wayland)
    gdk_set_allowed_backends ("wayland");
  else
    gdk_set_allowed_backends ("x11");

  gtk_init (NULL, NULL);

  screen = gdk_screen_get_default ();
  g_assert_true (gdk_screen_is_composited (screen));

  provider = gtk_css_provider_new ();
  static const char *no_decoration_css =
    "decoration {"
    "  border-radius: 0 0 0 0;"
    "  border-width: 0;"
    "  padding: 0 0 0 0;"
    "  box-shadow: 0 0 0 0 rgba(0, 0, 0, 0), 0 0 0 0 rgba(0, 0, 0, 0);"
    "  margin: 0px;"
    "}";
  if (!gtk_css_provider_load_from_data (provider,
                                        no_decoration_css,
                                        strlen (no_decoration_css),
                                        &error))
    {
      g_printerr ("%s", error->message);
      return 1;
    }
  gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  windows = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, NULL);
  event_source_quark = g_quark_from_static_string ("event-source");
  event_handlers_quark = g_quark_from_static_string ("event-handlers");
  can_take_focus_quark = g_quark_from_static_string ("can-take-focus");

  GInputStream *raw_in = g_unix_input_stream_new (0, FALSE);
  GDataInputStream *in = g_data_input_stream_new (raw_in);

  read_next_line (in);

  gtk_main ();

  return 0;
}
