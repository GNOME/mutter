/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#include "config.h"

#include <libeis.h>

#include "backends/meta-eis.h"
#include "backends/meta-eis-client.h"
#include "clutter/clutter-mutter.h"
#include "meta/util.h"

typedef struct _MetaEventSource MetaEventSource;

struct _MetaEventSource
{
  GSource source;

  MetaEis *meta_eis;
  GPollFD event_poll_fd;
};

struct _MetaEis
{
  GObject parent_instance;
  MetaBackend *backend;

  struct eis *eis;
  MetaEventSource *event_source;

  GHashTable *eis_clients; /* eis_client => MetaEisClient */
};

G_DEFINE_TYPE (MetaEis, meta_eis, G_TYPE_OBJECT)

MetaBackend *
meta_eis_get_backend (MetaEis *meta_eis)
{
  return meta_eis->backend;
}

void
meta_eis_remove_all_clients (MetaEis *meta_eis)
{
  g_hash_table_remove_all (meta_eis->eis_clients);
}

static void
meta_eis_remove_client (MetaEis           *meta_eis,
                        struct eis_client *eis_client)
{
  g_hash_table_remove (meta_eis->eis_clients, eis_client);
}

static void
meta_eis_add_client (MetaEis           *meta_eis,
                     struct eis_client *eis_client)
{
  MetaEisClient *meta_eis_client;

  meta_eis_client = meta_eis_client_new (meta_eis, eis_client);

  g_hash_table_insert (meta_eis->eis_clients,
                       eis_client_ref (eis_client),
                       meta_eis_client);
}

static void
process_event (MetaEis          *meta_eis,
               struct eis_event *event)
{
  enum eis_event_type type = eis_event_get_type (event);
  struct eis_client *eis_client = eis_event_get_client (event);
  MetaEisClient *meta_eis_client;

  switch (type)
    {
    case EIS_EVENT_CLIENT_CONNECT:
      meta_eis_add_client (meta_eis, eis_client);
      break;
    case EIS_EVENT_CLIENT_DISCONNECT:
      meta_eis_remove_client (meta_eis, eis_client);
      break;
    default:
      meta_eis_client = g_hash_table_lookup (meta_eis->eis_clients, eis_client);
      if (!meta_eis_client)
        {
          g_warning ("Event for unknown EIS client: %s",
                     eis_client_get_name (eis_client));
          return;
        }
      meta_eis_client_process_event (meta_eis_client, event);
      break;
    }
}

static void
process_events (MetaEis *meta_eis)
{
  struct eis_event *e;

  while ((e = eis_get_event (meta_eis->eis)))
    {
      process_event (meta_eis, e);
      eis_event_unref (e);
    }
}

static MetaEventSource *
meta_event_source_new (MetaEis      *meta_eis,
                       int           fd,
                       GSourceFuncs *event_funcs)
{
  GSource *source;
  MetaEventSource *event_source;

  source = g_source_new (event_funcs, sizeof (MetaEventSource));
  event_source = (MetaEventSource *) source;

  /* setup the source */
  event_source->meta_eis = meta_eis;

  event_source->event_poll_fd.fd = fd;
  event_source->event_poll_fd.events = G_IO_IN;

  /* and finally configure and attach the GSource */
  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_add_poll (source, &event_source->event_poll_fd);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return event_source;
}

static void
meta_event_source_free (MetaEventSource *source)
{
  GSource *g_source = (GSource *) source;

  /* ignore the return value of close, it's not like we can do something
   * about it */
  close (source->event_poll_fd.fd);

  g_source_destroy (g_source);
  g_source_unref (g_source);
}

static gboolean
meta_event_prepare (GSource *g_source,
                    int     *timeout_ms)
{
  MetaEventSource *source = (MetaEventSource *) g_source;
  MetaEis *meta_eis = source->meta_eis;
  struct eis_event *e;

  *timeout_ms = -1;

  e = eis_peek_event (meta_eis->eis);
  if (e)
    {
      eis_event_unref (e);
      return TRUE;
    }
  return FALSE;
}

