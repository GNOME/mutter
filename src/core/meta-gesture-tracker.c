/*
 * Copyright (C) 2014 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * SECTION:gesture-tracker
 * @Title: MetaGestureTracker
 * @Short_Description: Manages gestures on windows/desktop
 *
 * Forwards touch events to clutter actors, and accepts/rejects touch sequences
 * based on the outcome of those.
 */

#include "config.h"

#include "core/meta-gesture-tracker-private.h"

#include "compositor/meta-surface-actor.h"
#include "meta/compositor-mutter.h"
#include "meta/util.h"
#include "core/display-private.h"

#define DISTANCE_THRESHOLD 30

typedef struct _MetaGestureTrackerPrivate MetaGestureTrackerPrivate;
typedef struct _MetaSequenceInfo MetaSequenceInfo;

struct _MetaSequenceInfo
{
  MetaGestureTracker *tracker;
  ClutterEventSequence *sequence;
  ClutterActor *source_actor;
  gboolean notified_x11;
  guint autodeny_timeout_id;
  gfloat start_x;
  gfloat start_y;
};

struct _MetaGestureTrackerPrivate
{
  GHashTable *sequences; /* Hashtable of ClutterEventSequence->MetaSequenceInfo */
  GArray *stage_gestures; /* Array of ClutterGestureAction* */

  guint autodeny_timeout;
  gboolean gesture_in_progress;
  guint actions_changed_id;

  ClutterActor *window_group;
  ClutterActor *top_window_group;
};

enum {
  PROP_0,
  PROP_AUTODENY_TIMEOUT,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum {
  STATE_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

#define DEFAULT_AUTODENY_TIMEOUT 150

G_DEFINE_TYPE_WITH_PRIVATE (MetaGestureTracker, meta_gesture_tracker, G_TYPE_OBJECT)

static void
meta_gesture_tracker_finalize (GObject *object)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  g_array_free (priv->stage_gestures, TRUE);

  if (!meta_is_wayland_compositor ())
    g_hash_table_destroy (priv->sequences);

  G_OBJECT_CLASS (meta_gesture_tracker_parent_class)->finalize (object);
}

static void
meta_gesture_tracker_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      priv->autodeny_timeout = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_gesture_tracker_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (META_GESTURE_TRACKER (object));

