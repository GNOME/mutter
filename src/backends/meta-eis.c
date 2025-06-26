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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <libeis.h>

#include "backends/meta-eis.h"
#include "backends/meta-eis-client.h"
#include "clutter/clutter-mutter.h"
#include "meta/util.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-input-thread.h"
#include "backends/native/meta-seat-native.h"
#endif

enum
{
  VIEWPORTS_CHANGED,

  N_SIGNALS
};

static int signals[N_SIGNALS];

typedef struct _MetaEventSource MetaEventSource;

struct _MetaEventSource
{
  GSource source;

  MetaEis *eis;
  GPollFD event_poll_fd;
};

struct _MetaEis
{
  GObject parent_instance;
  MetaBackend *backend;

  struct eis *eis;
  MetaEventSource *event_source;

  MetaEisDeviceTypes device_types;

  GList *viewports;

  GHashTable *eis_clients; /* eis_client => MetaEisClient */

  GCancellable *cancellable;
};

G_DEFINE_TYPE (MetaEis, meta_eis, G_TYPE_OBJECT)

MetaBackend *
meta_eis_get_backend (MetaEis *eis)
{
  return eis->backend;
}

static void
meta_eis_remove_client (MetaEis           *eis,
                        struct eis_client *eis_client)
{
  g_hash_table_remove (eis->eis_clients, eis_client);
}

static void
meta_eis_add_client (MetaEis           *eis,
                     struct eis_client *eis_client)
{
  MetaEisClient *client;

  client = meta_eis_client_new (eis, eis_client);

  g_hash_table_insert (eis->eis_clients,
                       eis_client_ref (eis_client),
                       client);
}

#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_EIS_EVENT_REF)
static gboolean
sync_callback_in_main (gpointer user_data)
{
  GTask *task = G_TASK (user_data);

  if (g_task_return_error_if_cancelled (task))
    return G_SOURCE_REMOVE;

  g_task_return_boolean (task, TRUE);
  return G_SOURCE_REMOVE;
}

static gboolean
sync_callback_in_impl (GTask *task)
{
  MetaEis *eis;
  ClutterSeat *seat;
  MetaSeatNative *seat_native;

  if (g_task_return_error_if_cancelled (task))
    return G_SOURCE_REMOVE;

  eis = g_task_get_source_object (task);
  seat = meta_backend_get_default_seat (eis->backend);
  seat_native = META_SEAT_NATIVE (seat);

  meta_seat_impl_queue_main_thread_idle (seat_native->impl,
                                         sync_callback_in_main,
                                         g_object_ref (task), g_object_unref);

  return G_SOURCE_REMOVE;
}
#endif /* defined(HAVE_NATIVE_BACKEND) && defined(HAVE_EIS_EVENT_REF) */

static void
process_event (MetaEis          *eis,
               struct eis_event *event)
{
  enum eis_event_type type = eis_event_get_type (event);
  struct eis_client *eis_client = eis_event_get_client (event);
  MetaEisClient *client;

  switch (type)
    {
    case EIS_EVENT_CLIENT_CONNECT:
      meta_eis_add_client (eis, eis_client);
      break;
    case EIS_EVENT_CLIENT_DISCONNECT:
      meta_eis_remove_client (eis, eis_client);
      break;
    case EIS_EVENT_SYNC:
#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_EIS_EVENT_REF)
      if (META_IS_BACKEND_NATIVE (eis->backend))
        {
          ClutterSeat *seat = meta_backend_get_default_seat (eis->backend);
          MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
          g_autoptr (GTask) task = NULL;

          /*
           * The sync is considered done when the last reference of the sync
           * event is released, so pass it via the input thread via a GTask to
           * make sure queued input events are processed before releasing it.
           */
          task = g_task_new (eis, eis->cancellable, NULL, NULL);
          g_task_set_task_data (task,
                                eis_event_ref (event),
                                (GDestroyNotify) eis_event_unref);
          meta_seat_impl_run_input_task (seat_native->impl, task,
                                         (GSourceFunc) sync_callback_in_impl);
        }
#endif /* defined(HAVE_NATIVE_BACKEND) && defined(HAVE_EIS_EVENT_REF) */
      break;
    default:
      client = g_hash_table_lookup (eis->eis_clients, eis_client);
      if (!client)
        {
          meta_topic (META_DEBUG_EIS, "Event for unknown EIS client: %s",
                      eis_client_get_name (eis_client));
          return;
        }
      meta_eis_client_process_event (client, event);
      break;
    }
}

static void
process_events (MetaEis *eis)
{
  struct eis_event *e;

  while ((e = eis_get_event (eis->eis)))
    {
      process_event (eis, e);
      eis_event_unref (e);
    }
}

