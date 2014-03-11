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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "meta-login1.h"

#include "meta-dbus-login1.h"

#include "meta-wayland-private.h"
#include "meta-cursor-tracker-private.h"

#include <gio/gunixfdlist.h>

#include <clutter/clutter.h>
#include <clutter/evdev/clutter-evdev.h>
#include <clutter/egl/clutter-egl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>

#include <systemd/sd-login.h>

struct _MetaLogin1
{
  Login1Session *session_proxy;
  Login1Seat *seat_proxy;

  gboolean session_active;
};

/* Stolen from tp_escape_as_identifier, from tp-glib,
 * which follows the same escaping convention as systemd.
 */
static inline gboolean
_esc_ident_bad (gchar c, gboolean is_first)
{
  return ((c < 'a' || c > 'z') &&
          (c < 'A' || c > 'Z') &&
          (c < '0' || c > '9' || is_first));
}

static gchar *
escape_dbus_component (const gchar *name)
{
  gboolean bad = FALSE;
  size_t len = 0;
  GString *op;
  const gchar *ptr, *first_ok;

  g_return_val_if_fail (name != NULL, NULL);

  /* fast path for empty name */
  if (name[0] == '\0')
    return g_strdup ("_");

  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          bad = TRUE;
          len += 3;
        }
      else
        len++;
    }

  /* fast path if it's clean */
  if (!bad)
    return g_strdup (name);

  /* If strictly less than ptr, first_ok is the first uncopied safe character.
   */
  first_ok = name;
  op = g_string_sized_new (len);
  for (ptr = name; *ptr; ptr++)
    {
      if (_esc_ident_bad (*ptr, ptr == name))
        {
          /* copy preceding safe characters if any */
          if (first_ok < ptr)
            {
              g_string_append_len (op, first_ok, ptr - first_ok);
            }
          /* escape the unsafe character */
          g_string_append_printf (op, "_%02x", (unsigned char)(*ptr));
          /* restart after it */
          first_ok = ptr + 1;
        }
    }
  /* copy trailing safe characters if any */
  if (first_ok < ptr)
    {
      g_string_append_len (op, first_ok, ptr - first_ok);
    }
  return g_string_free (op, FALSE);
}

static char *
get_escaped_dbus_path (const char *prefix,
                       const char *component)
{
  char *escaped_component = escape_dbus_component (component);
  char *path = g_strconcat (prefix, "/", component, NULL);

  g_free (escaped_component);
  return path;
}

static Login1Session *
get_session_proxy (GCancellable *cancellable)
{
  char *proxy_path;
  char *session_id;
  Login1Session *session_proxy;

  if (sd_pid_get_session (getpid (), &session_id) < 0)
    return NULL;

  proxy_path = get_escaped_dbus_path ("/org/freedesktop/login1/session", session_id);

  session_proxy = login1_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                         "org.freedesktop.login1",
                                                         proxy_path,
                                                         cancellable, NULL);
  free (proxy_path);

  return session_proxy;
}

static Login1Seat *
get_seat_proxy (GCancellable *cancellable)
{
  return login1_seat_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                             G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1/seat/self",
                                             cancellable, NULL);
}

static void
session_unpause (void)
{
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglDisplay *cogl_display;

  backend = clutter_get_default_backend ();
  cogl_context = clutter_backend_get_cogl_context (backend);
  cogl_display = cogl_context_get_display (cogl_context);
  cogl_kms_display_queue_modes_reset (cogl_display);

  clutter_set_paused (FALSE);
  /* clutter_evdev_reclaim_devices (); */

  {
    MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

    /* When we mode-switch back, we need to immediately queue a redraw
     * in case nothing else quueed one for us, and force the cursor to
     * update. */

    clutter_actor_queue_redraw (compositor->stage);
    meta_cursor_tracker_force_update (compositor->seat->cursor_tracker);
  }
}

static void
session_pause (void)
{
  clutter_set_paused (TRUE);
  /* clutter_evdev_release_devices (); */
}

static void
sync_active (MetaLogin1 *self)
{
  gboolean active = login1_session_get_active (LOGIN1_SESSION (self->session_proxy));

  if (active == self->session_active)
    return;

  self->session_active = active;

  if (active)
    session_unpause ();
  else
    session_pause ();
}

static void
on_active_changed (Login1Session *session,
                   GParamSpec    *pspec,
                   gpointer       user_data)
{
  MetaLogin1 *self = user_data;
  sync_active (self);
}

