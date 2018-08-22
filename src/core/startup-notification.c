/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001, 2002 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat Inc.
 * Some ICCCM manager selection code derived from fvwm2,
 * Copyright (C) 2001 Dominik Vogt, Matthias Clasen, and fvwm2 team
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

#include <glib-object.h>

#include "core/display-private.h"
#include "core/startup-notification-private.h"
#include "core/util-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

/* This should be fairly long, as it should never be required unless
 * apps or .desktop files are buggy, and it's confusing if
 * OpenOffice or whatever seems to stop launching - people
 * might decide they need to launch it again.
 */
#define STARTUP_TIMEOUT 15000000

enum {
  PROP_0,
  PROP_DISPLAY,
  N_PROPS
};

enum {
  PROP_SEQ_0,
  PROP_SEQ_ID,
  PROP_SEQ_TIMESTAMP,
  N_SEQ_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint sn_signals[N_SIGNALS];
static GParamSpec *sn_props[N_PROPS];
static GParamSpec *seq_props[N_SEQ_PROPS];

typedef struct
{
  GSList *list;
  gint64 now;
} CollectTimedOutData;

struct _MetaStartupNotification
{
  GObject parent_instance;
  MetaDisplay *display;


  GSList *startup_sequences;
  guint startup_sequence_timeout;
};

typedef struct {
  gchar *id;
  gint64 timestamp;
} MetaStartupSequencePrivate;

G_DEFINE_TYPE (MetaStartupNotification,
               meta_startup_notification,
               G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (MetaStartupSequence,
                            meta_startup_sequence,
                            G_TYPE_OBJECT)


static void meta_startup_notification_ensure_timeout  (MetaStartupNotification *sn);

static void
meta_startup_notification_update_feedback (MetaStartupNotification *sn)
{
  MetaDisplay *display = sn->display;

  if (sn->startup_sequences != NULL)
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting busy cursor\n");
      meta_display_set_cursor (display, META_CURSOR_BUSY);
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting default cursor\n");
      meta_display_set_cursor (display, META_CURSOR_DEFAULT);
    }
}

static void
meta_startup_sequence_init (MetaStartupSequence *seq)
{
}

static void
meta_startup_sequence_finalize (GObject *object)
{
  MetaStartupSequence *seq;
  MetaStartupSequencePrivate *priv;

  seq = META_STARTUP_SEQUENCE (object);
  priv = meta_startup_sequence_get_instance_private (seq);
  g_free (priv->id);

  G_OBJECT_CLASS (meta_startup_sequence_parent_class)->finalize (object);
}

static void
meta_startup_sequence_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  MetaStartupSequence *seq;
  MetaStartupSequencePrivate *priv;

  seq = META_STARTUP_SEQUENCE (object);
  priv = meta_startup_sequence_get_instance_private (seq);

