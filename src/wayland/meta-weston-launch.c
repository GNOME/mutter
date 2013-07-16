/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#include <config.h>

#include <gio/gio.h>
#include <gio/gunixfdmessage.h>

#include <glib.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "meta-weston-launch.h"

static gboolean
send_message_to_wl (GSocket                *weston_launch,
		    void                   *message,
		    gsize                   size,
		    GSocketControlMessage  *out_cmsg,
		    GSocketControlMessage **in_cmsg,
		    GError                **error)
{
  int ok;
  GInputVector in_iov = { &ok, sizeof (int) };
  GOutputVector out_iov = { message, size };
  GSocketControlMessage *out_all_cmsg[2];
  GSocketControlMessage **in_all_cmsg;
  int flags = 0;
  int i;

  out_all_cmsg[0] = out_cmsg;
  out_all_cmsg[1] = NULL;
  if (g_socket_send_message (weston_launch, NULL,
			     &out_iov, 1,
			     out_all_cmsg, -1,
			     flags, NULL, error) != (gssize)size)
    return FALSE;

  if (g_socket_receive_message (weston_launch, NULL,
				&in_iov, 1,
				&in_all_cmsg, NULL,
				&flags, NULL, error) != sizeof (int))
    return FALSE;

  if (ok != 0)
    {
      if (ok == -1)
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		     "Got failure from weston-launch");
      else
	g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ok),
		     "Got failure from weston-launch: %s", strerror (-ok));

      for (i = 0; in_all_cmsg && in_all_cmsg[i]; i++)
	g_object_unref (in_all_cmsg[i]);
      g_free (in_all_cmsg);

      return FALSE;
    }

  if (in_all_cmsg && in_all_cmsg[0])
    {
      for (i = 1; in_all_cmsg[i]; i++)
	g_object_unref (in_all_cmsg[i]);
      *in_cmsg = in_all_cmsg[0];
    }

  g_free (in_all_cmsg);
  return TRUE;
}	    

gboolean
meta_weston_launch_set_master (GSocket   *weston_launch,
			       int        drm_fd,
			       gboolean   master,
			       GError   **error)
{
  if (weston_launch)
    {
      struct weston_launcher_set_master message;
      GSocketControlMessage *cmsg;
      gboolean ok;

      message.header.opcode = WESTON_LAUNCHER_DRM_SET_MASTER;
      message.set_master = master;

      cmsg = g_unix_fd_message_new ();
      if (g_unix_fd_message_append_fd (G_UNIX_FD_MESSAGE (cmsg),
				       drm_fd, error) == FALSE)
	{
	  g_object_unref (cmsg);
	  return FALSE;
	}

      ok = send_message_to_wl (weston_launch, &message, sizeof message, cmsg, NULL, error);

      g_object_unref (cmsg);
      return ok;
    }
  else
    {
      int ret;

      if (master)
	ret = drmSetMaster (drm_fd);
      else
	ret = drmDropMaster (drm_fd);

      if (ret < 0)
	{
	  g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
		       "Failed to set DRM master directly: %s", strerror (-ret));
	  return FALSE;
	}
      else
	return TRUE;
    }
}

int
meta_weston_launch_open_input_device (GSocket    *weston_launch,
				      const char *name,
				      int         flags,
				      GError    **error)
{
  if (weston_launch)
    {
      struct weston_launcher_open *message;
      GSocketControlMessage *cmsg;
      gboolean ok;
      gsize size;
      int *fds, n_fd;
      int ret;

      size = sizeof (struct weston_launcher_open) + strlen (name) + 1;
      message = g_malloc (size);
      message->header.opcode = WESTON_LAUNCHER_OPEN;
      message->flags = flags;
      strcpy (message->path, name);
      message->path[strlen(name)] = 0;

      ok = send_message_to_wl (weston_launch, message, size,
			       NULL, &cmsg, error);

      if (ok)
	{
	  g_assert (G_IS_UNIX_FD_MESSAGE (cmsg));

	  fds = g_unix_fd_message_steal_fds (G_UNIX_FD_MESSAGE (cmsg), &n_fd);
	  g_assert (n_fd == 1);

	  ret = fds[0];
	  g_free (fds);
	  g_object_unref (cmsg);
	}
      else
	ret = -1;

      g_free (message);
      return ret;
    }
  else
    {
      int ret;

      ret = open (name, flags, 0);

      if (ret < 0)
	g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		     "Failed to open input device directly: %s", strerror (errno));

      return ret;
    }
}

