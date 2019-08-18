/*
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
 * Author: Jonas Dreßler <verdre@v0yd.nl>
 */

#ifndef __CLUTTER_TRIGGER_ACTION_H__
#define __CLUTTER_TRIGGER_ACTION_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-gesture-action.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_TRIGGER_ACTION               (clutter_trigger_action_get_type ())
#define CLUTTER_TRIGGER_ACTION(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TRIGGER_ACTION, ClutterTriggerAction))
#define CLUTTER_IS_TRIGGER_ACTION(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TRIGGER_ACTION))
#define CLUTTER_TRIGGER_ACTION_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_TRIGGER_ACTION, ClutterTriggerActionClass))
#define CLUTTER_IS_TRIGGER_ACTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_TRIGGER_ACTION))
#define CLUTTER_TRIGGER_ACTION_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_TRIGGER_ACTION, ClutterTriggerActionClass))

typedef struct _ClutterTriggerAction              ClutterTriggerAction;
typedef struct _ClutterTriggerActionPrivate       ClutterTriggerActionPrivate;
typedef struct _ClutterTriggerActionClass         ClutterTriggerActionClass;

/**
 * ClutterTriggerAction:
 *
 * The #ClutterTriggerAction structure contains only
 * private data and should be accessed using the provided API.
 */
struct _ClutterTriggerAction
{
  /*< private >*/
  ClutterGestureAction parent_instance;

  ClutterTriggerActionPrivate *priv;
};

/**
 * ClutterTriggerActionClass:
 *
 * The #ClutterTriggerActionClass structure contains
 * only private data.
 */
struct _ClutterTriggerActionClass
{
  /*< private >*/
  ClutterGestureActionClass parent_class;
};

CLUTTER_EXPORT
GType clutter_trigger_action_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterAction *    clutter_trigger_action_new                    (void);

CLUTTER_EXPORT
void               clutter_trigger_action_set_trigger_edge       (ClutterTriggerAction *action,
                                                                  ClutterTriggerEdge    edge);
CLUTTER_EXPORT
ClutterTriggerEdge clutter_trigger_action_get_trigger_edge       (ClutterTriggerAction *action);

CLUTTER_EXPORT
void               clutter_trigger_action_set_threshold_distance (ClutterTriggerAction *action,
                                                                  float                 x,
                                                                  float                 y);

CLUTTER_EXPORT
void               clutter_trigger_action_get_threshold_distance (ClutterTriggerAction *action,
                                                                  float                *x,
                                                                  float                *y);

G_END_DECLS

#endif /* __CLUTTER_TRIGGER_ACTION_H__ */