  switch (prop_id)
    {
    case PROP_SEQ_ID:
      priv->id = g_value_dup_string (value);
      break;
    case PROP_SEQ_TIMESTAMP:
      priv->timestamp = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  MetaStartupSequence *seq;
  MetaStartupSequencePrivate *priv;

  seq = META_STARTUP_SEQUENCE (object);
  priv = meta_startup_sequence_get_instance_private (seq);

  switch (prop_id)
    {
    case PROP_SEQ_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_SEQ_TIMESTAMP:
      g_value_set_int64 (value, priv->timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_sequence_class_init (MetaStartupSequenceClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = meta_startup_sequence_finalize;
  object_class->set_property = meta_startup_sequence_set_property;
  object_class->get_property = meta_startup_sequence_get_property;

  seq_props[PROP_SEQ_ID] =
    g_param_spec_string ("id",
                         "ID",
                         "ID",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_TIMESTAMP] =
    g_param_spec_int64 ("timestamp",
                        "Timestamp",
                        "Timestamp",
                        G_MININT64, G_MAXINT64, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_PROPS, seq_props);
}

static const gchar *
meta_startup_sequence_get_id (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->id;
}

static gint64
meta_startup_sequence_get_timestamp (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->timestamp;
}

static void
meta_startup_sequence_complete (MetaStartupSequence *seq)
{
  MetaStartupSequenceClass *klass;

  klass = META_STARTUP_SEQUENCE_GET_CLASS (seq);

  if (klass->complete)
    klass->complete (seq);
}

void
meta_startup_notification_add_sequence (MetaStartupNotification *sn,
                                        MetaStartupSequence     *seq)
{
  sn->startup_sequences = g_slist_prepend (sn->startup_sequences,
                                           g_object_ref (seq));

  meta_startup_notification_ensure_timeout (sn);
  meta_startup_notification_update_feedback (sn);

  g_signal_emit (sn, sn_signals[CHANGED], 0, seq);
}

static void
collect_timed_out_foreach (void *element,
                           void *data)
{
  MetaStartupSequence *sequence = element;
  CollectTimedOutData *ctod = data;
  gint64 elapsed, timestamp;

  timestamp = meta_startup_sequence_get_timestamp (sequence);
  elapsed = ctod->now - timestamp;

  meta_topic (META_DEBUG_STARTUP,
              "Sequence used %" G_GINT64_FORMAT " ms vs. %d max: %s\n",
              elapsed, STARTUP_TIMEOUT,
              meta_startup_sequence_get_id (sequence));

  if (elapsed > STARTUP_TIMEOUT)
    ctod->list = g_slist_prepend (ctod->list, sequence);
}

static gboolean
startup_sequence_timeout (void *data)
{
  MetaStartupNotification *sn = data;
  CollectTimedOutData ctod;
  GSList *l;

  ctod.list = NULL;
  ctod.now = g_get_monotonic_time ();
  g_slist_foreach (sn->startup_sequences,
                   collect_timed_out_foreach,
                   &ctod);

  for (l = ctod.list; l != NULL; l = l->next)
    {
      MetaStartupSequence *sequence = l->data;

      meta_topic (META_DEBUG_STARTUP,
                  "Timed out sequence %s\n",
                  meta_startup_sequence_get_id (sequence));

      meta_startup_sequence_complete (sequence);
    }

  g_slist_free (ctod.list);

  if (sn->startup_sequences != NULL)
    {
      return TRUE;
    }
  else
    {
      /* remove */
      sn->startup_sequence_timeout = 0;
      return FALSE;
    }
}

static void
meta_startup_notification_ensure_timeout (MetaStartupNotification *sn)
{
  if (sn->startup_sequence_timeout != 0)
    return;

  /* our timeout just polls every second, instead of bothering
   * to compute exactly when we may next time out
   */
  sn->startup_sequence_timeout = g_timeout_add_seconds (1,
                                                        startup_sequence_timeout,
                                                        sn);
  g_source_set_name_by_id (sn->startup_sequence_timeout,
                           "[mutter] startup_sequence_timeout");
}

void
meta_startup_notification_remove_sequence (MetaStartupNotification *sn,
                                           MetaStartupSequence     *seq)
{
  sn->startup_sequences = g_slist_remove (sn->startup_sequences, seq);
  meta_startup_notification_update_feedback (sn);

  if (sn->startup_sequences == NULL &&
      sn->startup_sequence_timeout != 0)
    {
      g_source_remove (sn->startup_sequence_timeout);
      sn->startup_sequence_timeout = 0;
    }

  g_signal_emit (sn, sn_signals[CHANGED], 0, seq);
  g_object_unref (seq);
}

MetaStartupSequence *
meta_startup_notification_lookup_sequence (MetaStartupNotification *sn,
                                           const gchar             *id)
{
  MetaStartupSequence *seq;
  const gchar *seq_id;
  GSList *l;

  for (l = sn->startup_sequences; l; l = l->next)
    {
      seq = l->data;
      seq_id = meta_startup_sequence_get_id (seq);

      if (g_str_equal (seq_id, id))
        return l->data;
    }

  return NULL;
}

static void
meta_startup_notification_init (MetaStartupNotification *sn)
{
}

static void
meta_startup_notification_finalize (GObject *object)
{
  MetaStartupNotification *sn = META_STARTUP_NOTIFICATION (object);

  if (sn->startup_sequence_timeout)
    g_source_remove (sn->startup_sequence_timeout);

  g_slist_foreach (sn->startup_sequences, (GFunc) g_object_unref, NULL);
  g_slist_free (sn->startup_sequences);
  sn->startup_sequences = NULL;

  G_OBJECT_CLASS (meta_startup_notification_parent_class)->finalize (object);
}

static void
meta_startup_notification_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaStartupNotification *sn = META_STARTUP_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      sn->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_notification_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaStartupNotification *sn = META_STARTUP_NOTIFICATION (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, sn->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_startup_notification_constructed (GObject *object)
{
  MetaStartupNotification *sn = META_STARTUP_NOTIFICATION (object);

  g_assert (sn->display != NULL);

  sn->startup_sequences = NULL;
  sn->startup_sequence_timeout = 0;
}

static void
meta_startup_notification_class_init (MetaStartupNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_startup_notification_constructed;
  object_class->finalize = meta_startup_notification_finalize;
  object_class->set_property = meta_startup_notification_set_property;
  object_class->get_property = meta_startup_notification_get_property;

  sn_props[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "Display",
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  sn_signals[CHANGED] =
    g_signal_new ("changed",
                  META_TYPE_STARTUP_NOTIFICATION,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  g_object_class_install_properties (object_class, N_PROPS, sn_props);
}

MetaStartupNotification *
meta_startup_notification_get (MetaDisplay *display)
{
  static MetaStartupNotification *notification = NULL;

  if (!notification)
    notification = g_object_new (META_TYPE_STARTUP_NOTIFICATION,
                                 "display", display,
                                 NULL);

  return notification;
}

GSList *
meta_startup_notification_get_sequences (MetaStartupNotification *sn)
{
  return sn->startup_sequences;
}
