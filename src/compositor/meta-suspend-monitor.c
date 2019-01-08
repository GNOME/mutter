/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <sys/timerfd.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#include "meta-suspend-monitor.h"

struct _MetaSuspendMonitorPrivate
{
  GSource *timer_source;
  GInputStream *timer_stream;
};

enum
{
  RESUMED,
  NUMBER_OF_SIGNALS,
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (MetaSuspendMonitor, meta_suspend_monitor, G_TYPE_OBJECT);

static gboolean schedule_indefinite_wakeup (MetaSuspendMonitor *self);

static void
meta_suspend_monitor_dispose (GObject *object)
{
  MetaSuspendMonitor *self = META_SUSPEND_MONITOR (object);

  g_clear_pointer (&self->priv->timer_source, g_source_destroy);
  g_clear_object (&self->priv->timer_stream);

  G_OBJECT_CLASS (meta_suspend_monitor_parent_class)->dispose (object);
}

static void
meta_suspend_monitor_class_init (MetaSuspendMonitorClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_suspend_monitor_dispose;

  signals[RESUMED] = g_signal_new ("resumed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static gboolean
on_timer_source_ready (GObject            *timer_stream,
                       MetaSuspendMonitor *self)
{
  gint64 number_of_fires;
  gssize bytes_read;
  GError *error = NULL;

  g_return_val_if_fail (META_IS_SUSPEND_MONITOR (self), FALSE);

  bytes_read =
    g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM (timer_stream),
                                              &number_of_fires, sizeof (gint64),
                                              NULL, &error);

  if (bytes_read < 0 )
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_signal_emit (self, signals[RESUMED], 0);
        }
      else
        {
          g_warning ("MetaSuspendMonitor: failed to read from timer fd: %s\n",
                     error->message);
          goto out;
        }
    }

  schedule_indefinite_wakeup (self);
out:
  g_error_free (error);
  return FALSE;
}

static void
clear_wakeup_source_pointer (MetaSuspendMonitor *self)
{
  self->priv->timer_source = NULL;
}

static gboolean
schedule_indefinite_wakeup (MetaSuspendMonitor *self)
{
  struct itimerspec timer_spec;
  int fd;
  int result;
  GSource *source;

  fd = timerfd_create (CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);

  if (fd < 0)
    {
      g_warning ("MetaSuspendMonitor: could not create timer fd: %s", strerror (errno));
      return FALSE;
    }

  memset (&timer_spec, 0, sizeof (timer_spec));

  /* set the timer for the distant future, so it only wakes up on resume */
  timer_spec.it_value.tv_sec = LONG_MAX;

  result = timerfd_settime (fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &timer_spec, NULL);

  if (result < 0)
    {
      g_warning ("MetaSuspendMonitor: could not set timer: %s", strerror (errno));
      return FALSE;
    }

  g_clear_object (&self->priv->timer_stream);

  self->priv->timer_stream = g_unix_input_stream_new (fd, TRUE);

  source =
    g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM (self->priv->timer_stream),
                                           NULL);
  g_clear_pointer (&self->priv->timer_source,
                   g_source_destroy);
  self->priv->timer_source = source;
  g_source_set_callback (self->priv->timer_source,
                         (GSourceFunc) on_timer_source_ready, self,
                         (GDestroyNotify) clear_wakeup_source_pointer);
  g_source_attach (self->priv->timer_source, NULL);
  g_source_unref (source);

  return TRUE;
}

static void
meta_suspend_monitor_init (MetaSuspendMonitor *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, META_TYPE_SUSPEND_MONITOR, MetaSuspendMonitorPrivate);

  schedule_indefinite_wakeup (self);
}

MetaSuspendMonitor *
meta_suspend_monitor_new (void)
{
  MetaSuspendMonitor *self;

  self = META_SUSPEND_MONITOR (g_object_new (META_TYPE_SUSPEND_MONITOR, NULL));

  return self;
}
