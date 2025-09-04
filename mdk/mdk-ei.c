/*
 * Copyright (C) 2024 Red Hat Inc.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-ei.h"

#include <libei.h>
#include <gio/gio.h>

#include "mdk-seat.h"

struct _MdkEi
{
  GObject parent;

  MdkSession *session;

  struct ei *ei;

  GHashTable *seats;
  MdkSeat *default_seat;

  GSource *source;
};

typedef struct _MdkEiSource
{
  GSource base;

  MdkEi *ei;
} MdkEiSource;

G_DEFINE_FINAL_TYPE (MdkEi, mdk_ei, G_TYPE_OBJECT)

static void
process_events (MdkEi *ei)
{
  struct ei_event *ei_event;

  while ((ei_event = ei_get_event (ei->ei)))
    {
      enum ei_event_type ei_event_type = ei_event_get_type (ei_event);

      g_debug ("Received event type %s",
               ei_event_type_to_string (ei_event_type));

      switch (ei_event_type)
        {
        case EI_EVENT_CONNECT:
        case EI_EVENT_DISCONNECT:
          break;
        case EI_EVENT_SEAT_ADDED:
            {
              struct ei_seat *ei_seat;
              MdkSeat *seat;

              ei_seat = ei_event_get_seat (ei_event);

              g_debug ("Adding seat %s", ei_seat_get_name (ei_seat));

              seat = mdk_seat_new (ei, ei_seat);
              g_hash_table_insert (ei->seats, ei_seat, seat);
              if (!ei->default_seat)
                ei->default_seat = seat;

              break;
            }
        case EI_EVENT_SEAT_REMOVED:
          {
            struct ei_seat *ei_seat = ei_event_get_seat (ei_event);
            g_autoptr (MdkSeat) seat = NULL;

            g_debug ("Removing seat %s", ei_seat_get_name (ei_seat));

            g_hash_table_steal_extended (ei->seats,
                                         ei_seat,
                                         NULL,
                                         (gpointer *) &seat);
            g_assert (seat);

            if (ei->default_seat == seat)
              {
                g_autoptr (GList) seats = NULL;
                GList *el;

                seats = g_hash_table_get_values (ei->seats);
                el = g_list_first (seats);
                if (el)
                  ei->default_seat = el->data;
                else
                  ei->default_seat = NULL;
              }
            break;
          }
        case EI_EVENT_DEVICE_ADDED:
        case EI_EVENT_DEVICE_REMOVED:
        case EI_EVENT_DEVICE_RESUMED:
        case EI_EVENT_DEVICE_PAUSED:
          {
            MdkSeat *seat;

            seat = g_hash_table_lookup (ei->seats, ei_event_get_seat (ei_event));
            mdk_seat_process_event (seat, ei_event);
            break;
          }
        default:
          break;
        }

      ei_event_unref (ei_event);
    }
}

static gboolean
ei_source_prepare (GSource *source,
                   int     *timeout)
{
  MdkEiSource *ei_source = (MdkEiSource *) source;
  MdkEi *ei = ei_source->ei;

  *timeout = -1;

  return !!ei_peek_event (ei->ei);
}

static gboolean
ei_source_dispatch (GSource     *source,
                    GSourceFunc  callback,
                    gpointer     user_data)
{
  MdkEiSource *ei_source = (MdkEiSource *) source;
  MdkEi *ei = ei_source->ei;

  ei_dispatch (ei->ei);
  process_events (ei);

  return TRUE;
}

static GSourceFuncs ei_source_funcs =
{
  .prepare = ei_source_prepare,
  .dispatch = ei_source_dispatch,
};

static GSource *
create_ei_source (MdkEi *ei)
{
  GSource *source;
  MdkEiSource *ei_source;

  source = g_source_new (&ei_source_funcs,
                         sizeof (MdkEiSource));
  ei_source = (MdkEiSource *) source;
  ei_source->ei = ei;

  g_source_add_unix_fd (source,
                        ei_get_fd (ei->ei),
                        G_IO_IN | G_IO_ERR);

  return source;
}

static void
mdk_ei_finalize (GObject *object)
{
  MdkEi *ei = MDK_EI (object);

  g_clear_pointer (&ei->source, g_source_destroy);
  g_clear_pointer (&ei->seats, g_hash_table_unref);
  g_clear_pointer (&ei->ei, ei_unref);

  G_OBJECT_CLASS (mdk_ei_parent_class)->finalize (object);
}

static void
mdk_ei_class_init (MdkEiClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_ei_finalize;
}

static void
mdk_ei_init (MdkEi *ei)
{
  ei->seats = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
}

MdkEi *
mdk_ei_new (MdkSession  *session,
            int          fd,
            GError     **error)
{
  MdkEi *ei;
  int ret;

  ei = g_object_new (MDK_TYPE_EI, NULL);
  ei->session = session;
  ei->ei = ei_new_sender (ei);
  ei_configure_name (ei->ei, "mutter-devkit");

  ret = ei_setup_backend_fd (ei->ei, fd);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "Failed to setup libei backend: %s", g_strerror (-ret));
      return FALSE;
    }

  ei->source = create_ei_source (ei);
  g_source_attach (ei->source, NULL);
  g_source_unref (ei->source);

  g_debug ("Waiting for default libei seat");
  while (!ei->default_seat)
    {
      ei_dispatch (ei->ei);
      process_events (ei);
    }

  return ei;
}

void
mdk_ei_dispatch (MdkEi *ei)
{
  if (ei_peek_event (ei->ei))
    {
      process_events (ei);
    }
  else
    {
      ei_dispatch (ei->ei);
      process_events (ei);
    }
}

MdkSeat *
mdk_ei_get_default_seat (MdkEi *ei)
{
  return ei->default_seat;
}