static gboolean
meta_event_check (GSource *source)
{
  MetaEventSource *event_source = (MetaEventSource *) source;
  gboolean retval;

  retval = !!(event_source->event_poll_fd.revents & G_IO_IN);

  return retval;
}

static gboolean
meta_event_dispatch (GSource     *g_source,
                     GSourceFunc  callback,
                     gpointer     user_data)
{
  MetaEventSource *source = (MetaEventSource *) g_source;
  MetaEis *meta_eis = source->meta_eis;

  eis_dispatch (meta_eis->eis);
  process_events (meta_eis);

  return TRUE;
}

static GSourceFuncs eis_event_funcs = {
  meta_event_prepare,
  meta_event_check,
  meta_event_dispatch,
  NULL
};

static void
eis_logger (struct eis             *eis,
            enum eis_log_priority   priority,
            const char             *message,
            struct eis_log_context *ctx)
{
  switch (priority)
    {
    case EIS_LOG_PRIORITY_DEBUG:
      meta_topic (META_DEBUG_EIS, "%s", message);
      break;
    case EIS_LOG_PRIORITY_WARNING:
      g_warning ("%s", message);
      break;
    case EIS_LOG_PRIORITY_ERROR:
      g_error ("%s", message);
      break;
    case EIS_LOG_PRIORITY_INFO:
    default:
      g_info ("%s", message);
      break;
    }
}

int
meta_eis_add_client_get_fd (MetaEis *meta_eis)
{
  return eis_backend_fd_add_client (meta_eis->eis);
}

static int
try_and_find_free_eis_socket (MetaEis *meta_eis)
{
  int rc;
  int n;
  char socketname[16];

  for (n = 0; n < 100; n++)
    {
      g_snprintf (socketname, sizeof (socketname), "eis-%d", n);
      rc = eis_setup_backend_socket (meta_eis->eis, socketname);
      if (rc == 0)
        {
          g_info ("Using EIS socket: %s", socketname);
          return 0;
        }
    }

  return rc;
}

MetaEis *
meta_eis_new (MetaBackend *backend)
{
  MetaEis *meta_eis;
  int fd;
  int rc;

  meta_eis = g_object_new (META_TYPE_EIS, NULL);
  meta_eis->backend = backend;

  meta_eis->eis = eis_new (meta_eis);
  rc = try_and_find_free_eis_socket (meta_eis);
  if (rc != 0)
    {
      g_warning ("Failed to initialize the EIS socket: %s", g_strerror (-rc));
      g_clear_pointer (&meta_eis->eis, eis_unref);
      return NULL;
    }

  eis_log_set_handler (meta_eis->eis, eis_logger);
  eis_log_set_priority (meta_eis->eis, EIS_LOG_PRIORITY_DEBUG);

  fd = eis_get_fd (meta_eis->eis);
  meta_eis->event_source = meta_event_source_new (meta_eis, fd, &eis_event_funcs);

  return meta_eis;
}

static void
meta_eis_init (MetaEis *meta_eis)
{
}

static void
meta_eis_constructed (GObject *object)
{
  MetaEis *meta_eis = META_EIS (object);

  meta_eis->eis_clients = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                 (GDestroyNotify) eis_client_unref,
                                                 (GDestroyNotify) g_object_unref);

  if (G_OBJECT_CLASS (meta_eis_parent_class)->constructed)
    G_OBJECT_CLASS (meta_eis_parent_class)->constructed (object);
}

static void
meta_eis_finalize (GObject *object)
{
  MetaEis *meta_eis = META_EIS (object);

  g_clear_pointer (&meta_eis->event_source, meta_event_source_free);
  g_clear_pointer (&meta_eis->eis, eis_unref);
  g_clear_pointer (&meta_eis->eis_clients, g_hash_table_destroy);

  G_OBJECT_CLASS (meta_eis_parent_class)->finalize (object);
}

static void
meta_eis_class_init (MetaEisClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_eis_constructed;
  object_class->finalize = meta_eis_finalize;
}
