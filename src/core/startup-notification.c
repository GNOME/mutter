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

/* This should be fairly long, as it should never be required unless
 * apps or .desktop files are buggy, and it's confusing if
 * OpenOffice or whatever seems to stop launching - people
 * might decide they need to launch it again.
 */
#define STARTUP_TIMEOUT_MS 15000
#define UPDATE_CURSOR_TIMEOUT_MS 20

enum
{
  PROP_0,
  PROP_DISPLAY,
  N_PROPS
};

enum
{
  PROP_SEQ_0,
  PROP_SEQ_DISPLAY,
  PROP_SEQ_ID,
  PROP_SEQ_TIMESTAMP,
  PROP_SEQ_ICON_NAME,
  PROP_SEQ_APPLICATION_ID,
  PROP_SEQ_WMCLASS,
  PROP_SEQ_WORKSPACE,
  PROP_SEQ_NAME,
  N_SEQ_PROPS
};

enum
{
  SEQ_COMPLETE,
  SEQ_TIMEOUT,
  N_SEQ_SIGNALS
};

enum
{
  CHANGED,
  N_SIGNALS
};

static guint sn_signals[N_SIGNALS];
static GParamSpec *sn_props[N_PROPS];
static guint seq_signals[N_SEQ_SIGNALS];
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
  guint startup_sequence_timeout_id;
  guint update_cursor_timeout_id;
  MetaCursor cursor;
};

typedef struct
{
  MetaDisplay *display;

  char *wmclass;
  char *name;
  char *application_id;
  char *icon_name;
  char *id;
  uint64_t timestamp;
  int workspace;
  uint completed : 1;
} MetaStartupSequencePrivate;

G_DEFINE_TYPE (MetaStartupNotification,
               meta_startup_notification,
               G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (MetaStartupSequence,
                            meta_startup_sequence,
                            G_TYPE_OBJECT)


static void meta_startup_notification_ensure_timeout  (MetaStartupNotification *sn);

static gboolean
meta_startup_notification_has_pending_sequences (MetaStartupNotification *sn)
{
  GSList *l;

  for (l = sn->startup_sequences; l; l = l->next)
    {
      if (!meta_startup_sequence_get_completed (l->data))
        return TRUE;
    }

  return FALSE;
}

static void
meta_startup_notification_update_cursor (MetaStartupNotification *sn)
{
  MetaDisplay *display = sn->display;
  MetaCursor cursor;

  if (meta_startup_notification_has_pending_sequences (sn))
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting busy cursor");
      cursor = META_CURSOR_BUSY;
    }
  else
    {
      meta_topic (META_DEBUG_STARTUP,
                  "Setting default cursor");
      cursor = META_CURSOR_DEFAULT;
    }

  if (sn->cursor != cursor)
    {
      meta_display_set_cursor (display, cursor);
      sn->cursor = cursor;
    }
}

static gboolean
meta_startup_notification_cursor_timeout (gpointer user_data)
{
  MetaStartupNotification *sn = user_data;

  meta_startup_notification_update_cursor (sn);
  sn->update_cursor_timeout_id = 0;

  return G_SOURCE_REMOVE;
}

