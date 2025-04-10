/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

/**
 * ClutterTimeline:
 *
 * A class for time-based events
 *
 * #ClutterTimeline is a base class for managing time-based event that cause
 * Clutter to redraw a stage, such as animations.
 *
 * Each #ClutterTimeline instance has a duration: once a timeline has been
 * started, using [method@Timeline.start], it will emit a signal that can
 * be used to update the state of the actors.
 *
 * It is important to note that #ClutterTimeline is not a generic API for
 * calling closures after an interval; each Timeline is tied into a frame
 * clock used to drive the frame cycle.
 *
 * Users of #ClutterTimeline should connect to the [signal@Timeline::new-frame]
 * signal, which is emitted each time a timeline is advanced during the maste
 * clock iteration. The [signal@Timeline::new-frame] signal provides the time
 * elapsed since the beginning of the timeline, in milliseconds. A normalized
 * progress value can be obtained by calling [method@Timeline.get_progress].
 * By using [method@Timeline.get_delta] it is possible to obtain the wallclock
 * time elapsed since the last emission of the [signal@Timeline::new-frame]
 * signal.
 *
 * Initial state can be set up by using the [signal@Timeline::started] signal,
 * while final state can be set up by using the [signal@Timeline::stopped]
 * signal. The #ClutterTimeline guarantees the emission of at least a single
 * [signal@Timeline::new-frame] signal, as well as the emission of the
 * [signal@Timeline::completed] signal every time the #ClutterTimeline reaches
 * its [property@Timeline:duration].
 *
 * It is possible to connect to specific points in the timeline progress by
 * adding markers using [method@Timeline.add_marker_at_time] and connecting
 * to the [signal@Timeline::marker-reached] signal.
 *
 * Timelines can be made to loop once they reach the end of their duration, by
 * using clutter_timeline_set_repeat_count(); a looping timeline will still
 * emit the [signal@Timeline::completed] signal once it reaches the end of its
 * duration at each repeat. If you want to be notified of the end of the last
 * repeat, use the [signal@Timeline::stopped] signal.
 *
 * Timelines have a [property@Timeline:direction]: the default direction is
 * %CLUTTER_TIMELINE_FORWARD, and goes from 0 to the duration; it is possible
 * to change the direction to %CLUTTER_TIMELINE_BACKWARD, and have the timeline
 * go from the duration to 0. The direction can be automatically reversed
 * when reaching completion by using the [property@Timeline:auto-reverse] property.
 *
 * Timelines are used in the Clutter animation framework by classes like
 * [class@Transition].
 */

#include "config.h"

#include "clutter/clutter-timeline.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-easing.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-frame-clock.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-timeline-private.h"

typedef struct _ClutterTimelinePrivate
{
  ClutterTimelineDirection direction;

  ClutterFrameClock *custom_frame_clock;
  ClutterFrameClock *frame_clock;
  ClutterActor *frame_clock_actor;
  gulong frame_clock_actor_stage_views_handler_id;

  ClutterActor *actor;
  gulong actor_destroy_handler_id;
  gulong actor_stage_views_handler_id;
  gulong stage_stage_views_handler_id;
  ClutterActor *stage;

  guint delay_id;

  /* The total length in milliseconds of this timeline */
  guint duration;
  guint delay;

  /* The current amount of elapsed time */
  gint64 elapsed_time;

  /* The elapsed time since the last frame was fired */
  gint64 msecs_delta;

  GHashTable *markers_by_name;

  /* Time we last advanced the elapsed time and showed a frame */
  gint64 last_frame_time;

  /* How many times the timeline should repeat */
  gint repeat_count;

  /* The number of times the timeline has repeated */
  gint current_repeat;

  ClutterTimelineProgressFunc progress_func;
  gpointer progress_data;
  GDestroyNotify progress_notify;
  ClutterAnimationMode progress_mode;

  /* step() parameters */
  gint n_steps;
  ClutterStepMode step_mode;

  /* cubic-bezier() parameters */
  graphene_point_t cb_1;
  graphene_point_t cb_2;

  guint is_playing         : 1;

  /* If we've just started playing and haven't yet gotten
   * a tick from the frame clock
   */
  guint waiting_first_tick : 1;
  guint auto_reverse       : 1;
} ClutterTimelinePrivate;

typedef struct {
  gchar *name;
  GQuark quark;

  union {
    guint msecs;
    gdouble progress;
  } data;

  guint is_relative : 1;
} TimelineMarker;

enum
{
  PROP_0,

  PROP_ACTOR,
  PROP_DELAY,
  PROP_DURATION,
  PROP_DIRECTION,
  PROP_AUTO_REVERSE,
  PROP_REPEAT_COUNT,
  PROP_PROGRESS_MODE,
  PROP_FRAME_CLOCK,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

enum
{
  NEW_FRAME,
  STARTED,
  PAUSED,
  COMPLETED,
  MARKER_REACHED,
  STOPPED,

  LAST_SIGNAL
};

static guint timeline_signals[LAST_SIGNAL] = { 0, };

static void update_frame_clock (ClutterTimeline *timeline);


G_DEFINE_TYPE_WITH_CODE (ClutterTimeline, clutter_timeline, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ClutterTimeline))

static TimelineMarker *
timeline_marker_new_time (const gchar *name,
                          guint        msecs)
{
  TimelineMarker *marker = g_new0 (TimelineMarker, 1);

  marker->name = g_strdup (name);
  marker->quark = g_quark_from_string (marker->name);
  marker->is_relative = FALSE;
  marker->data.msecs = msecs;

  return marker;
}

static TimelineMarker *
timeline_marker_new_progress (const gchar *name,
                              gdouble      progress)
{
  TimelineMarker *marker = g_new0 (TimelineMarker, 1);

  marker->name = g_strdup (name);
  marker->quark = g_quark_from_string (marker->name);
  marker->is_relative = TRUE;
  marker->data.progress = CLAMP (progress, 0.0, 1.0);

  return marker;
}

static void
timeline_marker_free (gpointer data)
{
  if (G_LIKELY (data))
    {
      TimelineMarker *marker = data;

      g_free (marker->name);
      g_free (marker);
    }
}

/*< private >
 * clutter_timeline_add_marker_internal:
 * @timeline: a #ClutterTimeline
 * @marker: a TimelineMarker
 *
 * Adds @marker into the hash table of markers for @timeline.
 *
 * The TimelineMarker will either be added or, in case of collisions
 * with another existing marker, freed. In any case, this function
 * assumes the ownership of the passed @marker.
 */
static inline void
clutter_timeline_add_marker_internal (ClutterTimeline *timeline,
                                      TimelineMarker  *marker)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);
  TimelineMarker *old_marker;

  /* create the hash table that will hold the markers */
  if (G_UNLIKELY (priv->markers_by_name == NULL))
    priv->markers_by_name = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   NULL,
                                                   timeline_marker_free);

  old_marker = g_hash_table_lookup (priv->markers_by_name, marker->name);
  if (old_marker != NULL)
    {
      guint msecs;

      if (old_marker->is_relative)
        msecs = (unsigned int) (old_marker->data.progress * priv->duration);
      else
        msecs = old_marker->data.msecs;

      g_warning ("A marker named '%s' already exists at time %d",
                 old_marker->name,
                 msecs);
      timeline_marker_free (marker);
      return;
    }

  g_hash_table_insert (priv->markers_by_name, marker->name, marker);
}

static void
on_actor_destroyed (ClutterActor    *actor,
                    ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  g_clear_signal_handler (&priv->stage_stage_views_handler_id, priv->stage);
  priv->actor = NULL;
}

/**
 * clutter_timeline_get_actor:
 * @timeline: a #ClutterTimeline
 *
 * Get the actor the timeline is associated with.
 *
 * Returns: (transfer none): the associated #ClutterActor
 */
ClutterActor *
clutter_timeline_get_actor (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  return priv->actor;
}

static void
maybe_add_timeline (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  if (!priv->frame_clock)
    return;

  clutter_frame_clock_add_timeline (priv->frame_clock, timeline);
}

static void
maybe_remove_timeline (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  if (!priv->frame_clock)
    return;

  clutter_frame_clock_remove_timeline (priv->frame_clock, timeline);
}