static MetaEventSource *
meta_event_source_new (MetaEis      *eis,
                       int           fd,
                       GSourceFuncs *event_funcs)
{
  GSource *source;
  MetaEventSource *event_source;

  source = g_source_new (event_funcs, sizeof (MetaEventSource));
  g_source_set_name (source, "[mutter] EIS event source");
  event_source = (MetaEventSource *) source;

  /* setup the source */
  event_source->eis = eis;

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
  MetaEis *eis = source->eis;
  struct eis_event *e;

  *timeout_ms = -1;

  e = eis_peek_event (eis->eis);
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
  MetaEis *eis = source->eis;

  eis_dispatch (eis->eis);
  process_events (eis);

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
      g_critical ("%s", message);
      break;
    case EIS_LOG_PRIORITY_INFO:
    default:
      g_info ("%s", message);
      break;
    }
}

int
meta_eis_add_client_get_fd (MetaEis *eis)
{
  return eis_backend_fd_add_client (eis->eis);
}

MetaEis *
meta_eis_new (MetaBackend        *backend,
              MetaEisDeviceTypes  device_types)
{
  MetaEis *eis;
  int fd;

  eis = g_object_new (META_TYPE_EIS, NULL);
  eis->backend = backend;
  eis->device_types = device_types;

  eis->eis = eis_new (eis);
  eis_log_set_handler (eis->eis, eis_logger);
  eis_log_set_priority (eis->eis, EIS_LOG_PRIORITY_DEBUG);
  eis_setup_backend_fd (eis->eis);

  fd = eis_get_fd (eis->eis);
  eis->event_source = meta_event_source_new (eis, fd, &eis_event_funcs);

  return eis;
}

static void
meta_eis_init (MetaEis *eis)
{
  eis->eis_clients = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            (GDestroyNotify) eis_client_unref,
                                            (GDestroyNotify) g_object_unref);
  eis->cancellable = g_cancellable_new ();
}

static void
meta_eis_dispose (GObject *object)
{
  MetaEis *eis = META_EIS (object);

  g_cancellable_cancel (eis->cancellable);
  g_clear_object (&eis->cancellable);
  g_clear_pointer (&eis->viewports, g_list_free);
  g_clear_pointer (&eis->event_source, meta_event_source_free);
  g_clear_pointer (&eis->eis, eis_unref);
  g_clear_pointer (&eis->eis_clients, g_hash_table_destroy);

  G_OBJECT_CLASS (meta_eis_parent_class)->dispose (object);
}

static void
meta_eis_class_init (MetaEisClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_eis_dispose;

  signals[VIEWPORTS_CHANGED] =
    g_signal_new ("viewports-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

MetaEisDeviceTypes
meta_eis_get_device_types (MetaEis *eis)
{
  return eis->device_types;
}

static void
on_viewport_changed (MetaEisViewport *viewport,
                     MetaEis         *eis)
{
  g_signal_emit (eis, signals[VIEWPORTS_CHANGED], 0);
}

void
meta_eis_add_viewport (MetaEis         *eis,
                       MetaEisViewport *viewport)
{
  eis->viewports = g_list_append (eis->viewports, viewport);
  g_signal_emit (eis, signals[VIEWPORTS_CHANGED], 0);

  g_signal_connect (viewport, "viewport-changed",
                    G_CALLBACK (on_viewport_changed), eis);
}

void
meta_eis_remove_viewport (MetaEis         *eis,
                          MetaEisViewport *viewport)
{
  g_signal_handlers_disconnect_by_func (viewport, on_viewport_changed, eis);
  eis->viewports = g_list_remove (eis->viewports, viewport);
  g_signal_emit (eis, signals[VIEWPORTS_CHANGED], 0);
}

void
meta_eis_take_viewports (MetaEis *eis,
                         GList   *viewports)
{
  GList *l;

  for (l = viewports; l; l = l->next)
    {
      MetaEisViewport *viewport = l->data;

      g_signal_connect (viewport, "viewport-changed",
                        G_CALLBACK (on_viewport_changed), eis);
    }

  eis->viewports = g_list_concat (eis->viewports, viewports);
  g_signal_emit (eis, signals[VIEWPORTS_CHANGED], 0);
}

void
meta_eis_remove_all_viewports (MetaEis *eis)
{
  GList *l;

  for (l = eis->viewports; l; l = l->next)
    {
      MetaEisViewport *viewport = l->data;

      g_signal_handlers_disconnect_by_func (viewport, on_viewport_changed, eis);
    }

  g_clear_pointer (&eis->viewports, g_list_free);
  g_signal_emit (eis, signals[VIEWPORTS_CHANGED], 0);
}

GList *
meta_eis_peek_viewports (MetaEis *eis)
{
  return eis->viewports;
}
