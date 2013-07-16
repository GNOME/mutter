/*
 * Copyright © 2010 Intel Corporation
 *             2013 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/major.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "meta-tty.h"
#include <gio/gio.h>
#include <glib-unix.h>

/* Introduced in 2.6.38 */
#ifndef K_OFF
#define K_OFF 0x04
#endif

struct _MetaTTYClass
{
  GObjectClass parent_class;
};

struct _MetaTTY
{
  GObject parent;

  int fd;
  struct termios terminal_attributes;

  GMainContext *nested_context;
  GMainLoop *nested_loop;

  int input_source;
  GSource *vt_enter_source, *vt_leave_source;
  GSource *nested_term;
  int vt, starting_vt;
  int kb_mode;
};

enum {
  SIGNAL_ENTER,
  SIGNAL_LEAVE,
  SIGNAL_LAST
};

static int signals[SIGNAL_LAST];

static void meta_tty_initable_iface_init (GInitableIface *);

G_DEFINE_TYPE_WITH_CODE (MetaTTY, meta_tty, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
						meta_tty_initable_iface_init));

static gboolean
quit_nested_loop (gpointer user_data)
{
  MetaTTY *tty = user_data;

  g_main_loop_quit (tty->nested_loop);

  return FALSE;
}

static gboolean
vt_release_handler (gpointer user_data)
{
  MetaTTY *tty = user_data;

  g_signal_emit (tty, signals[SIGNAL_LEAVE], 0);

  ioctl (tty->fd, VT_RELDISP, 1);

  /* We can't do anything at this point, because we don't
     have input devices and we don't have the DRM master,
     so let's run a nested busy loop until the VT is reentered */
  g_main_loop_run (tty->nested_loop);

  ioctl (tty->fd, VT_RELDISP, VT_ACKACQ);

  g_signal_emit (tty, signals[SIGNAL_ENTER], 0);

  return FALSE;
}

static int
on_tty_input (int          fd, 
	      GIOCondition mask,
	      gpointer     user_data)
{
  MetaTTY *tty = user_data;

  /* Ignore input to tty.  We get keyboard events from evdev */
  tcflush(tty->fd, TCIFLUSH);

  return 1;
}

static int
try_open_vt (MetaTTY  *tty,
	     GError  **error)
{
  int tty0, fd;
  char filename[16];

  tty0 = open ("/dev/tty0", O_WRONLY | O_CLOEXEC);
  if (tty0 < 0)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Could not open tty0: %s", strerror (errno));
      return -1;
    }

  if (ioctl (tty0, VT_OPENQRY, &tty->vt) < 0 || tty->vt == -1) {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Could not open tty0: %s", strerror (errno));
      close (tty0);
      return -1;
  }

  close (tty0);
  snprintf (filename, sizeof filename, "/dev/tty%d", tty->vt);
  g_debug("compositor: using new vt %s\n", filename);
  fd = open (filename, O_RDWR | O_NOCTTY | O_CLOEXEC);
  return fd;
}

gboolean
meta_tty_activate_vt (MetaTTY  *tty,
		      int       vt,
		      GError  **error)
{
  if (ioctl(tty->fd, VT_ACTIVATE, vt) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		   strerror (errno));
      return FALSE;
    }
  else
    return TRUE;
}

static int
env_get_fd (const char *env)
{
  const char *value;

  value = g_getenv (env);

  if (value == NULL)
    return -1;
  else
    return g_ascii_strtoll (value, NULL, 10);
}