static void
set_frame_clock_internal (ClutterTimeline   *timeline,
                          ClutterFrameClock *frame_clock)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  if (priv->frame_clock == frame_clock)
    return;

  if (priv->frame_clock && priv->is_playing)
    maybe_remove_timeline (timeline);

  g_set_object (&priv->frame_clock, frame_clock);

  g_object_notify_by_pspec (G_OBJECT (timeline),
                            obj_props[PROP_FRAME_CLOCK]);

  if (priv->is_playing)
    maybe_add_timeline (timeline);
}

static void
on_stage_stage_views_changed (ClutterActor    *stage,
                              ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  g_clear_signal_handler (&priv->stage_stage_views_handler_id, priv->stage);
  priv->stage = NULL;

  update_frame_clock (timeline);
}

static void
on_frame_clock_actor_stage_views_changed (ClutterActor    *frame_clock_actor,
                                          ClutterTimeline *timeline)
{
  update_frame_clock (timeline);
}

static void
update_frame_clock (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);
  ClutterFrameClock *frame_clock = NULL;
  ClutterActor *stage;
  ClutterActor *frame_clock_actor;

  if (!priv->actor)
    goto out;

  if (priv->frame_clock_actor)
    {
      g_clear_signal_handler (&priv->frame_clock_actor_stage_views_handler_id,
                              priv->frame_clock_actor);
      g_clear_weak_pointer (&priv->frame_clock_actor);
    }

  frame_clock = clutter_actor_pick_frame_clock (priv->actor, &frame_clock_actor);
  if (frame_clock)
    {
      g_set_weak_pointer (&priv->frame_clock_actor, frame_clock_actor);
      priv->frame_clock_actor_stage_views_handler_id =
        g_signal_connect (frame_clock_actor, "stage-views-changed",
                          G_CALLBACK (on_frame_clock_actor_stage_views_changed),
                          timeline);

      g_clear_signal_handler (&priv->stage_stage_views_handler_id, priv->stage);
      goto out;
    }

  stage = clutter_actor_get_stage (priv->actor);
  if (!stage)
    {
      if (priv->is_playing)
        g_warning ("Timelines with detached actors are not supported. "
                   "%s in animation of duration %ums but not on stage.",
                   _clutter_actor_get_debug_name (priv->actor),
                   priv->duration);
      goto out;
    }

  if (priv->stage_stage_views_handler_id > 0)
    goto out;

  priv->stage_stage_views_handler_id =
    g_signal_connect (stage, "stage-views-changed",
                      G_CALLBACK (on_stage_stage_views_changed),
                      timeline);
  priv->stage = stage;

out:
  set_frame_clock_internal (timeline, frame_clock);
}

static void
on_actor_stage_views_changed (ClutterActor    *actor,
                              ClutterTimeline *timeline)
{
  update_frame_clock (timeline);
}

/**
 * clutter_timeline_set_actor:
 * @timeline: a #ClutterTimeline
 * @actor: (nullable): a #ClutterActor
 *
 * Set the actor the timeline is associated with.
 */
void
clutter_timeline_set_actor (ClutterTimeline *timeline,
                            ClutterActor    *actor)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  g_return_if_fail (!actor || (actor && !priv->custom_frame_clock));

  if (priv->actor)
    {
      g_clear_signal_handler (&priv->actor_destroy_handler_id, priv->actor);
      g_clear_signal_handler (&priv->actor_stage_views_handler_id, priv->actor);
      g_clear_signal_handler (&priv->stage_stage_views_handler_id, priv->stage);
      priv->stage = NULL;
      priv->actor = NULL;
    }

  priv->actor = actor;

  if (priv->actor)
    {
      priv->actor_destroy_handler_id =
        g_signal_connect (priv->actor, "destroy",
                          G_CALLBACK (on_actor_destroyed),
                          timeline);
      priv->actor_stage_views_handler_id =
        g_signal_connect (priv->actor, "stage-views-changed",
                          G_CALLBACK (on_actor_stage_views_changed),
                          timeline);
    }

  update_frame_clock (timeline);
}

void
clutter_timeline_cancel_delay (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  g_clear_handle_id (&priv->delay_id, g_source_remove);
}

/* Object */