static void
meta_startup_notification_update_feedback (MetaStartupNotification *sn)
{
  if (sn->update_cursor_timeout_id)
    return;

  meta_startup_notification_update_cursor (sn);
  sn->update_cursor_timeout_id =
    g_timeout_add (UPDATE_CURSOR_TIMEOUT_MS,
                   meta_startup_notification_cursor_timeout,
                   sn);
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
  g_free (priv->wmclass);
  g_free (priv->name);
  g_free (priv->application_id);
  g_free (priv->icon_name);

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
    case PROP_SEQ_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_SEQ_ID:
      priv->id = g_value_dup_string (value);
      break;
    case PROP_SEQ_TIMESTAMP:
      priv->timestamp = g_value_get_uint64 (value);
      break;
    case PROP_SEQ_ICON_NAME:
      priv->icon_name = g_value_dup_string (value);
      break;
    case PROP_SEQ_APPLICATION_ID:
      priv->application_id = g_value_dup_string (value);
      break;
    case PROP_SEQ_WMCLASS:
      priv->wmclass = g_value_dup_string (value);
      break;
    case PROP_SEQ_WORKSPACE:
      priv->workspace = g_value_get_int (value);
      break;
    case PROP_SEQ_NAME:
      priv->name = g_value_dup_string (value);
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
    case PROP_SEQ_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    case PROP_SEQ_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_SEQ_TIMESTAMP:
      g_value_set_uint64 (value, priv->timestamp);
      break;
    case PROP_SEQ_ICON_NAME:
      g_value_set_string (value, priv->icon_name);
      break;
    case PROP_SEQ_APPLICATION_ID:
      g_value_set_string (value, priv->application_id);
      break;
    case PROP_SEQ_WMCLASS:
      g_value_set_string (value, priv->wmclass);
      break;
    case PROP_SEQ_WORKSPACE:
      g_value_set_int (value, priv->workspace);
      break;
    case PROP_SEQ_NAME:
      g_value_set_string (value, priv->name);
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

  seq_signals[SEQ_COMPLETE] =
    g_signal_new ("complete",
                  META_TYPE_STARTUP_SEQUENCE,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaStartupSequenceClass, complete),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  seq_signals[SEQ_TIMEOUT] =
    g_signal_new ("timeout",
                  META_TYPE_STARTUP_SEQUENCE,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  seq_props[PROP_SEQ_DISPLAY] =
    g_param_spec_object ("display", NULL, NULL,
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_ID] =
    g_param_spec_string ("id", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_TIMESTAMP] =
    g_param_spec_uint64 ("timestamp", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_ICON_NAME] =
    g_param_spec_string ("icon-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_APPLICATION_ID] =
    g_param_spec_string ("application-id", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_WMCLASS] =
    g_param_spec_string ("wmclass", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_WORKSPACE] =
    g_param_spec_int ("workspace", NULL, NULL,
                      G_MININT, G_MAXINT, -1,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);
  seq_props[PROP_SEQ_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_SEQ_PROPS, seq_props);
}

const char *
meta_startup_sequence_get_id (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->id;
}

uint64_t
meta_startup_sequence_get_timestamp (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), 0);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->timestamp;
}

void
meta_startup_sequence_complete (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_if_fail (META_IS_STARTUP_SEQUENCE (seq));

  priv = meta_startup_sequence_get_instance_private (seq);
  if (priv->completed)
    return;

  priv->completed = TRUE;
  g_signal_emit (seq, seq_signals[SEQ_COMPLETE], 0);
}

gboolean
meta_startup_sequence_get_completed (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), FALSE);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->completed;
}

const char *
meta_startup_sequence_get_name (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->name;
}

int
meta_startup_sequence_get_workspace (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), 0);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->workspace;
}

/**
 * meta_startup_sequence_get_icon_name:
 * @seq: a #MetaStartupSequence
 *
 * Get the icon name of the startup sequence.
 *
 * Returns: (nullable): the icon name or %NULL.
 **/
const char *
meta_startup_sequence_get_icon_name (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->icon_name;
}

/**
 * meta_startup_sequence_get_application_id:
 * @seq: a #MetaStartupSequence
 *
 * Get the application id of the startup sequence.
 *
 * Returns: (nullable): the application id or %NULL.
 **/
const char *
meta_startup_sequence_get_application_id (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->application_id;
}

/**
 * meta_startup_sequence_get_wmclass:
 * @seq: a #MetaStartupSequence
 *
 * Get the wmclass of the startup sequence.
 *
 * Returns: (nullable): the wmclass or %NULL.
 **/
const char *
meta_startup_sequence_get_wmclass (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->wmclass;
}

MetaDisplay *
meta_startup_sequence_get_display (MetaStartupSequence *seq)
{
  MetaStartupSequencePrivate *priv;

  g_return_val_if_fail (META_IS_STARTUP_SEQUENCE (seq), NULL);

  priv = meta_startup_sequence_get_instance_private (seq);
  return priv->display;
}

static void
on_sequence_completed (MetaStartupSequence     *seq,
                       MetaStartupNotification *sn)
{
  meta_startup_notification_update_feedback (sn);
  g_signal_emit (sn, sn_signals[CHANGED], 0, seq);
}

void
meta_startup_notification_add_sequence (MetaStartupNotification *sn,
                                        MetaStartupSequence     *seq)
{
  sn->startup_sequences = g_slist_prepend (sn->startup_sequences,
                                           g_object_ref (seq));
  g_signal_connect (seq, "complete",
                    G_CALLBACK (on_sequence_completed), sn);

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
              "Sequence used %" G_GINT64_FORMAT " ms vs. %d max: %s",
              elapsed, STARTUP_TIMEOUT_MS,
              meta_startup_sequence_get_id (sequence));

  if (elapsed > STARTUP_TIMEOUT_MS)
    ctod->list = g_slist_prepend (ctod->list, sequence);
}