static gboolean
meta_tty_initable_init(GInitable     *initable,
		       GCancellable  *cancellable,
		       GError       **error)
{
  MetaTTY *tty = META_TTY (initable);
  struct termios raw_attributes;
  struct vt_mode mode = { 0 };
  int ret;
	
  struct stat buf;
  struct vt_stat vts;

  tty->fd = env_get_fd ("WESTON_TTY_FD");
  if (tty->fd < 0)
    tty->fd = STDIN_FILENO;

  if (fstat(tty->fd, &buf) == 0 &&
      major(buf.st_rdev) == TTY_MAJOR &&
      minor(buf.st_rdev) > 0)
    {
      if (tty->fd == STDIN_FILENO)
	tty->fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
      tty->vt = minor(buf.st_rdev);
    }
  else
    {
      /* Fall back to try opening a new VT.  This typically
       * requires root. */
      tty->fd = try_open_vt(tty, error);
    }

  if (tty->fd <= 0 && (!error || !*error))
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Could not open tty0: %s", strerror (errno));
      return FALSE;
    }

  if (ioctl(tty->fd, VT_GETSTATE, &vts) == 0)
    tty->starting_vt = vts.v_active;
  else
    tty->starting_vt = tty->vt;
  
  if (tty->starting_vt != tty->vt)
    {
      if (ioctl(tty->fd, VT_ACTIVATE, tty->vt) < 0 ||
	  ioctl(tty->fd, VT_WAITACTIVE, tty->vt) < 0)
	{
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       "Failed to switch to new vt: %s", strerror (errno));
	  goto err;
	}
    }

  if (tcgetattr(tty->fd, &tty->terminal_attributes) < 0)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Could not get terminal attributes: %s", strerror (errno));
      goto err;
    }

  /* Ignore control characters and disable echo */
  raw_attributes = tty->terminal_attributes;
  cfmakeraw(&raw_attributes);

  /* Fix up line endings to be normal (cfmakeraw hoses them) */
  raw_attributes.c_oflag |= OPOST | OCRNL;
  /* Don't generate ttou signals */
  raw_attributes.c_oflag &= ~TOSTOP;

  if (tcsetattr(tty->fd, TCSANOW, &raw_attributes) < 0)
    g_warning("Could not put terminal into raw mode: %s", strerror (errno));

  ioctl(tty->fd, KDGKBMODE, &tty->kb_mode);
  ret = ioctl(tty->fd, KDSKBMODE, K_OFF);
  if (ret)
    {
      ret = ioctl(tty->fd, KDSKBMODE, K_RAW);
      if (ret)
	{
	  g_set_error (error, G_IO_ERROR,
		       g_io_error_from_errno (errno),
		       "Failed to set keyboard mode: %s", strerror (errno));
	  goto err_attr;
	}

      tty->input_source = g_unix_fd_add (tty->fd,
					 G_IO_IN,
					 on_tty_input, tty);
    }

  ret = ioctl(tty->fd, KDSETMODE, KD_GRAPHICS);
  if (ret)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Failed to set KD_GRAPHICS mode: %s", strerror (errno));
      goto err_kdkbmode;
    }

  mode.mode = VT_PROCESS;
  mode.relsig = SIGUSR1;
  mode.acqsig = SIGUSR2;
  if (ioctl(tty->fd, VT_SETMODE, &mode) < 0)
    {
      g_set_error (error, G_IO_ERROR,
		   g_io_error_from_errno (errno),
		   "Failed to take control of vt handling: %s", strerror (errno));
      goto err_kdmode;
    }

  tty->vt_leave_source = g_unix_signal_source_new (SIGUSR1);
  g_source_set_callback (tty->vt_leave_source, vt_release_handler, tty, NULL);

  tty->vt_enter_source = g_unix_signal_source_new (SIGUSR2);
  g_source_set_callback (tty->vt_enter_source, quit_nested_loop, tty, NULL);
  tty->nested_term = g_unix_signal_source_new (SIGTERM);
  g_source_set_callback (tty->nested_term, quit_nested_loop, tty, NULL);

  tty->nested_context = g_main_context_new ();
  tty->nested_loop = g_main_loop_new (tty->nested_context, FALSE);

  g_source_attach (tty->vt_leave_source, NULL);
  g_source_attach (tty->vt_enter_source, tty->nested_context);
  g_source_attach (tty->nested_term, tty->nested_context);

  return TRUE;

 err_kdmode:
  ioctl (tty->fd, KDSETMODE, KD_TEXT);

 err_kdkbmode:
  if (tty->input_source)
    g_source_remove (tty->input_source);
  ioctl (tty->fd, KDSKBMODE, tty->kb_mode);

 err_attr:
  tcsetattr (tty->fd, TCSANOW, &tty->terminal_attributes);
  
 err:
  close (tty->fd);
  return FALSE;
}

static void
tty_reset (MetaTTY *tty)
{
  struct vt_mode mode = { 0 };

  if (ioctl (tty->fd, KDSKBMODE, tty->kb_mode))
    g_warning ("failed to restore keyboard mode: %s", strerror (errno));

  if (ioctl (tty->fd, KDSETMODE, KD_TEXT))
    g_warning ("failed to set KD_TEXT mode on tty: %s", strerror (errno));

  if (tcsetattr (tty->fd, TCSANOW, &tty->terminal_attributes) < 0)
    g_warning ("could not restore terminal to canonical mode");

  mode.mode = VT_AUTO;
  if (ioctl (tty->fd, VT_SETMODE, &mode) < 0)
    g_warning ("could not reset vt handling\n");

  if (tty->vt != tty->starting_vt)
    {
      ioctl(tty->fd, VT_ACTIVATE, tty->starting_vt);
      ioctl(tty->fd, VT_WAITACTIVE, tty->starting_vt);
    }
}

static void
meta_tty_finalize (GObject *object)
{
  MetaTTY *tty = META_TTY (object);

  if (tty->input_source)
    g_source_remove (tty->input_source);

  g_source_destroy (tty->vt_enter_source);
  g_source_destroy (tty->vt_leave_source);
  g_source_destroy (tty->nested_term);

  g_main_loop_unref (tty->nested_loop);
  g_main_context_unref (tty->nested_context);

  tty_reset (tty);

  close (tty->fd);

  G_OBJECT_CLASS (meta_tty_parent_class)->finalize (object);
}

static void
meta_tty_init (MetaTTY *self)
{
}

static void
meta_tty_class_init (MetaTTYClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_tty_finalize;

  signals[SIGNAL_ENTER] = g_signal_new ("enter",
					G_TYPE_FROM_CLASS (klass),
					G_SIGNAL_RUN_FIRST,
					0, /* class offset */
					NULL, NULL, /* accumulator */
					g_cclosure_marshal_VOID__VOID,
					G_TYPE_NONE, 0);

  signals[SIGNAL_LEAVE] = g_signal_new ("leave",
					G_TYPE_FROM_CLASS (klass),
					G_SIGNAL_RUN_FIRST,
					0, /* class offset */
					NULL, NULL, /* accumulator */
					g_cclosure_marshal_VOID__VOID,
					G_TYPE_NONE, 0);
}

static void
meta_tty_initable_iface_init (GInitableIface *iface)
{
  iface->init = meta_tty_initable_init;
}

MetaTTY *
meta_tty_new (void)
{
  GError *error;
  MetaTTY *tty;

  error = NULL;
  tty = g_initable_new (META_TYPE_TTY, NULL, &error, NULL);

  if (tty == NULL)
    {
      g_warning ("Failed to initalize TTY handling: %s", error->message);
      g_error_free (error);
    }

  return tty;
}