static void
clutter_timeline_set_property (GObject      *object,
			       guint         prop_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
  ClutterTimeline *timeline = CLUTTER_TIMELINE (object);

  switch (prop_id)
    {
    case PROP_ACTOR:
      clutter_timeline_set_actor (timeline, g_value_get_object (value));
      break;

    case PROP_DELAY:
      clutter_timeline_set_delay (timeline, g_value_get_uint (value));
      break;

    case PROP_DURATION:
      clutter_timeline_set_duration (timeline, g_value_get_uint (value));
      break;

    case PROP_DIRECTION:
      clutter_timeline_set_direction (timeline, g_value_get_enum (value));
      break;

    case PROP_AUTO_REVERSE:
      clutter_timeline_set_auto_reverse (timeline, g_value_get_boolean (value));
      break;

    case PROP_REPEAT_COUNT:
      clutter_timeline_set_repeat_count (timeline, g_value_get_int (value));
      break;

    case PROP_PROGRESS_MODE:
      clutter_timeline_set_progress_mode (timeline, g_value_get_enum (value));
      break;

    case PROP_FRAME_CLOCK:
      clutter_timeline_set_frame_clock (timeline, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_timeline_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  ClutterTimeline *timeline = CLUTTER_TIMELINE (object);
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  switch (prop_id)
    {
    case PROP_ACTOR:
      g_value_set_object (value, priv->actor);
      break;

    case PROP_DELAY:
      g_value_set_uint (value, priv->delay);
      break;

    case PROP_DURATION:
      g_value_set_uint (value, clutter_timeline_get_duration (timeline));
      break;

    case PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;

    case PROP_AUTO_REVERSE:
      g_value_set_boolean (value, priv->auto_reverse);
      break;

    case PROP_REPEAT_COUNT:
      g_value_set_int (value, priv->repeat_count);
      break;

    case PROP_PROGRESS_MODE:
      g_value_set_enum (value, priv->progress_mode);
      break;

    case PROP_FRAME_CLOCK:
      g_value_set_object (value, priv->frame_clock);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_timeline_finalize (GObject *object)
{
  ClutterTimeline *self = CLUTTER_TIMELINE (object);
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (self);

  if (priv->markers_by_name)
    g_hash_table_destroy (priv->markers_by_name);

  if (priv->is_playing)
    maybe_remove_timeline (self);

  g_clear_object (&priv->frame_clock);

  G_OBJECT_CLASS (clutter_timeline_parent_class)->finalize (object);
}

static void
clutter_timeline_dispose (GObject *object)
{
  ClutterTimeline *self = CLUTTER_TIMELINE (object);
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (self);

  clutter_timeline_cancel_delay (self);

  if (priv->actor)
    {
      g_clear_signal_handler (&priv->actor_destroy_handler_id, priv->actor);
      g_clear_signal_handler (&priv->actor_stage_views_handler_id, priv->actor);
      g_clear_signal_handler (&priv->stage_stage_views_handler_id, priv->stage);
      priv->actor = NULL;
    }

  if (priv->frame_clock_actor)
    {
      g_clear_signal_handler (&priv->frame_clock_actor_stage_views_handler_id,
                              priv->frame_clock_actor);
      g_clear_weak_pointer (&priv->frame_clock_actor);
    }

  if (priv->progress_notify != NULL)
    {
      priv->progress_notify (priv->progress_data);
      priv->progress_func = NULL;
      priv->progress_data = NULL;
      priv->progress_notify = NULL;
    }

  G_OBJECT_CLASS (clutter_timeline_parent_class)->dispose (object);
}

static void
clutter_timeline_class_init (ClutterTimelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /**
   * ClutterTimeline::actor:
   *
   * The actor the timeline is associated with. This will determine what frame
   * clock will drive it.
   */
  obj_props[PROP_ACTOR] =
    g_param_spec_object ("actor", NULL, NULL,
                         CLUTTER_TYPE_ACTOR,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT);
  /**
   * ClutterTimeline:delay:
   *
   * A delay, in milliseconds, that should be observed by the
   * timeline before actually starting.
   */
  obj_props[PROP_DELAY] =
    g_param_spec_uint ("delay", NULL, NULL,
                       0, G_MAXUINT,
                       0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:duration:
   *
   * Duration of the timeline in milliseconds, depending on the
   * [property@Timeline:frame-clock] value.
   */
  obj_props[PROP_DURATION] =
    g_param_spec_uint ("duration", NULL, NULL,
                       0, G_MAXUINT,
                       1000,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:direction:
   *
   * The direction of the timeline, either %CLUTTER_TIMELINE_FORWARD or
   * %CLUTTER_TIMELINE_BACKWARD.
   */
  obj_props[PROP_DIRECTION] =
    g_param_spec_enum ("direction", NULL, NULL,
                       CLUTTER_TYPE_TIMELINE_DIRECTION,
                       CLUTTER_TIMELINE_FORWARD,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:auto-reverse:
   *
   * If the direction of the timeline should be automatically reversed
   * when reaching the end.
   */
  obj_props[PROP_AUTO_REVERSE] =
    g_param_spec_boolean ("auto-reverse", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:repeat-count:
   *
   * Defines how many times the timeline should repeat.
   *
   * If the repeat count is 0, the timeline does not repeat.
   *
   * If the repeat count is set to -1, the timeline will repeat until it is
   * stopped.
   */
  obj_props[PROP_REPEAT_COUNT] =
    g_param_spec_int ("repeat-count", NULL, NULL,
                      -1, G_MAXINT,
                      0,
                      G_PARAM_READWRITE |
                      G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:progress-mode:
   *
   * Controls the way a #ClutterTimeline computes the normalized progress.
   */
  obj_props[PROP_PROGRESS_MODE] =
    g_param_spec_enum ("progress-mode", NULL, NULL,
                       CLUTTER_TYPE_ANIMATION_MODE,
                       CLUTTER_LINEAR,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  /**
   * ClutterTimeline:frame-clock:
   *
   * The frame clock driving the timeline.
   */
  obj_props[PROP_FRAME_CLOCK] =
    g_param_spec_object ("frame-clock", NULL, NULL,
                         CLUTTER_TYPE_FRAME_CLOCK,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_CONSTRUCT);

  object_class->dispose = clutter_timeline_dispose;
  object_class->finalize = clutter_timeline_finalize;
  object_class->set_property = clutter_timeline_set_property;
  object_class->get_property = clutter_timeline_get_property;
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  /**
   * ClutterTimeline::new-frame:
   * @timeline: the timeline which received the signal
   * @msecs: the elapsed time between 0 and duration
   *
   * The signal is emitted for each timeline running
   * timeline before a new frame is drawn to give animations a chance
   * to update the scene.
   */
  timeline_signals[NEW_FRAME] =
    g_signal_new (I_("new-frame"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, new_frame),
		  NULL, NULL, NULL,
		  G_TYPE_NONE,
		  1, G_TYPE_INT);
  /**
   * ClutterTimeline::completed:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The signal is emitted when the timeline's
   * elapsed time reaches the value of the [property@Timeline:duration]
   * property.
   *
   * This signal will be emitted even if the #ClutterTimeline is set to be
   * repeating.
   *
   * If you want to get notification on whether the #ClutterTimeline has
   * been stopped or has finished its run, including its eventual repeats,
   * you should use the [signal@Timeline::stopped] signal instead.
   */
  timeline_signals[COMPLETED] =
    g_signal_new (I_("completed"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, completed),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterTimeline::started:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The signal is emitted when the timeline starts its run.
   * This might be as soon as [method@Timeline.start] is invoked or
   * after the delay set in the [property@Timeline:delay] property has
   * expired.
   */
  timeline_signals[STARTED] =
    g_signal_new (I_("started"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, started),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterTimeline::paused:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The signal is emitted when [method@Timeline.pause] is invoked.
   */
  timeline_signals[PAUSED] =
    g_signal_new (I_("paused"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, paused),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 0);
  /**
   * ClutterTimeline::marker-reached:
   * @timeline: the #ClutterTimeline which received the signal
   * @marker_name: the name of the marker reached
   * @msecs: the elapsed time
   *
   * The signal is emitted each time a timeline
   * reaches a marker set with [method@Timeline.add_marker_at_time].
   *
   * This signal is detailed with the name of the marker as well,
   * so it is possible to connect a callback to the [signal@Timeline::marker-reached]
   * signal for a specific marker with:
   *
   * ```c
   *   clutter_timeline_add_marker_at_time (timeline, "foo", 500);
   *   clutter_timeline_add_marker_at_time (timeline, "bar", 750);
   *
   *   g_signal_connect (timeline, "marker-reached",
   *                     G_CALLBACK (each_marker_reached), NULL);
   *   g_signal_connect (timeline, "marker-reached::foo",
   *                     G_CALLBACK (foo_marker_reached), NULL);
   *   g_signal_connect (timeline, "marker-reached::bar",
   *                     G_CALLBACK (bar_marker_reached), NULL);
   * ```
   *
   * In the example, the first callback will be invoked for both
   * the "foo" and "bar" marker, while the second and third callbacks
   * will be invoked for the "foo" or "bar" markers, respectively.
   */
  timeline_signals[MARKER_REACHED] =
    g_signal_new (I_("marker-reached"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE |
                  G_SIGNAL_DETAILED | G_SIGNAL_NO_HOOKS,
                  G_STRUCT_OFFSET (ClutterTimelineClass, marker_reached),
                  NULL, NULL,
                  _clutter_marshal_VOID__STRING_INT,
                  G_TYPE_NONE, 2,
                  G_TYPE_STRING,
                  G_TYPE_INT);
  /**
   * ClutterTimeline::stopped:
   * @timeline: the #ClutterTimeline that emitted the signal
   * @is_finished: %TRUE if the signal was emitted at the end of the
   *   timeline.
   *
   * The signal is emitted when the timeline
   * has been stopped, either because [method@Timeline.stop] has been
   * called, or because it has been exhausted.
   *
   * This is different from the [signal@Timeline::completed] signal,
   * which gets emitted after every repeat finishes.
   *
   * If the #ClutterTimeline has is marked as infinitely repeating,
   * this signal will never be emitted.
   */
  timeline_signals[STOPPED] =
    g_signal_new (I_("stopped"),
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, stopped),
		  NULL, NULL, NULL,
		  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
}

static void
clutter_timeline_init (ClutterTimeline *self)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (self);

  priv->progress_mode = CLUTTER_LINEAR;

  /* default steps() parameters are 1, end */
  priv->n_steps = 1;
  priv->step_mode = CLUTTER_STEP_MODE_END;

  /* default cubic-bezier() paramereters are (0, 0, 1, 1) */
  graphene_point_init (&priv->cb_1, 0, 0);
  graphene_point_init (&priv->cb_2, 1, 1);
}

struct CheckIfMarkerHitClosure
{
  ClutterTimeline *timeline;
  ClutterTimelineDirection direction;
  gint new_time;
  gint duration;
  gint delta;
};

static gboolean
have_passed_time (const struct CheckIfMarkerHitClosure *data,
                  gint msecs)
{
  /* Ignore markers that are outside the duration of the timeline */
  if (msecs < 0 || msecs > data->duration)
    return FALSE;

  if (data->direction == CLUTTER_TIMELINE_FORWARD)
    {
      /* We need to special case when a marker is added at the
         beginning of the timeline */
      if (msecs == 0 &&
          data->delta > 0 &&
          data->new_time - data->delta <= 0)
        return TRUE;

      /* Otherwise it's just a simple test if the time is in range of
         the previous time and the new time */
      return (msecs > data->new_time - data->delta &&
              msecs <= data->new_time);
    }
  else
    {
      /* We need to special case when a marker is added at the
         end of the timeline */
      if (msecs == data->duration &&
          data->delta > 0 &&
          data->new_time + data->delta >= data->duration)
        return TRUE;

      /* Otherwise it's just a simple test if the time is in range of
         the previous time and the new time */
      return (msecs >= data->new_time &&
              msecs < data->new_time + data->delta);
    }
}

static void
check_if_marker_hit (const gchar *name,
                     TimelineMarker *marker,
                     struct CheckIfMarkerHitClosure *data)
{
  gint msecs;

  if (marker->is_relative)
    msecs = (int) ((double) data->duration * marker->data.progress);
  else
    msecs = marker->data.msecs;

  if (have_passed_time (data, msecs))
    {
      CLUTTER_NOTE (SCHEDULER, "Marker '%s' reached", name);

      g_signal_emit (data->timeline, timeline_signals[MARKER_REACHED],
                     marker->quark,
                     name,
                     msecs);
    }
}

static void
check_markers (ClutterTimeline *timeline,
               gint             delta)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);
  struct CheckIfMarkerHitClosure data;

  /* shortcircuit here if we don't have any marker installed */
  if (priv->markers_by_name == NULL)
    return;

  /* store the details of the timeline so that changing them in a
     marker signal handler won't affect which markers are hit */
  data.timeline = timeline;
  data.direction = priv->direction;
  data.new_time = priv->elapsed_time;
  data.duration = priv->duration;
  data.delta = delta;

  g_hash_table_foreach (priv->markers_by_name,
                        (GHFunc) check_if_marker_hit,
                        &data);
}

static void
emit_frame_signal (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  COGL_TRACE_BEGIN_SCOPED (Emit, "Clutter::Timeline::emit_frame_signal()");

  /* see bug https://bugzilla.gnome.org/show_bug.cgi?id=654066 */
  gint elapsed = (gint) priv->elapsed_time;

  CLUTTER_NOTE (SCHEDULER, "Emitting ::new-frame signal on timeline[%p]", timeline);

  g_signal_emit (timeline, timeline_signals[NEW_FRAME], 0, elapsed);
}

static gboolean
is_complete (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  return (priv->direction == CLUTTER_TIMELINE_FORWARD
          ? priv->elapsed_time >= priv->duration
          : priv->elapsed_time <= 0);
}

static void
set_is_playing (ClutterTimeline *timeline,
                gboolean         is_playing)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  is_playing = !!is_playing;

  if (is_playing == priv->is_playing)
    return;

  priv->is_playing = is_playing;

  if (priv->is_playing)
    {
      priv->waiting_first_tick = TRUE;
      priv->current_repeat = 0;

      maybe_add_timeline (timeline);
    }
  else
    {
      maybe_remove_timeline (timeline);
    }
}

static gboolean
clutter_timeline_do_frame (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  priv = clutter_timeline_get_instance_private (timeline);

  g_object_ref (timeline);

  CLUTTER_NOTE (SCHEDULER, "Timeline [%p] activated (elapsed time: %ld, "
                "duration: %ld, msecs_delta: %ld)\n",
                timeline,
                (long) priv->elapsed_time,
                (long) priv->duration,
                (long) priv->msecs_delta);

  /* Advance time */
  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    priv->elapsed_time += priv->msecs_delta;
  else
    priv->elapsed_time -= priv->msecs_delta;

  /* If we have not reached the end of the timeline: */
  if (!is_complete (timeline))
    {
      /* Emit the signal */
      emit_frame_signal (timeline);
      check_markers (timeline, priv->msecs_delta);

      g_object_unref (timeline);

      return priv->is_playing;
    }
  else
    {
      /* Handle loop or stop */
      ClutterTimelineDirection saved_direction = priv->direction;
      gint elapsed_time_delta = priv->msecs_delta;
      guint overflow_msecs = priv->elapsed_time;
      gint end_msecs;

      /* Update the current elapsed time in case the signal handlers
       * want to take a peek. If we clamp elapsed time, then we need
       * to correpondingly reduce elapsed_time_delta to reflect the correct
       * range of times */
      if (priv->direction == CLUTTER_TIMELINE_FORWARD)
        {
          elapsed_time_delta -= (priv->elapsed_time - priv->duration);
          priv->elapsed_time = priv->duration;
        }
      else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
        {
          elapsed_time_delta -= -priv->elapsed_time;
          priv->elapsed_time = 0;
        }

      end_msecs = priv->elapsed_time;

      /* Emit the signal */
      emit_frame_signal (timeline);
      check_markers (timeline, elapsed_time_delta);

      /* Did the signal handler modify the elapsed time? */
      if (priv->elapsed_time != end_msecs)
        {
          g_object_unref (timeline);
          return TRUE;
        }

      /* Note: If the new-frame signal handler paused the timeline
       * on the last frame we will still go ahead and send the
       * completed signal */
      CLUTTER_NOTE (SCHEDULER,
                    "Timeline [%p] completed (cur: %ld, tot: %ld)",
                    timeline,
                    (long) priv->elapsed_time,
                    (long) priv->msecs_delta);

      if (priv->is_playing &&
          (priv->repeat_count == 0 ||
           priv->repeat_count == priv->current_repeat))
        {
          /* We stop the timeline now, so that the completed signal handler
           * may choose to re-start the timeline
           *
           * XXX Perhaps we should do this earlier, and regardless of
           * priv->repeat_count. Are we limiting the things that could be
           * done in the above new-frame signal handler?
           */
          set_is_playing (timeline, FALSE);

          g_signal_emit (timeline, timeline_signals[COMPLETED], 0);
          g_signal_emit (timeline, timeline_signals[STOPPED], 0, TRUE);
        }
      else
        g_signal_emit (timeline, timeline_signals[COMPLETED], 0);

      priv->current_repeat += 1;

      if (priv->auto_reverse)
        {
          /* :auto-reverse changes the direction of the timeline */
          if (priv->direction == CLUTTER_TIMELINE_FORWARD)
            priv->direction = CLUTTER_TIMELINE_BACKWARD;
          else
            priv->direction = CLUTTER_TIMELINE_FORWARD;

          g_object_notify_by_pspec (G_OBJECT (timeline),
                                    obj_props[PROP_DIRECTION]);
        }

      /* Again check to see if the user has manually played with
       * the elapsed time, before we finally stop or loop the timeline */

      if (priv->elapsed_time != end_msecs &&
          !(/* Except allow changing time from 0 -> duration (or vice-versa)
               since these are considered equivalent */
            (priv->elapsed_time == 0 && end_msecs == priv->duration) ||
            (priv->elapsed_time == priv->duration && end_msecs == 0)
            ))
        {
          g_object_unref (timeline);
          return TRUE;
        }

      if (priv->repeat_count != 0)
        {
          /* We try and interpolate smoothly around a loop */
          if (saved_direction == CLUTTER_TIMELINE_FORWARD)
            priv->elapsed_time = overflow_msecs - priv->duration;
          else
            priv->elapsed_time = priv->duration + overflow_msecs;

          /* Or if the direction changed, we try and bounce */
          if (priv->direction != saved_direction)
            priv->elapsed_time = priv->duration - priv->elapsed_time;

          /* If we have overflowed then we are changing the elapsed
             time without emitting the new frame signal so we need to
             check for markers again */
          check_markers (timeline,
                         priv->direction == CLUTTER_TIMELINE_FORWARD
                           ? priv->elapsed_time
                           : priv->duration - priv->elapsed_time);

          g_object_unref (timeline);
          return TRUE;
        }
      else
        {
          clutter_timeline_rewind (timeline);

          g_object_unref (timeline);
          return FALSE;
        }
    }
}

static gboolean
delay_timeout_func (gpointer data)
{
  ClutterTimeline *timeline = data;
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  priv->delay_id = 0;
  priv->msecs_delta = 0;
  set_is_playing (timeline, TRUE);

  g_signal_emit (timeline, timeline_signals[STARTED], 0);

  return FALSE;
}

/**
 * clutter_timeline_start:
 * @timeline: A #ClutterTimeline
 *
 * Starts the #ClutterTimeline playing.
 **/
void
clutter_timeline_start (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->delay_id || priv->is_playing)
    return;

  if (priv->duration == 0)
    return;

  g_warn_if_fail ((priv->actor && clutter_actor_get_stage (priv->actor)) ||
                  priv->frame_clock);

  if (priv->delay)
    {
      priv->delay_id = g_timeout_add (priv->delay,
                                      delay_timeout_func,
                                      timeline);
    }
  else
    {
      priv->msecs_delta = 0;
      set_is_playing (timeline, TRUE);

      g_signal_emit (timeline, timeline_signals[STARTED], 0);
    }
}

/**
 * clutter_timeline_pause:
 * @timeline: A #ClutterTimeline
 *
 * Pauses the #ClutterTimeline on current frame
 **/
void
clutter_timeline_pause (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  clutter_timeline_cancel_delay (timeline);

  if (!priv->is_playing)
    return;

  priv->msecs_delta = 0;
  set_is_playing (timeline, FALSE);

  g_signal_emit (timeline, timeline_signals[PAUSED], 0);
}

/**
 * clutter_timeline_stop:
 * @timeline: A #ClutterTimeline
 *
 * Stops the #ClutterTimeline and moves to frame 0
 **/
void
clutter_timeline_stop (ClutterTimeline *timeline)
{
  gboolean was_playing;
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  /* we check the is_playing here because pause() will return immediately
   * if the timeline wasn't playing, so we don't know if it was actually
   * stopped, and yet we still don't want to emit a ::stopped signal if
   * the timeline was not playing in the first place.
   */

  priv = clutter_timeline_get_instance_private (timeline);
  was_playing = priv->is_playing;

  clutter_timeline_pause (timeline);
  clutter_timeline_rewind (timeline);

  if (was_playing)
    g_signal_emit (timeline, timeline_signals[STOPPED], 0, FALSE);
}

/**
 * clutter_timeline_rewind:
 * @timeline: A #ClutterTimeline
 *
 * Rewinds #ClutterTimeline to the first frame if its direction is
 * %CLUTTER_TIMELINE_FORWARD and the last frame if it is
 * %CLUTTER_TIMELINE_BACKWARD.
 */
void
clutter_timeline_rewind (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    clutter_timeline_advance (timeline, 0);
  else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
    clutter_timeline_advance (timeline, priv->duration);
}

/**
 * clutter_timeline_skip:
 * @timeline: A #ClutterTimeline
 * @msecs: Amount of time to skip
 *
 * Advance timeline by the requested time in milliseconds
 */
void
clutter_timeline_skip (ClutterTimeline *timeline,
                       guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    {
      priv->elapsed_time += msecs;

      if (priv->elapsed_time > priv->duration)
        priv->elapsed_time = 1;
    }
  else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
    {
      priv->elapsed_time -= msecs;

      if (priv->elapsed_time < 1)
        priv->elapsed_time = priv->duration - 1;
    }

  priv->msecs_delta = 0;
}

/**
 * clutter_timeline_advance:
 * @timeline: A #ClutterTimeline
 * @msecs: Time to advance to
 *
 * Advance timeline to the requested point. The point is given as a
 * time in milliseconds since the timeline started.
 *
 * The @timeline will not emit the [signal@Timeline::new-frame]
 * signal for the given time. The first [signal@Timeline::new-frame] signal
 * after the call to [method@Timeline.advance] will be emit the skipped markers.
 */
void
clutter_timeline_advance (ClutterTimeline *timeline,
                          guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  priv->elapsed_time = CLAMP (msecs, 0, priv->duration);
}

/**
 * clutter_timeline_get_elapsed_time:
 * @timeline: A #ClutterTimeline
 *
 * Request the current time position of the timeline.
 *
 * Return value: current elapsed time in milliseconds.
 */
guint
clutter_timeline_get_elapsed_time (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->elapsed_time;
}

/**
 * clutter_timeline_is_playing:
 * @timeline: A #ClutterTimeline
 *
 * Queries state of a #ClutterTimeline.
 *
 * Return value: %TRUE if timeline is currently playing
 */
gboolean
clutter_timeline_is_playing (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->is_playing;
}

/**
 * clutter_timeline_new_for_actor:
 * @actor: The #ClutterActor the timeline is associated with
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #ClutterTimeline with a duration of @duration milli seconds.
 *
 * Return value: the newly created #ClutterTimeline instance. Use
 *   [method@GObject.Object.unref] when done using it
 */
ClutterTimeline *
clutter_timeline_new_for_actor (ClutterActor *actor,
                                unsigned int  duration_ms)
{
  return g_object_new (CLUTTER_TYPE_TIMELINE,
                       "duration", duration_ms,
                       "actor", actor,
                       NULL);
}

/**
 * clutter_timeline_new_for_frame_clock:
 * @frame_clock: The #ClutterFrameClock the timeline is driven by
 * @duration_ms: Duration of the timeline in milliseconds
 *
 * Creates a new #ClutterTimeline with a duration of @duration_ms milli seconds.
 *
 * Return value: the newly created #ClutterTimeline instance. Use
 *   [method@GObject.Object.unref] when done using it
 */
ClutterTimeline *
clutter_timeline_new_for_frame_clock (ClutterFrameClock *frame_clock,
                                      unsigned int       duration_ms)
{
  return g_object_new (CLUTTER_TYPE_TIMELINE,
                       "duration", duration_ms,
                       "frame-clock", frame_clock,
                       NULL);
}

/**
 * clutter_timeline_get_delay:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the delay set using [method@Timeline.set_delay].
 *
 * Return value: the delay in milliseconds.
 */
guint
clutter_timeline_get_delay (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->delay;
}

/**
 * clutter_timeline_set_delay:
 * @timeline: a #ClutterTimeline
 * @msecs: delay in milliseconds
 *
 * Sets the delay, in milliseconds, before @timeline should start.
 */
void
clutter_timeline_set_delay (ClutterTimeline *timeline,
                            guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->delay != msecs)
    {
      priv->delay = msecs;
      g_object_notify_by_pspec (G_OBJECT (timeline), obj_props[PROP_DELAY]);
    }
}

/**
 * clutter_timeline_get_duration:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the duration of a #ClutterTimeline in milliseconds.
 * See [method@Timeline.set_duration].
 *
 * Return value: the duration of the timeline, in milliseconds.
 */
guint
clutter_timeline_get_duration (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->duration;
}

/**
 * clutter_timeline_set_duration:
 * @timeline: a #ClutterTimeline
 * @msecs: duration of the timeline in milliseconds
 *
 * Sets the duration of the timeline, in milliseconds. The speed
 * of the timeline depends on the [property@Timeline:frame-clock] setting.
 */
void
clutter_timeline_set_duration (ClutterTimeline *timeline,
                               guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (msecs > 0);

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->duration != msecs)
    {
      priv->duration = msecs;

      g_object_notify_by_pspec (G_OBJECT (timeline), obj_props[PROP_DURATION]);
    }
}

/**
 * clutter_timeline_get_progress:
 * @timeline: a #ClutterTimeline
 *
 * The position of the timeline in a normalized [-1, 2] interval.
 *
 * The return value of this function is determined by the progress
 * mode set using [method@Timeline.set_progress_mode], or by the
 * progress function set using [method@Timeline.set_progress_func].
 *
 * Return value: the normalized current position in the timeline.
 */
gdouble
clutter_timeline_get_progress (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0.0);

  priv = clutter_timeline_get_instance_private (timeline);

  /* short-circuit linear progress */
  if (priv->progress_func == NULL)
    return (gdouble) priv->elapsed_time / (gdouble) priv->duration;
  else
    return priv->progress_func (timeline,
                                (gdouble) priv->elapsed_time,
                                (gdouble) priv->duration,
                                priv->progress_data);
}

/**
 * clutter_timeline_get_direction:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the direction of the timeline set with
 * [method@Timeline.set_direction].
 *
 * Return value: the direction of the timeline
 */
ClutterTimelineDirection
clutter_timeline_get_direction (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline),
                        CLUTTER_TIMELINE_FORWARD);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->direction;
}

/**
 * clutter_timeline_set_direction:
 * @timeline: a #ClutterTimeline
 * @direction: the direction of the timeline
 *
 * Sets the direction of @timeline, either %CLUTTER_TIMELINE_FORWARD or
 * %CLUTTER_TIMELINE_BACKWARD.
 */
void
clutter_timeline_set_direction (ClutterTimeline          *timeline,
                                ClutterTimelineDirection  direction)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->direction != direction)
    {
      priv->direction = direction;

      if (priv->elapsed_time == 0)
        priv->elapsed_time = priv->duration;

      g_object_notify_by_pspec (G_OBJECT (timeline), obj_props[PROP_DIRECTION]);
    }
}

/**
 * clutter_timeline_get_delta:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the amount of time elapsed since the last
 * [signal@Timeline::new-frame] signal.
 *
 * This function is only useful inside handlers for the ::new-frame
 * signal, and its behaviour is undefined if the timeline is not
 * playing.
 *
 * Return value: the amount of time in milliseconds elapsed since the
 * last frame
 */
guint
clutter_timeline_get_delta (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);
  if (!clutter_timeline_is_playing (timeline))
    return 0;

  return priv->msecs_delta;
}

