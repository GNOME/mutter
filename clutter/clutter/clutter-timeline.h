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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_TIMELINE                   (clutter_timeline_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterTimeline,
                          clutter_timeline,
                          CLUTTER,
                          TIMELINE,
                          GObject)

/**
 * ClutterTimelineProgressFunc:
 * @timeline: a #ClutterTimeline
 * @elapsed: the elapsed time, in milliseconds
 * @total: the total duration of the timeline, in milliseconds,
 * @user_data: data passed to the function
 *
 * A function for defining a custom progress.
 *
 * Return value: the progress, as a floating point value between -1.0 and 2.0.
 */
typedef gdouble (* ClutterTimelineProgressFunc) (ClutterTimeline *timeline,
                                                 gdouble          elapsed,
                                                 gdouble          total,
                                                 gpointer         user_data);


/**
 * ClutterTimelineClass:
 * @started: class handler for the #ClutterTimeline::started signal
 * @completed: class handler for the #ClutterTimeline::completed signal
 * @paused: class handler for the #ClutterTimeline::paused signal
 * @new_frame: class handler for the #ClutterTimeline::new-frame signal
 * @marker_reached: class handler for the #ClutterTimeline::marker-reached signal
 * @stopped: class handler for the #ClutterTimeline::stopped signal
 *
 * The #ClutterTimelineClass structure contains only private data
 */
struct _ClutterTimelineClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (*started)        (ClutterTimeline *timeline);
  void (*completed)      (ClutterTimeline *timeline);
  void (*paused)         (ClutterTimeline *timeline);

  void (*new_frame)      (ClutterTimeline *timeline,
		          gint             msecs);

  void (*marker_reached) (ClutterTimeline *timeline,
                          const gchar     *marker_name,
                          gint             msecs);
  void (*stopped)        (ClutterTimeline *timeline,
                          gboolean         is_finished);
};

CLUTTER_EXPORT
ClutterTimeline *               clutter_timeline_new_for_actor                  (ClutterActor             *actor,
                                                                                 unsigned int              duration_ms);

CLUTTER_EXPORT
ClutterTimeline *               clutter_timeline_new_for_frame_clock            (ClutterFrameClock        *frame_clock,
                                                                                 unsigned int              duration_ms);

CLUTTER_EXPORT
ClutterActor *                  clutter_timeline_get_actor                      (ClutterTimeline          *timeline);

CLUTTER_EXPORT
void                            clutter_timeline_set_actor                      (ClutterTimeline          *timeline,
                                                                                 ClutterActor             *actor);

CLUTTER_EXPORT
guint                           clutter_timeline_get_duration                   (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_duration                   (ClutterTimeline          *timeline,
                                                                                 guint                     msecs);
CLUTTER_EXPORT
ClutterTimelineDirection        clutter_timeline_get_direction                  (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_direction                  (ClutterTimeline          *timeline,
                                                                                 ClutterTimelineDirection  direction);
CLUTTER_EXPORT
void                            clutter_timeline_start                          (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_pause                          (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_stop                           (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_auto_reverse               (ClutterTimeline          *timeline,
                                                                                 gboolean                  reverse);
CLUTTER_EXPORT
gboolean                        clutter_timeline_get_auto_reverse               (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_repeat_count               (ClutterTimeline          *timeline,
                                                                                 gint                      count);
CLUTTER_EXPORT
gint                            clutter_timeline_get_repeat_count               (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_rewind                         (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_skip                           (ClutterTimeline          *timeline,
                                                                                 guint                     msecs);
CLUTTER_EXPORT
void                            clutter_timeline_advance                        (ClutterTimeline          *timeline,
                                                                                 guint                     msecs);
CLUTTER_EXPORT
guint                           clutter_timeline_get_elapsed_time               (ClutterTimeline          *timeline);
CLUTTER_EXPORT
gdouble                         clutter_timeline_get_progress                   (ClutterTimeline          *timeline);
CLUTTER_EXPORT
gboolean                        clutter_timeline_is_playing                     (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_delay                      (ClutterTimeline          *timeline,
                                                                                 guint                     msecs);
CLUTTER_EXPORT
guint                           clutter_timeline_get_delay                      (ClutterTimeline          *timeline);
CLUTTER_EXPORT
guint                           clutter_timeline_get_delta                      (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_add_marker                     (ClutterTimeline          *timeline,
                                                                                 const gchar              *marker_name,
                                                                                 gdouble                   progress);
CLUTTER_EXPORT
void                            clutter_timeline_add_marker_at_time             (ClutterTimeline          *timeline,
                                                                                 const gchar              *marker_name,
                                                                                 guint                     msecs);
CLUTTER_EXPORT
void                            clutter_timeline_remove_marker                  (ClutterTimeline          *timeline,
                                                                                 const gchar              *marker_name);
CLUTTER_EXPORT
gchar **                        clutter_timeline_list_markers                   (ClutterTimeline          *timeline,
                                                                                 gint                      msecs,
                                                                                 gsize                    *n_markers) G_GNUC_MALLOC;
CLUTTER_EXPORT
gboolean                        clutter_timeline_has_marker                     (ClutterTimeline          *timeline,
                                                                                 const gchar              *marker_name);
CLUTTER_EXPORT
void                            clutter_timeline_advance_to_marker              (ClutterTimeline          *timeline,
                                                                                 const gchar              *marker_name);
CLUTTER_EXPORT
void                            clutter_timeline_set_progress_func              (ClutterTimeline          *timeline,
                                                                                 ClutterTimelineProgressFunc func,
                                                                                 gpointer                  data,
                                                                                 GDestroyNotify            notify);
CLUTTER_EXPORT
void                            clutter_timeline_set_progress_mode              (ClutterTimeline          *timeline,
                                                                                 ClutterAnimationMode      mode);
CLUTTER_EXPORT
ClutterAnimationMode            clutter_timeline_get_progress_mode              (ClutterTimeline          *timeline);
CLUTTER_EXPORT
void                            clutter_timeline_set_step_progress              (ClutterTimeline          *timeline,
                                                                                 gint                      n_steps,
                                                                                 ClutterStepMode           step_mode);
CLUTTER_EXPORT
gboolean                        clutter_timeline_get_step_progress              (ClutterTimeline          *timeline,
                                                                                 gint                     *n_steps,
                                                                                 ClutterStepMode          *step_mode);
CLUTTER_EXPORT
void                            clutter_timeline_set_cubic_bezier_progress      (ClutterTimeline        *timeline,
                                                                                 const graphene_point_t *c_1,
                                                                                 const graphene_point_t *c_2);
CLUTTER_EXPORT
gboolean                        clutter_timeline_get_cubic_bezier_progress      (ClutterTimeline  *timeline,
                                                                                 graphene_point_t *c_1,
                                                                                 graphene_point_t *c_2);

CLUTTER_EXPORT
gint64                          clutter_timeline_get_duration_hint              (ClutterTimeline          *timeline);
CLUTTER_EXPORT
gint                            clutter_timeline_get_current_repeat             (ClutterTimeline          *timeline);

CLUTTER_EXPORT
ClutterFrameClock *             clutter_timeline_get_frame_clock                (ClutterTimeline           *timeline);

CLUTTER_EXPORT
void                            clutter_timeline_set_frame_clock                (ClutterTimeline           *timeline,
                                                                                 ClutterFrameClock         *frame_clock);

G_END_DECLS
