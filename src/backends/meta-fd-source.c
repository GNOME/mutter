/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
 * Copyright (C) 2016-2022 Red Hat Inc.
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
 *
 */

#include "config.h"

#include <unistd.h>

#include "backends/meta-fd-source.h"

typedef struct _MetaFdtSource
{
  GSource source;

  GSourceFunc prepare;
  GSourceFunc dispatch;
  gpointer user_data;

  GPollFD poll_fd;
} MetaFdSource;

static gboolean
meta_fd_source_prepare (GSource *source,
                        int     *timeout_ms)
{
  MetaFdSource *fd_source = (MetaFdSource *) source;

  *timeout_ms = -1;

  return fd_source->prepare (fd_source->user_data);
}

static gboolean
meta_fd_source_check (GSource *source)
{
  MetaFdSource *fd_source = (MetaFdSource *) source;

  return !!(fd_source->poll_fd.revents & G_IO_IN);
}

static gboolean
meta_fd_source_dispatch (GSource     *source,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  MetaFdSource *fd_source = (MetaFdSource *) source;

  return fd_source->dispatch (fd_source->user_data);
}

static void
meta_fd_source_finalize (GSource *source)
{
  MetaFdSource *fd_source = (MetaFdSource *) source;

  close (fd_source->poll_fd.fd);
}

static GSourceFuncs fd_source_funcs = {
  .prepare = meta_fd_source_prepare,
  .check = meta_fd_source_check,
  .dispatch = meta_fd_source_dispatch,
  .finalize = meta_fd_source_finalize,
};

GSource *
meta_create_fd_source (int             fd,
                       const char     *name,
                       GSourceFunc     prepare,
                       GSourceFunc     dispatch,
                       gpointer        user_data,
                       GDestroyNotify  notify)
{
  GSource *source;
  MetaFdSource *fd_source;

  source = g_source_new (&fd_source_funcs, sizeof (MetaFdSource));
  g_source_set_name (source, name);
  fd_source = (MetaFdSource *) source;

  fd_source->poll_fd.fd = fd;
  fd_source->poll_fd.events = G_IO_IN;

  fd_source->prepare = prepare;
  fd_source->dispatch = dispatch;
  fd_source->user_data = user_data;

  g_source_set_callback (source, dispatch, user_data, notify);
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_add_poll (source, &fd_source->poll_fd);
  g_source_set_can_recurse (source, TRUE);

  return source;
}