static gboolean
take_device (Login1Session *session_proxy,
             int            dev_major,
             int            dev_minor,
             int           *out_fd,
             GCancellable  *cancellable,
             GError       **error)
{
  gboolean ret = FALSE;
  GVariant *fd_variant = NULL;
  int fd = -1;
  GUnixFDList *fd_list;

  if (!login1_session_call_take_device_sync (session_proxy,
                                             dev_major,
                                             dev_minor,
                                             NULL,
                                             &fd_variant,
                                             NULL, /* paused */
                                             &fd_list,
                                             cancellable,
                                             error))
    goto out;

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), error);
  if (fd == -1)
    goto out;

  *out_fd = fd;
  ret = TRUE;

 out:
  if (fd_variant)
    g_variant_unref (fd_variant);
  if (fd_list)
    g_object_unref (fd_list);
  return ret;
}

static gboolean
get_device_info_from_path (const char *path,
                           int        *out_major,
                           int        *out_minor)
{
  gboolean ret = FALSE;
  int r;
  struct stat st;

  r = stat (path, &st);
  if (r < 0)
    goto out;
  if (!S_ISCHR (st.st_mode))
    goto out;

  *out_major = major (st.st_rdev);
  *out_minor = minor (st.st_rdev);
  ret = TRUE;

 out:
  return ret;
}

static gboolean
get_device_info_from_fd (int  fd,
                         int *out_major,
                         int *out_minor)
{
  gboolean ret = FALSE;
  int r;
  struct stat st;

  r = fstat (fd, &st);
  if (r < 0)
    goto out;
  if (!S_ISCHR (st.st_mode))
    goto out;

  *out_major = major (st.st_rdev);
  *out_minor = minor (st.st_rdev);
  ret = TRUE;

 out:
  return ret;
}

static int
open_evdev_device (const char  *path,
                   int          flags,
                   gpointer     user_data,
                   GError     **error)
{
  MetaLogin1 *self = user_data;
  int fd;
  int major, minor;

  if (!get_device_info_from_path (path, &major, &minor))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_FOUND,
                   "Could not get device info for path %s: %m", path);
      return -1;
    }

  if (!take_device (self->session_proxy, major, minor, &fd, NULL, error))
    return -1;

  return fd;
}

static void
close_evdev_device (int      fd,
                    gpointer user_data)
{
  MetaLogin1 *self = user_data;
  int major, minor;
  GError *error = NULL;

  if (!get_device_info_from_fd (fd, &major, &minor))
    {
      g_warning ("Could not get device info for fd %d: %m", fd);
      return;
    }

  if (!login1_session_call_release_device_sync (self->session_proxy,
                                                major, minor,
                                                NULL, &error))
    {
      g_warning ("Could not release device %d,%d: %s", major, minor, error->message);
    }
}

static gboolean
get_kms_fd (Login1Session *session_proxy,
            int *fd_out)
{
  int major, minor;
  int fd;
  GError *error = NULL;

  /* XXX -- use udev to find the DRM master device */
  if (!get_device_info_from_path ("/dev/dri/card0", &major, &minor))
    {
      g_warning ("Could not stat /dev/dri/card0: %m");
      return FALSE;
    }

  if (!take_device (session_proxy, major, minor, &fd, NULL, &error))
    {
      g_warning ("Could not open DRM device: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  *fd_out = fd;

  return TRUE;
}

MetaLogin1 *
meta_login1_new (void)
{
  MetaLogin1 *self;
  Login1Session *session_proxy;
  GError *error = NULL;
  int kms_fd;

  session_proxy = get_session_proxy (NULL);
  if (!login1_session_call_take_control_sync (session_proxy, FALSE, NULL, &error))
    {
      g_warning ("Could not take control: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  if (!get_kms_fd (session_proxy, &kms_fd))
    return NULL;

  self = g_slice_new0 (MetaLogin1);
  self->session_proxy = session_proxy;
  self->seat_proxy = get_seat_proxy (NULL);

  /* Clutter/Cogl start out in a state that assumes the session is active */
  self->session_active = TRUE;

  clutter_egl_set_kms_fd (kms_fd);
  clutter_evdev_set_device_callbacks (open_evdev_device,
                                      close_evdev_device,
                                      self);

  g_signal_connect (self->session_proxy, "notify::active", G_CALLBACK (on_active_changed), self);
  sync_active (self);

  return self;
}

void
meta_login1_free (MetaLogin1 *self)
{
  g_object_unref (self->seat_proxy);
  g_object_unref (self->session_proxy);
  g_slice_free (MetaLogin1, self);
}

gboolean
meta_login1_activate_session (MetaLogin1  *self,
                              GError     **error)
{
  if (!login1_session_call_activate_sync (self->session_proxy, NULL, error))
    return FALSE;

  sync_active (self);
  return TRUE;
}

gboolean
meta_login1_activate_vt (MetaLogin1  *self,
                         int          vt,
                         GError     **error)
{
  return login1_seat_call_switch_to_sync (self->seat_proxy, vt, NULL, error);
}