static gboolean
startup_sequence_timeout (void *data)
{
  MetaStartupNotification *sn = data;
  CollectTimedOutData ctod;
  GSList *l;

  ctod.list = NULL;
  ctod.now = g_get_monotonic_time () / 1000;
  g_slist_foreach (sn->startup_sequences,
                   collect_timed_out_foreach,
                   &ctod);

  for (l = ctod.list; l != NULL; l = l->next)
    {
      MetaStartupSequence *sequence = l->data;

      meta_topic (META_DEBUG_STARTUP,
                  "Timed out sequence %s",
                  meta_startup_sequence_get_id (sequence));

      if (!meta_startup_sequence_get_completed (sequence))
        {
          g_signal_emit (sequence, seq_signals[SEQ_TIMEOUT], 0, sequence);
          meta_startup_sequence_complete (sequence);
        }

      meta_startup_notification_remove_sequence (sn, sequence);
    }

  g_slist_free (ctod.list);

  if (sn->startup_sequences != NULL)
    {
      return G_SOURCE_CONTINUE;
    }
  else
    {
      sn->startup_sequence_timeout_id = 0;
      return G_SOURCE_REMOVE;
    }
}

static void
meta_startup_notification_ensure_timeout (MetaStartupNotification *sn)
{
  if (sn->startup_sequence_timeout_id != 0)
    return;

  /* our timeout just polls every second, instead of bothering
   * to compute exactly when we may next time out
   */
  sn->startup_sequence_timeout_id =
    g_timeout_add_seconds (1, startup_sequence_timeout, sn);
  g_source_set_name_by_id (sn->startup_sequence_timeout_id,
                           "[mutter] startup_sequence_timeout");
}

void
meta_startup_notification_remove_sequence (MetaStartupNotification *sn,
                                           MetaStartupSequence     *seq)
{
  sn->startup_sequences = g_slist_remove (sn->startup_sequences, seq);
  meta_startup_notification_update_feedback (sn);

  g_signal_handlers_disconnect_by_func (seq, on_sequence_completed, sn);

  if (sn->startup_sequences == NULL)
    g_clear_handle_id (&sn->startup_sequence_timeout_id, g_source_remove);

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
  sn->startup_sequences = NULL;
  sn->startup_sequence_timeout_id = 0;
}

static void
meta_startup_notification_finalize (GObject *object)
{
  MetaStartupNotification *sn = META_STARTUP_NOTIFICATION (object);

  g_clear_handle_id (&sn->startup_sequence_timeout_id, g_source_remove);
  g_clear_handle_id (&sn->update_cursor_timeout_id, g_source_remove);

  g_slist_free_full (sn->startup_sequences, g_object_unref);
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
meta_startup_notification_class_init (MetaStartupNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_startup_notification_finalize;
  object_class->set_property = meta_startup_notification_set_property;
  object_class->get_property = meta_startup_notification_get_property;

  sn_props[PROP_DISPLAY] =
    g_param_spec_object ("display", NULL, NULL,
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  sn_signals[CHANGED] =
    g_signal_new ("changed",
                  META_TYPE_STARTUP_NOTIFICATION,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, META_TYPE_STARTUP_SEQUENCE);

  g_object_class_install_properties (object_class, N_PROPS, sn_props);
}

MetaStartupNotification *
meta_startup_notification_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_STARTUP_NOTIFICATION,
                       "display", display,
                       NULL);
}

/**
 * meta_startup_notification_get_sequences:
 * @sn: a #MetaStartupNotification
 *
 * Get the list of startup sequences arrived in the startup notification object.
 *
 * Returns: (element-type MetaStartupSequence) (transfer none): a #GSList of
 * #MetaStartupSequence in the #MetaStartupNotification.
 **/
GSList *
meta_startup_notification_get_sequences (MetaStartupNotification *sn)
{
  return sn->startup_sequences;
}

/**
 * meta_startup_notification_create_launcher:
 * @sn: a #MetaStartupNotification
 *
 * Creates an app launch context.
 *
 * Returns: (transfer full): a launch context.
 **/
MetaLaunchContext *
meta_startup_notification_create_launcher (MetaStartupNotification *sn)
{
  return g_object_new (META_TYPE_LAUNCH_CONTEXT,
                       "display", sn->display,
                       NULL);
}
