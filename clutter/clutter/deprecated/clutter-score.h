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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __CLUTTER_SCORE_H__
#define __CLUTTER_SCORE_H__

#include <clutter/clutter-timeline.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_SCORE              (clutter_score_get_type ())

#define CLUTTER_SCORE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_SCORE, ClutterScore))
#define CLUTTER_SCORE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_SCORE, ClutterScoreClass))
#define CLUTTER_IS_SCORE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_SCORE))
#define CLUTTER_IS_SCORE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_SCORE))
#define CLUTTER_SCORE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_SCORE, ClutterScoreClass))

typedef struct _ClutterScore        ClutterScore;
typedef struct _ClutterScorePrivate ClutterScorePrivate;
typedef struct _ClutterScoreClass   ClutterScoreClass; 

/**
 * ClutterScore:
 *
 * The #ClutterScore structure contains only private data
 * and should be accessed using the provided API
 *
 * Since: 0.6
 */
struct _ClutterScore
{
  /*< private >*/
  GObject                 parent;
  ClutterScorePrivate    *priv;
};

/**
 * ClutterScoreClass:
 * @timeline_started: handler for the #ClutterScore::timeline-started signal
 * @timeline_completed: handler for the #ClutterScore::timeline-completed
 *   signal
 * @started: handler for the #ClutterScore::started signal
 * @completed: handler for the #ClutterScore::completed signal
 * @paused: handler for the #ClutterScore::paused signal
 *
 * The #ClutterScoreClass structure contains only private data
 *
 * Since: 0.6
 */
struct _ClutterScoreClass
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  void (* timeline_started)   (ClutterScore    *score,
                               ClutterTimeline *timeline);
  void (* timeline_completed) (ClutterScore    *score,
                               ClutterTimeline *timeline);

  void (* started)            (ClutterScore    *score);
  void (* completed)          (ClutterScore    *score);
  void (* paused)             (ClutterScore    *score);

  /*< private >*/
  /* padding for future expansion */
  void (*_clutter_score_1) (void);
  void (*_clutter_score_2) (void);
  void (*_clutter_score_3) (void);
  void (*_clutter_score_4) (void);
  void (*_clutter_score_5) (void);
}; 

CLUTTER_DEPRECATED
GType clutter_score_get_type (void) G_GNUC_CONST;

CLUTTER_DEPRECATED
ClutterScore *   clutter_score_new            (void);

CLUTTER_DEPRECATED
void             clutter_score_set_loop         (ClutterScore    *score,
                                                 gboolean         loop);
CLUTTER_DEPRECATED
gboolean         clutter_score_get_loop         (ClutterScore    *score);

CLUTTER_DEPRECATED
gulong           clutter_score_append           (ClutterScore    *score,
                                                 ClutterTimeline *parent,
                                                 ClutterTimeline *timeline);
CLUTTER_DEPRECATED
gulong           clutter_score_append_at_marker (ClutterScore    *score,
                                                 ClutterTimeline *parent,
                                                 const gchar     *marker_name,
                                                 ClutterTimeline *timeline);
CLUTTER_DEPRECATED
void             clutter_score_remove           (ClutterScore    *score,
                                                 gulong           id_);
CLUTTER_DEPRECATED
void             clutter_score_remove_all       (ClutterScore    *score);
CLUTTER_DEPRECATED
ClutterTimeline *clutter_score_get_timeline     (ClutterScore    *score,
                                                 gulong           id_);
CLUTTER_DEPRECATED
GSList *         clutter_score_list_timelines   (ClutterScore    *score);

CLUTTER_DEPRECATED
void             clutter_score_start            (ClutterScore    *score);
CLUTTER_DEPRECATED
void             clutter_score_stop             (ClutterScore    *score);
CLUTTER_DEPRECATED
void             clutter_score_pause            (ClutterScore    *score);
CLUTTER_DEPRECATED
void             clutter_score_rewind           (ClutterScore    *score);
CLUTTER_DEPRECATED
gboolean         clutter_score_is_playing       (ClutterScore    *score);

G_END_DECLS

#endif /* __CLUTTER_SCORE_H__ */