void
_clutter_timeline_advance (ClutterTimeline *timeline,
                           gint64           tick_time)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  g_object_ref (timeline);

  CLUTTER_NOTE (SCHEDULER,
                "Timeline [%p] advancing (cur: %ld, tot: %ld, "
                "tick_time: %lu)",
                timeline,
                (long) priv->elapsed_time,
                (long) priv->msecs_delta,
                (long) tick_time);

  priv->msecs_delta = tick_time;
  priv->is_playing = TRUE;

  clutter_timeline_do_frame (timeline);

  priv->is_playing = FALSE;

  g_object_unref (timeline);
}

/*< private >
 * clutter_timeline_do_tick
 * @timeline: a #ClutterTimeline
 * @tick_time: time of advance
 *
 * Advances @timeline based on the time passed in @tick_time. This
 * function is called by the frame clock and ideally passes the next
 * presentation time in which consequences of our timeline will be visible.
 * Otherwise an estimate using the current monotonic time is also acceptable.
 * The @timeline will use this interval to emit the #ClutterTimeline::new-frame
 * signal and eventually skip frames.
 */
void
_clutter_timeline_do_tick (ClutterTimeline *timeline,
                           gint64           tick_time)
{
  ClutterTimelinePrivate *priv;

  COGL_TRACE_BEGIN_SCOPED (DoTick, "Clutter::Timeline::do_tick()");

  priv = clutter_timeline_get_instance_private (timeline);

  CLUTTER_NOTE (SCHEDULER,
                "Timeline [%p] ticked (elapsed_time: %ld, msecs_delta: %ld, "
                "last_frame_time: %ld, tick_time: %ld)",
                timeline,
                (long) priv->elapsed_time,
                (long) priv->msecs_delta,
                (long) priv->last_frame_time,
                (long) tick_time);

  /* Check the is_playing variable before performing the timeline tick.
   * This is necessary, as if a timeline is stopped in response to a
   * frame clock generated signal of a different timeline, this code can
   * still be reached.
   */
  if (!priv->is_playing)
    return;

  if (priv->waiting_first_tick)
    {
      priv->last_frame_time = tick_time;
      priv->msecs_delta = 0;
      priv->waiting_first_tick = FALSE;
      clutter_timeline_do_frame (timeline);
    }
  else
    {
      gint64 msecs;

      msecs = tick_time - priv->last_frame_time;

      /* if the clock rolled back between ticks we need to
       * account for it; the best course of action, since the
       * clock roll back can happen by any arbitrary amount
       * of milliseconds, is to drop a frame here
       */
      if (msecs < 0)
        {
          priv->last_frame_time = tick_time;
          return;
        }

      if (msecs != 0)
        {
          /* Avoid accumulating error */
          priv->last_frame_time += msecs;
          priv->msecs_delta = msecs;
          clutter_timeline_do_frame (timeline);
        }
    }
}

