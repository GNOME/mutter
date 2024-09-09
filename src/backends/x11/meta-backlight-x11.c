/*
 * Copyright (C) 2024 Red Hat
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

#include "backends/x11/meta-backlight-x11.h"

#include <glib.h>

#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <xcb/randr.h>

#include "backends/x11/meta-output-xrandr.h"

struct _MetaBacklightX11
{
  MetaBacklight parent;

  Display *xdisplay;
  RROutput output_id;
};

G_DEFINE_FINAL_TYPE (MetaBacklightX11,
                     meta_backlight_x11,
                     META_TYPE_BACKLIGHT)

static void
meta_backlight_x11_update (MetaBacklightX11 *backlight)
{
  Atom atom, actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  g_autofree unsigned char *buffer = NULL;

  atom = XInternAtom (backlight->xdisplay, "Backlight", False);
  XRRGetOutputProperty (backlight->xdisplay,
                        (XID) backlight->output_id,
                        atom,
                        0, G_MAXLONG, False, False, XA_INTEGER,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after, &buffer);

  if (actual_type != XA_INTEGER || actual_format != 32 || nitems < 1)
    {
      g_warning ("Backlight %s: Bad XRandr `Backlight` property format",
                 meta_backlight_get_name (META_BACKLIGHT (backlight)));
      return;
    }

  meta_backlight_update_brightness_target (META_BACKLIGHT (backlight),
                                           ((int *) buffer)[0]);
}

static void
meta_backlight_x11_set_brightness (MetaBacklight       *backlight,
                                   int                  brightness_target,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  MetaBacklightX11 *backlight_x11 = META_BACKLIGHT_X11 (backlight);
  g_autoptr (GTask) task = NULL;
  Atom atom;

  task = g_task_new (backlight_x11, cancellable, callback, user_data);
  g_task_set_task_data (task, GINT_TO_POINTER (brightness_target), NULL);

  atom = XInternAtom (backlight_x11->xdisplay, "Backlight", False);

  xcb_randr_change_output_property (XGetXCBConnection (backlight_x11->xdisplay),
                                    (XID) backlight_x11->output_id,
                                    atom, XCB_ATOM_INTEGER, 32,
                                    XCB_PROP_MODE_REPLACE,
                                    1, &brightness_target);

  g_task_return_int (task, brightness_target);
}

static int
meta_backlight_x11_set_brightness_finish (MetaBacklight  *backlight,
                                          GAsyncResult   *result,
                                          GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, backlight), -1);

  return g_task_propagate_int (G_TASK (result), error);
}

static void
meta_backlight_x11_class_init (MetaBacklightX11Class *klass)
{
  MetaBacklightClass *backlight_class = META_BACKLIGHT_CLASS (klass);

  backlight_class->set_brightness = meta_backlight_x11_set_brightness;
  backlight_class->set_brightness_finish = meta_backlight_x11_set_brightness_finish;
}

static void
meta_backlight_x11_init (MetaBacklightX11 *backlight)
{
}

static gboolean
get_backlight_info (Display   *xdisplay,
                    RROutput   output_id,
                    int       *brightness_min_out,
                    int       *brightness_max_out,
                    GError   **error)
{
  Atom atom;
  xcb_connection_t *xcb_conn;
  xcb_randr_query_output_property_cookie_t cookie;
  g_autofree xcb_randr_query_output_property_reply_t *reply = NULL;
  int32_t *values;

  atom = XInternAtom (xdisplay, "Backlight", False);

  xcb_conn = XGetXCBConnection (xdisplay);
  cookie = xcb_randr_query_output_property (xcb_conn,
                                            output_id,
                                            (xcb_atom_t) atom);
  reply = xcb_randr_query_output_property_reply (xcb_conn,
                                                 cookie,
                                                 NULL);

  /* This can happen on systems without backlights. */
  if (reply == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No backlight found");
      return FALSE;
    }

  if (!reply->range || reply->length != 2)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Backlight is not in range");
      return FALSE;
    }

  values = xcb_randr_query_output_property_valid_values (reply);

  if (brightness_min_out)
    *brightness_min_out = values[0];
  if (brightness_max_out)
    *brightness_max_out = values[1];

  return TRUE;
}

MetaBacklightX11 *
meta_backlight_x11_new (MetaBackend           *backend,
                        Display               *xdisplay,
                        RROutput               output_id,
                        const MetaOutputInfo  *output_info,
                        GError               **error)
{
  g_autoptr (MetaBacklightX11) backlight = NULL;
  int min, max;

  /* we currently only support backlights for built-in panels */
  if (!meta_output_info_is_builtin (output_info))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "External displays are not supported");
      return NULL;
    }

  if (!get_backlight_info (xdisplay, output_id, &min, &max, error))
    return NULL;

  backlight = g_object_new (META_TYPE_BACKLIGHT_X11,
                            "backend", backend,
                            "name", output_info->name,
                            "brightness-min", min,
                            "brightness-max", max,
                            NULL);
  backlight->xdisplay = xdisplay;
  backlight->output_id = output_id;

  meta_backlight_x11_update (backlight);

  return g_steal_pointer (&backlight);
}
