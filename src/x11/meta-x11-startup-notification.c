/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Red Hat, Inc
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
#include "meta-x11-startup-notification.h"

#include <meta/meta-x11-errors.h>
#include "display-private.h"
#include "x11/meta-x11-display-private.h"
#include "startup-notification-private.h"

#ifdef HAVE_STARTUP_NOTIFICATION

enum {
  PROP_SEQ_X11_0,
  PROP_SEQ_X11_SEQ,
  N_SEQ_X11_PROPS
};

struct _MetaStartupSequenceX11 {
  MetaStartupSequence parent_instance;
  SnStartupSequence *seq;
};

static GParamSpec *seq_x11_props[N_SEQ_X11_PROPS];

G_DEFINE_TYPE (MetaStartupSequenceX11,
               meta_startup_sequence_x11,
               META_TYPE_STARTUP_SEQUENCE)

static void
meta_startup_sequence_x11_complete (MetaStartupSequence *seq)
{
  MetaStartupSequenceX11 *seq_x11;

  seq_x11 = META_STARTUP_SEQUENCE_X11 (seq);
  sn_startup_sequence_complete (seq_x11->seq);
}

static void
meta_startup_sequence_x11_finalize (GObject *object)
{
  MetaStartupSequenceX11 *seq;

  seq = META_STARTUP_SEQUENCE_X11 (object);
  sn_startup_sequence_unref (seq->seq);

  G_OBJECT_CLASS (meta_startup_sequence_x11_parent_class)->finalize (object);
}

static void
meta_startup_sequence_x11_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaStartupSequenceX11 *seq;

  seq = META_STARTUP_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      seq->seq = g_value_get_pointer (value);
      sn_startup_sequence_ref (seq->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_x11_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaStartupSequenceX11 *seq;

  seq = META_STARTUP_SEQUENCE_X11 (object);

  switch (prop_id)
    {
    case PROP_SEQ_X11_SEQ:
      g_value_set_pointer (value, seq->seq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_x11_init (MetaStartupSequenceX11 *seq)
{
}

static void
meta_startup_sequence_x11_class_init (MetaStartupSequenceX11Class *klass)
{
  MetaStartupSequenceClass *seq_class;
  GObjectClass *object_class;

  seq_class = META_STARTUP_SEQUENCE_CLASS (klass);
  seq_class->complete = meta_startup_sequence_x11_complete;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = meta_startup_sequence_x11_finalize;
  object_class->set_property = meta_startup_sequence_x11_set_property;
  object_class->get_property = meta_startup_sequence_x11_get_property;

  seq_x11_props[PROP_SEQ_X11_SEQ] =
    g_param_spec_pointer ("seq",
                          "Sequence",
                          "Sequence",
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_X11_PROPS,
                                     seq_x11_props);
}

static MetaStartupSequence *
meta_startup_sequence_x11_new (SnStartupSequence *seq)
{
  gint64 timestamp;

  timestamp = sn_startup_sequence_get_timestamp (seq) * 1000;
  return g_object_new (META_TYPE_STARTUP_SEQUENCE_X11,
                       "id", sn_startup_sequence_get_id (seq),
                       "timestamp", timestamp,
                       "seq", seq,
                       NULL);
}

static void
sn_error_trap_push (SnDisplay *sn_display,
                    Display   *xdisplay)
{
  MetaDisplay *display;
  display = meta_display_for_x_display (xdisplay);
  if (display != NULL)
    meta_x11_error_trap_push (display->x11_display);
}

static void
sn_error_trap_pop (SnDisplay *sn_display,
                   Display   *xdisplay)
{
  MetaDisplay *display;
  display = meta_display_for_x_display (xdisplay);
  if (display != NULL)
    meta_x11_error_trap_pop (display->x11_display);
}

static void
meta_startup_notification_sn_event (SnMonitorEvent *event,
                                    void           *user_data)
{
  MetaX11Display *x11_display = user_data;
  MetaStartupNotification *sn = x11_display->display->startup_notification;
  MetaStartupSequence *seq;
  SnStartupSequence *sequence;

  sequence = sn_monitor_event_get_startup_sequence (event);

  sn_startup_sequence_ref (sequence);

  switch (sn_monitor_event_get_type (event))
    {
    case SN_MONITOR_EVENT_INITIATED:
      {
        const char *wmclass;

        wmclass = sn_startup_sequence_get_wmclass (sequence);

        meta_topic (META_DEBUG_STARTUP,
                    "Received startup initiated for %s wmclass %s\n",
                    sn_startup_sequence_get_id (sequence),
                    wmclass ? wmclass : "(unset)");

        seq = meta_startup_sequence_x11_new (sequence);
        meta_startup_notification_add_sequence (sn, seq);
        g_object_unref (seq);
      }
      break;

    case SN_MONITOR_EVENT_COMPLETED:
      {
        meta_topic (META_DEBUG_STARTUP,
                    "Received startup completed for %s\n",
                    sn_startup_sequence_get_id (sequence));

        seq = meta_startup_notification_lookup_sequence (sn, sn_startup_sequence_get_id (sequence));
        if (seq)
          meta_startup_notification_remove_sequence (sn, seq);
      }
      break;

    case SN_MONITOR_EVENT_CHANGED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup changed for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;

    case SN_MONITOR_EVENT_CANCELED:
      meta_topic (META_DEBUG_STARTUP,
                  "Received startup canceled for %s\n",
                  sn_startup_sequence_get_id (sequence));
      break;
    }

  sn_startup_sequence_unref (sequence);
}
#endif

void
meta_x11_startup_notification_init (MetaX11Display *x11_display)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  x11_display->sn_display = sn_display_new (x11_display->xdisplay,
                                            sn_error_trap_push,
                                            sn_error_trap_pop);
  x11_display->sn_context =
    sn_monitor_context_new (x11_display->sn_display,
                            meta_x11_display_get_screen_number (x11_display),
                            meta_startup_notification_sn_event,
                            x11_display,
                            NULL);
#endif
}

void
meta_x11_startup_notification_close (MetaX11Display *x11_display)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  g_clear_pointer (&x11_display->sn_context,
                   (GDestroyNotify) sn_monitor_context_unref);
  g_clear_pointer (&x11_display->sn_display, (GDestroyNotify) sn_display_unref);
#endif
}

gboolean
meta_x11_startup_notification_handle_xevent (MetaX11Display *x11_display,
                                             XEvent         *xevent)
{
#ifdef HAVE_STARTUP_NOTIFICATION
  return sn_display_process_event (x11_display->sn_display, xevent);
#endif
  return FALSE;
}