/**
 * clutter_timeline_add_marker:
 * @timeline: a #ClutterTimeline
 * @marker_name: the unique name for this marker
 * @progress: the normalized value of the position of the martke
 *
 * Adds a named marker that will be hit when the timeline has reached
 * the specified @progress.
 *
 * Markers are unique string identifiers for a given position on the
 * timeline. Once @timeline reaches the given @progress of its duration,
 * if will emit a [signal@Timeline::marker-reached] signal for each marker
 * attached to that particular point.
 *
 * A marker can be removed with [method@Timeline.remove_marker]. The
 * timeline can be advanced to a marker using
 * [method@Timeline.advance_to_marker].
 *
 * See also: [method@Timeline.add_marker_at_time]
 */
void
clutter_timeline_add_marker (ClutterTimeline *timeline,
                             const gchar     *marker_name,
                             gdouble          progress)
{
  TimelineMarker *marker;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (marker_name != NULL);

  marker = timeline_marker_new_progress (marker_name, progress);
  clutter_timeline_add_marker_internal (timeline, marker);
}

/**
 * clutter_timeline_add_marker_at_time:
 * @timeline: a #ClutterTimeline
 * @marker_name: the unique name for this marker
 * @msecs: position of the marker in milliseconds
 *
 * Adds a named marker that will be hit when the timeline has been
 * running for @msecs milliseconds.
 *
 * Markers are unique string identifiers for a given position on the
 * timeline. Once @timeline reaches the given @msecs, it will emit
 * a [signal@Timeline::marker-reached] signal for each marker attached to that position.
 *
 * A marker can be removed with [method@Timeline.remove_marker]. The
 * timeline can be advanced to a marker using
 * [method@Timeline.advance_to_marker].
 *
 * See also: [method@Timeline.add_marker]
 */