  switch (prop_id)
    {
    case PROP_AUTODENY_TIMEOUT:
      g_value_set_uint (value, priv->autodeny_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_gesture_tracker_class_init (MetaGestureTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_gesture_tracker_finalize;
  object_class->set_property = meta_gesture_tracker_set_property;
  object_class->get_property = meta_gesture_tracker_get_property;

  obj_props[PROP_AUTODENY_TIMEOUT] = g_param_spec_uint ("autodeny-timeout",
                                                        "Auto-deny timeout",
                                                        "Auto-deny timeout",
                                                        0, G_MAXUINT, DEFAULT_AUTODENY_TIMEOUT,
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  signals[STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MetaGestureTrackerClass, state_changed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
}

static void
set_sequence_accepted (MetaSequenceInfo *info, gboolean accepted)
{
  MetaGestureTrackerPrivate *priv;
  priv = meta_gesture_tracker_get_instance_private (info->tracker);

  if (!info->notified_x11)
    {
      info->notified_x11 = TRUE;

      /* Only reject sequences when their target actor actually is a window,
       * all the other actors are part of the shell and get their events
       * directly from Clutter.
       */
      if (!accepted &&
          !clutter_actor_contains (priv->window_group, info->source_actor) &&
          !clutter_actor_contains (priv->top_window_group, info->source_actor))
        return;

      g_signal_emit (info->tracker, signals[STATE_CHANGED], 0, info->sequence, accepted);
    }
}

static gboolean
autodeny_sequence (gpointer user_data)
{
  MetaSequenceInfo *info = user_data;
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (info->tracker);

  if (priv->gesture_in_progress)
    {
      info->autodeny_timeout_id = 0;
      return G_SOURCE_REMOVE;
    }

  set_sequence_accepted (info, FALSE);

  info->autodeny_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static MetaSequenceInfo *
meta_sequence_info_new (MetaGestureTracker *tracker,
                        const ClutterEvent *event)
{
  MetaGestureTrackerPrivate *priv;
  MetaSequenceInfo *info;
  guint ms;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  ms = priv->autodeny_timeout;

  info = g_slice_new0 (MetaSequenceInfo);
  info->tracker = tracker;
  info->sequence = event->touch.sequence;
  info->source_actor = clutter_event_get_source (event);
  info->notified_x11 = FALSE;
  info->autodeny_timeout_id = g_timeout_add (ms, autodeny_sequence, info);

  clutter_event_get_coords (event, &info->start_x, &info->start_y);

  return info;
}

static void
meta_sequence_info_free (MetaSequenceInfo *info)
{
  if (info->autodeny_timeout_id)
    g_source_remove (info->autodeny_timeout_id);

  set_sequence_accepted (info, FALSE);

  g_slice_free (MetaSequenceInfo, info);
}

static void
on_grab_op (MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;
  gint i;

  priv = meta_gesture_tracker_get_instance_private (tracker);

  for (i = 0; i < priv->stage_gestures->len; i++)
    {
      ClutterGestureAction *gesture = g_array_index (priv->stage_gestures, ClutterGestureAction*, i);
      clutter_gesture_action_reset (gesture);
    }

  g_hash_table_remove_all (priv->sequences);

  priv->gesture_in_progress = FALSE;
}

static void
meta_gesture_tracker_init (MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (tracker);

  priv->stage_gestures = g_array_new (FALSE, FALSE, sizeof (ClutterGestureAction*));
  priv->actions_changed_id = 0;

  if (!meta_is_wayland_compositor ())
    {
      priv->sequences = g_hash_table_new_full (NULL, NULL, NULL,
                                           (GDestroyNotify) meta_sequence_info_free);

      MetaDisplay *display = meta_get_display ();
      g_signal_connect_swapped (display, "grab-op-begin",
                      G_CALLBACK (on_grab_op), tracker);

      g_signal_connect_swapped (display, "grab-op-end",
                      G_CALLBACK (on_grab_op), tracker);

      priv->window_group = meta_get_window_group_for_display (display);
      priv->top_window_group = meta_get_top_window_group_for_display (display);
    }
}

MetaGestureTracker *
meta_gesture_tracker_new (void)
{
  return g_object_new (META_TYPE_GESTURE_TRACKER, NULL);
}

static void
on_actions_changed (ClutterActor       *stage,
                    GParamSpec         *pspec,
                    MetaGestureTracker *tracker)
{
  MetaGestureTrackerPrivate *priv;
  GList *actions, *l;

  priv = meta_gesture_tracker_get_instance_private (tracker);
  actions = clutter_actor_get_actions (stage);

  if (priv->stage_gestures->len > 0)
    g_array_remove_range (priv->stage_gestures, 0, priv->stage_gestures->len);

  for (l = actions; l; l = l->next)
    {
      if (!CLUTTER_IS_GESTURE_ACTION (l->data))
        continue;

      ClutterGestureAction *gesture = g_object_ref (l->data);

      g_array_append_val (priv->stage_gestures, gesture);
    }

  g_list_free (actions);
}

static void
meta_gesture_tracker_track_stage (MetaGestureTracker *tracker,
                              ClutterActor       *stage)
{
  MetaGestureTrackerPrivate *priv;

  priv = meta_gesture_tracker_get_instance_private (tracker);

  priv->actions_changed_id = g_signal_connect (stage, "notify::actions",
                                               G_CALLBACK (on_actions_changed), tracker);
  on_actions_changed (stage, NULL, tracker);
}

gboolean
meta_gesture_tracker_handle_event (MetaGestureTracker *tracker,
				                           const ClutterEvent *event)
{
  MetaGestureTrackerPrivate *priv;
  ClutterEventSequence *sequence;
  gint i;
  MetaSequenceInfo *info;
  gfloat x, y;
  GHashTableIter iter;
  ClutterEventType event_type;
  gboolean event_is_gesture = FALSE;
  priv = meta_gesture_tracker_get_instance_private (tracker);

  gboolean gesture_was_in_progress = priv->gesture_in_progress;

  if (priv->actions_changed_id == 0)
    meta_gesture_tracker_track_stage (tracker, CLUTTER_ACTOR (clutter_event_get_stage (event)));

  event_type = clutter_event_type (event);

  if (priv->gesture_in_progress &&
      (event_type == CLUTTER_ENTER ||
       event_type == CLUTTER_LEAVE))
    return TRUE;

  if (event_type != CLUTTER_TOUCH_BEGIN &&
      event_type != CLUTTER_TOUCH_UPDATE &&
      event_type != CLUTTER_TOUCH_END &&
      event_type != CLUTTER_TOUCH_CANCEL)
    return FALSE;

  for (i = 0; i < priv->stage_gestures->len; i++)
    {
      ClutterGestureAction *gesture = g_array_index (priv->stage_gestures, ClutterGestureAction*, i);

      if (clutter_gesture_action_eval_event (gesture, event) == CLUTTER_EVENT_STOP)
        event_is_gesture = TRUE;
    }

  priv->gesture_in_progress = event_is_gesture;

  sequence = clutter_event_get_event_sequence (event);

  /* From here on it's mostly sequence tracking for X11 support,
   * no need to do this with mouse events.
   */
  if (!sequence)
    return event_is_gesture;

  if (!meta_is_wayland_compositor ())
    {
      if (event_type == CLUTTER_TOUCH_BEGIN)
        {
          info = meta_sequence_info_new (tracker, event);

          g_hash_table_insert (priv->sequences, sequence, info);

          if (priv->gesture_in_progress)
            set_sequence_accepted (info, TRUE);
        }

      /* Reject the sequence right away if it exceeded the distance threshold. */
      if ((event_type == CLUTTER_TOUCH_UPDATE) &&
          !priv->gesture_in_progress)
        {
          info = g_hash_table_lookup (priv->sequences, sequence);

          if (info)
            {
              clutter_event_get_coords (event, &x, &y);

              if ((ABS (info->start_x - x) > DISTANCE_THRESHOLD) ||
                  (ABS (info->start_y - y) > DISTANCE_THRESHOLD))
                set_sequence_accepted (info, FALSE);
            }
        }

      if (event_type == CLUTTER_TOUCH_END)
        {
          info = g_hash_table_lookup (priv->sequences, sequence);

          if (info)
            g_hash_table_remove (priv->sequences, sequence);
        }

      if (event_type == CLUTTER_TOUCH_CANCEL)
        g_hash_table_remove_all (priv->sequences);
    }

  /* Only accept sequences on the first state change.
   * We don't reject gestures here at all because we have to be able
   * to handle a situation like this:
   *  -> three-finger gesture was accepted
   *  -> fourth finger is added, now three finger gesture is rejected
   *     (together with the sequences)
   *  -> four-finger gesture would be accepted after some movement,
   *     but sequences are already gone
   */
  if ((gesture_was_in_progress != priv->gesture_in_progress) &&
      priv->gesture_in_progress)
    {
      if (meta_is_wayland_compositor ())
        {
          g_signal_emit (tracker, signals[STATE_CHANGED], 0, NULL, 1);
        }
      else
        {
          g_hash_table_iter_init (&iter, priv->sequences);
          while (g_hash_table_iter_next (&iter, (gpointer *) &sequence, (gpointer *) &info))
            set_sequence_accepted (info, TRUE);
        }
    }

  return event_is_gesture;
}