void
clutter_timeline_add_marker_at_time (ClutterTimeline *timeline,
                                     const gchar     *marker_name,
                                     guint            msecs)
{
  TimelineMarker *marker;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (marker_name != NULL);
  g_return_if_fail (msecs <= clutter_timeline_get_duration (timeline));

  marker = timeline_marker_new_time (marker_name, msecs);
  clutter_timeline_add_marker_internal (timeline, marker);
}

struct CollectMarkersClosure
{
  guint duration;
  guint msecs;
  GArray *markers;
};

static void
collect_markers (const gchar *key,
                 TimelineMarker *marker,
                 struct CollectMarkersClosure *data)
{
  guint msecs;

  if (marker->is_relative)
    msecs = (unsigned int) (marker->data.progress * data->duration);
  else
    msecs = marker->data.msecs;

  if (msecs == data->msecs)
    {
      gchar *name_copy = g_strdup (key);
      g_array_append_val (data->markers, name_copy);
    }
}

/**
 * clutter_timeline_list_markers:
 * @timeline: a #ClutterTimeline
 * @msecs: the time to check, or -1
 * @n_markers: the number of markers returned
 *
 * Retrieves the list of markers at time @msecs. If @msecs is a
 * negative integer, all the markers attached to @timeline will be
 * returned.
 *
 * Return value: (transfer full) (array zero-terminated=1 length=n_markers):
 *   a newly allocated, %NULL terminated string array containing the names
 *   of the markers. Use [func@GLib.strfreev] when done.
 */
gchar **
clutter_timeline_list_markers (ClutterTimeline *timeline,
                               gint             msecs,
                               gsize           *n_markers)
{
  ClutterTimelinePrivate *priv;
  gchar **retval = NULL;
  gsize i;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), NULL);

  priv = clutter_timeline_get_instance_private (timeline);

  if (G_UNLIKELY (priv->markers_by_name == NULL))
    {
      if (n_markers)
        *n_markers = 0;

      return NULL;
    }

  if (msecs < 0)
    {
      GList *markers, *l;

      markers = g_hash_table_get_keys (priv->markers_by_name);
      retval = g_new0 (gchar*, g_list_length (markers) + 1);

      for (i = 0, l = markers; l != NULL; i++, l = l->next)
        retval[i] = g_strdup (l->data);

      g_list_free (markers);
    }
  else
    {
      struct CollectMarkersClosure data;

      data.duration = priv->duration;
      data.msecs = msecs;
      data.markers = g_array_new (TRUE, FALSE, sizeof (gchar *));

      g_hash_table_foreach (priv->markers_by_name,
                            (GHFunc) collect_markers,
                            &data);

      i = data.markers->len;
      retval = (gchar **) (void *) g_array_free (data.markers, FALSE);
    }

  if (n_markers)
    *n_markers = i;

  return retval;
}

/**
 * clutter_timeline_advance_to_marker:
 * @timeline: a #ClutterTimeline
 * @marker_name: the name of the marker
 *
 * Advances @timeline to the time of the given @marker_name.
 *
 * Like [method@Timeline.advance], this function will not
 * emit the [signal@Timeline::new-frame] for the time where @marker_name
 * is set, nor it will emit [signal@Timeline::marker-reached] for
 * @marker_name.
 */
void
clutter_timeline_advance_to_marker (ClutterTimeline *timeline,
                                    const gchar     *marker_name)
{
  ClutterTimelinePrivate *priv;
  TimelineMarker *marker;
  guint msecs;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (marker_name != NULL);

  priv = clutter_timeline_get_instance_private (timeline);

  if (G_UNLIKELY (priv->markers_by_name == NULL))
    {
      g_warning ("No marker named '%s' found.", marker_name);
      return;
    }

  marker = g_hash_table_lookup (priv->markers_by_name, marker_name);
  if (marker == NULL)
    {
      g_warning ("No marker named '%s' found.", marker_name);
      return;
    }

  if (marker->is_relative)
    msecs = (unsigned int) (marker->data.progress * priv->duration);
  else
    msecs = marker->data.msecs;

  clutter_timeline_advance (timeline, msecs);
}

/**
 * clutter_timeline_remove_marker:
 * @timeline: a #ClutterTimeline
 * @marker_name: the name of the marker to remove
 *
 * Removes @marker_name, if found, from @timeline.
 */
void
clutter_timeline_remove_marker (ClutterTimeline *timeline,
                                const gchar     *marker_name)
{
  ClutterTimelinePrivate *priv;
  TimelineMarker *marker;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (marker_name != NULL);

  priv = clutter_timeline_get_instance_private (timeline);

  if (G_UNLIKELY (priv->markers_by_name == NULL))
    {
      g_warning ("No marker named '%s' found.", marker_name);
      return;
    }

  marker = g_hash_table_lookup (priv->markers_by_name, marker_name);
  if (!marker)
    {
      g_warning ("No marker named '%s' found.", marker_name);
      return;
    }

  /* this will take care of freeing the marker as well */
  g_hash_table_remove (priv->markers_by_name, marker_name);
}

/**
 * clutter_timeline_has_marker:
 * @timeline: a #ClutterTimeline
 * @marker_name: the name of the marker
 *
 * Checks whether @timeline has a marker set with the given name.
 *
 * Return value: %TRUE if the marker was found
 */
gboolean
clutter_timeline_has_marker (ClutterTimeline *timeline,
                             const gchar     *marker_name)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (marker_name != NULL, FALSE);

  priv = clutter_timeline_get_instance_private (timeline);
  if (G_UNLIKELY (priv->markers_by_name == NULL))
    return FALSE;

  return NULL != g_hash_table_lookup (priv->markers_by_name,
                                      marker_name);
}

/**
 * clutter_timeline_set_auto_reverse:
 * @timeline: a #ClutterTimeline
 * @reverse: %TRUE if the @timeline should reverse the direction
 *
 * Sets whether @timeline should reverse the direction after the
 * emission of the [signal@Timeline::completed] signal.
 *
 * Setting the [property@Timeline:auto-reverse] property to %TRUE is the
 * equivalent of connecting a callback to the [signal@Timeline::completed]
 * signal and changing the direction of the timeline from that callback;
 * for instance, this code:
 *
 * ```c
 * static void
 * reverse_timeline (ClutterTimeline *timeline)
 * {
 *   ClutterTimelineDirection dir = clutter_timeline_get_direction (timeline);
 *
 *   if (dir == CLUTTER_TIMELINE_FORWARD)
 *     dir = CLUTTER_TIMELINE_BACKWARD;
 *   else
 *     dir = CLUTTER_TIMELINE_FORWARD;
 *
 *   clutter_timeline_set_direction (timeline, dir);
 * }
 * ...
 *   timeline = clutter_timeline_new_for_actor (some_actor, 1000);
 *   clutter_timeline_set_repeat_count (timeline, -1);
 *   g_signal_connect (timeline, "completed",
 *                     G_CALLBACK (reverse_timeline),
 *                     NULL);
 * ```
 *
 * can be effectively replaced by:
 *
 * ```c
 *   timeline = clutter_timeline_new_for_actor (some_actor, 1000);
 *   clutter_timeline_set_repeat_count (timeline, -1);
 *   clutter_timeline_set_auto_reverse (timeline);
 * ```
 */
void
clutter_timeline_set_auto_reverse (ClutterTimeline *timeline,
                                   gboolean         reverse)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  reverse = !!reverse;

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->auto_reverse != reverse)
    {
      priv->auto_reverse = reverse;

      g_object_notify_by_pspec (G_OBJECT (timeline),
                                obj_props[PROP_AUTO_REVERSE]);
    }
}

/**
 * clutter_timeline_get_auto_reverse:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the value set by [method@Timeline.set_auto_reverse].
 *
 * Return value: %TRUE if the timeline should automatically reverse, and
 *   %FALSE otherwise
 */
gboolean
clutter_timeline_get_auto_reverse (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->auto_reverse;
}

/**
 * clutter_timeline_set_repeat_count:
 * @timeline: a #ClutterTimeline
 * @count: the number of times the timeline should repeat
 *
 * Sets the number of times the @timeline should repeat.
 *
 * If @count is 0, the timeline never repeats.
 *
 * If @count is -1, the timeline will always repeat until
 * it's stopped.
 */
void
clutter_timeline_set_repeat_count (ClutterTimeline *timeline,
                                   gint             count)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (count >= -1);

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->repeat_count != count)
    {
      priv->repeat_count = count;

      g_object_notify_by_pspec (G_OBJECT (timeline),
                                obj_props[PROP_REPEAT_COUNT]);
    }
}

/**
 * clutter_timeline_get_repeat_count:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the number set using [method@Timeline.set_repeat_count].
 *
 * Return value: the number of repeats
 */
gint
clutter_timeline_get_repeat_count (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->repeat_count;
}

/**
 * clutter_timeline_set_progress_func:
 * @timeline: a #ClutterTimeline
 * @func: (scope notified) (allow-none) (closure data): a progress function,
 *        or %NULL
 * @data: data to pass to @func
 * @notify: a function to be called when the progress function is removed
 *    or the timeline is disposed
 *
 * Sets a custom progress function for @timeline. The progress function will
 * be called by [method@Timeline.get_progress] and will be used to compute
 * the progress value based on the elapsed time and the total duration of the
 * timeline.
 *
 * If @func is not %NULL, the [property@Timeline:progress-mode] property will
 * be set to %CLUTTER_CUSTOM_MODE.
 *
 * If @func is %NULL, any previously set progress function will be unset, and
 * the [property@Timeline:progress-mode] property will be set to %CLUTTER_LINEAR.
 */
void
clutter_timeline_set_progress_func (ClutterTimeline             *timeline,
                                    ClutterTimelineProgressFunc  func,
                                    gpointer                     data,
                                    GDestroyNotify               notify)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->progress_notify != NULL)
    priv->progress_notify (priv->progress_data);

  priv->progress_func = func;
  priv->progress_data = data;
  priv->progress_notify = notify;

  if (priv->progress_func != NULL)
    priv->progress_mode = CLUTTER_CUSTOM_MODE;
  else
    priv->progress_mode = CLUTTER_LINEAR;

  g_object_notify_by_pspec (G_OBJECT (timeline), obj_props[PROP_PROGRESS_MODE]);
}

static gdouble
clutter_timeline_progress_func (ClutterTimeline   *timeline,
                                gdouble            elapsed,
                                gdouble            duration,
                                gpointer user_data G_GNUC_UNUSED)
{
  ClutterTimelinePrivate *priv =
    clutter_timeline_get_instance_private (timeline);

  /* parametrized easing functions need to be handled separately */
  switch (priv->progress_mode)
    {
    case CLUTTER_STEPS:
      if (priv->step_mode == CLUTTER_STEP_MODE_START)
        return clutter_ease_steps_start (elapsed, duration, priv->n_steps);
      else if (priv->step_mode == CLUTTER_STEP_MODE_END)
        return clutter_ease_steps_end (elapsed, duration, priv->n_steps);
      else
        g_assert_not_reached ();
      break;

    case CLUTTER_STEP_START:
      return clutter_ease_steps_start (elapsed, duration, 1);

    case CLUTTER_STEP_END:
      return clutter_ease_steps_end (elapsed, duration, 1);

    case CLUTTER_CUBIC_BEZIER:
      return clutter_ease_cubic_bezier (elapsed, duration,
                                        priv->cb_1.x, priv->cb_1.y,
                                        priv->cb_2.x, priv->cb_2.y);

    case CLUTTER_EASE:
      return clutter_ease_cubic_bezier (elapsed, duration,
                                        0.25, 0.1, 0.25, 1.0);

    case CLUTTER_EASE_IN:
      return clutter_ease_cubic_bezier (elapsed, duration,
                                        0.42, 0.0, 1.0, 1.0);

    case CLUTTER_EASE_OUT:
      return clutter_ease_cubic_bezier (elapsed, duration,
                                        0.0, 0.0, 0.58, 1.0);

    case CLUTTER_EASE_IN_OUT:
      return clutter_ease_cubic_bezier (elapsed, duration,
                                        0.42, 0.0, 0.58, 1.0);

    default:
      break;
    }

  return clutter_easing_for_mode (priv->progress_mode, elapsed, duration);
}

/**
 * clutter_timeline_set_progress_mode:
 * @timeline: a #ClutterTimeline
 * @mode: the progress mode, as a #ClutterAnimationMode
 *
 * Sets the progress function using a value from the [enum@AnimationMode]
 * enumeration. The @mode cannot be %CLUTTER_CUSTOM_MODE or bigger than
 * %CLUTTER_ANIMATION_LAST.
 */
void
clutter_timeline_set_progress_mode (ClutterTimeline      *timeline,
                                    ClutterAnimationMode  mode)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (mode < CLUTTER_ANIMATION_LAST);
  g_return_if_fail (mode != CLUTTER_CUSTOM_MODE);

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->progress_mode == mode)
    return;

  if (priv->progress_notify != NULL)
    priv->progress_notify (priv->progress_data);

  priv->progress_mode = mode;

  /* short-circuit linear progress */
  if (priv->progress_mode != CLUTTER_LINEAR)
    priv->progress_func = clutter_timeline_progress_func;
  else
    priv->progress_func = NULL;

  priv->progress_data = NULL;
  priv->progress_notify = NULL;

  g_object_notify_by_pspec (G_OBJECT (timeline), obj_props[PROP_PROGRESS_MODE]);
}

/**
 * clutter_timeline_get_progress_mode:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the progress mode set using [method@Timeline.set_progress_mode]
 * or [method@Timeline.set_progress_func].
 *
 * Return value: a #ClutterAnimationMode
 */
ClutterAnimationMode
clutter_timeline_get_progress_mode (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), CLUTTER_LINEAR);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->progress_mode;
}

/**
 * clutter_timeline_get_duration_hint:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the full duration of the @timeline, taking into account the
 * current value of the [property@Timeline:repeat-count] property.
 *
 * If the [property@Timeline:repeat-count] property is set to -1, this function
 * will return %G_MAXINT64.
 *
 * The returned value is to be considered a hint, and it's only valid
 * as long as the @timeline hasn't been changed.
 *
 * Return value: the full duration of the #ClutterTimeline
 */
gint64
clutter_timeline_get_duration_hint (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->repeat_count == 0)
    return priv->duration;
  else if (priv->repeat_count < 0)
    return G_MAXINT64;
  else
    return priv->repeat_count * priv->duration;
}

/**
 * clutter_timeline_get_current_repeat:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the current repeat for a timeline.
 *
 * Repeats start at 0.
 *
 * Return value: the current repeat
 */
gint
clutter_timeline_get_current_repeat (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->current_repeat;
}

/**
 * clutter_timeline_set_step_progress:
 * @timeline: a #ClutterTimeline
 * @n_steps: the number of steps
 * @step_mode: whether the change should happen at the start
 *   or at the end of the step
 *
 * Sets the [property@Timeline:progress-mode] of the @timeline to %CLUTTER_STEPS
 * and provides the parameters of the step function.
 */
void
clutter_timeline_set_step_progress (ClutterTimeline *timeline,
                                    gint             n_steps,
                                    ClutterStepMode  step_mode)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (n_steps > 0);

  priv = clutter_timeline_get_instance_private (timeline);

  if (priv->progress_mode == CLUTTER_STEPS &&
      priv->n_steps == n_steps &&
      priv->step_mode == step_mode)
    return;

  priv->n_steps = n_steps;
  priv->step_mode = step_mode;
  clutter_timeline_set_progress_mode (timeline, CLUTTER_STEPS);
}

/**
 * clutter_timeline_get_step_progress:
 * @timeline: a #ClutterTimeline
 * @n_steps: (out): return location for the number of steps, or %NULL
 * @step_mode: (out): return location for the value change policy,
 *   or %NULL
 *
 * Retrieves the parameters of the step progress mode used by @timeline.
 *
 * Return value: %TRUE if the @timeline is using a step progress
 *   mode, and %FALSE otherwise
 */
gboolean
clutter_timeline_get_step_progress (ClutterTimeline *timeline,
                                    gint            *n_steps,
                                    ClutterStepMode *step_mode)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  priv = clutter_timeline_get_instance_private (timeline);

  if (!(priv->progress_mode == CLUTTER_STEPS ||
        priv->progress_mode == CLUTTER_STEP_START ||
        priv->progress_mode == CLUTTER_STEP_END))
    return FALSE;

  if (n_steps != NULL)
    *n_steps = priv->n_steps;

  if (step_mode != NULL)
    *step_mode = priv->step_mode;

  return TRUE;
}

/**
 * clutter_timeline_set_cubic_bezier_progress:
 * @timeline: a #ClutterTimeline
 * @c_1: the first control point for the cubic bezier
 * @c_2: the second control point for the cubic bezier
 *
 * Sets the [property@Timeline:progress-mode] of @timeline
 * to %CLUTTER_CUBIC_BEZIER, and sets the two control
 * points for the cubic bezier.
 *
 * The cubic bezier curve is between (0, 0) and (1, 1). The X coordinate
 * of the two control points must be in the [ 0, 1 ] range, while the
 * Y coordinate of the two control points can exceed this range.
 */
void
clutter_timeline_set_cubic_bezier_progress (ClutterTimeline        *timeline,
                                            const graphene_point_t *c_1,
                                            const graphene_point_t *c_2)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (c_1 != NULL && c_2 != NULL);

  priv = clutter_timeline_get_instance_private (timeline);

  priv->cb_1 = *c_1;
  priv->cb_2 = *c_2;

  /* ensure the range on the X coordinate */
  priv->cb_1.x = CLAMP (priv->cb_1.x, 0.f, 1.f);
  priv->cb_2.x = CLAMP (priv->cb_2.x, 0.f, 1.f);

  clutter_timeline_set_progress_mode (timeline, CLUTTER_CUBIC_BEZIER);
}

/**
 * clutter_timeline_get_cubic_bezier_progress:
 * @timeline: a #ClutterTimeline
 * @c_1: (out caller-allocates): return location for the first control
 *   point of the cubic bezier, or %NULL
 * @c_2: (out caller-allocates): return location for the second control
 *   point of the cubic bezier, or %NULL
 *
 * Retrieves the control points for the cubic bezier progress mode.
 *
 * Return value: %TRUE if the @timeline is using a cubic bezier progress
 *   more, and %FALSE otherwise
 */
gboolean
clutter_timeline_get_cubic_bezier_progress (ClutterTimeline  *timeline,
                                            graphene_point_t *c_1,
                                            graphene_point_t *c_2)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  priv = clutter_timeline_get_instance_private (timeline);
  if (!(priv->progress_mode == CLUTTER_CUBIC_BEZIER ||
        priv->progress_mode == CLUTTER_EASE ||
        priv->progress_mode == CLUTTER_EASE_IN ||
        priv->progress_mode == CLUTTER_EASE_OUT ||
        priv->progress_mode == CLUTTER_EASE_IN_OUT))
    return FALSE;

  if (c_1 != NULL)
    *c_1 = priv->cb_1;

  if (c_2 != NULL)
    *c_2 = priv->cb_2;

  return TRUE;
}

/**
 * clutter_timeline_get_frame_clock: (skip)
 */
ClutterFrameClock *
clutter_timeline_get_frame_clock (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), NULL);

  priv = clutter_timeline_get_instance_private (timeline);

  return priv->frame_clock;
}

void
clutter_timeline_set_frame_clock (ClutterTimeline   *timeline,
                                  ClutterFrameClock *frame_clock)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = clutter_timeline_get_instance_private (timeline);

  g_assert (!frame_clock || (frame_clock && !priv->actor));
  g_return_if_fail (!frame_clock || (frame_clock && !priv->actor));

  priv->custom_frame_clock = frame_clock;
  if (!priv->actor)
    set_frame_clock_internal (timeline, frame_clock);
}
