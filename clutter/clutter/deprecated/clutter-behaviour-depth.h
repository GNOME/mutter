/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Jorn Baayen  <jorn@openedhand.com>
 *             Emmanuele Bassi  <ebassi@openedhand.com>
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

#ifndef __CLUTTER_BEHAVIOUR_DEPTH__
#define __CLUTTER_BEHAVIOUR_DEPTH__

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_BEHAVIOUR_DEPTH            (clutter_behaviour_depth_get_type ())
#define CLUTTER_BEHAVIOUR_DEPTH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_BEHAVIOUR_DEPTH, ClutterBehaviourDepth))
#define CLUTTER_IS_BEHAVIOUR_DEPTH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_BEHAVIOUR_DEPTH))
#define CLUTTER_BEHAVIOUR_DEPTH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BEHAVIOUR_DEPTH, ClutterBehaviourDepthClass))
#define CLUTTER_IS_BEHAVIOUR_DEPTH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BEHAVIOUR_DEPTH))
#define CLUTTER_BEHAVIOUR_DEPTH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BEHAVIOUR_DEPTH, ClutterBehaviourDepthClass))

typedef struct _ClutterBehaviourDepth           ClutterBehaviourDepth;
typedef struct _ClutterBehaviourDepthPrivate    ClutterBehaviourDepthPrivate;
typedef struct _ClutterBehaviourDepthClass      ClutterBehaviourDepthClass;

/**
 * ClutterBehaviourDepth:
 *
 * The #ClutterBehaviourDepth structure contains only private data
 * and should be accessed using the provided API
 *
 * Since: 0.2
 *
 * Deprecated: 1.6: Use clutter_actor_animate() with #ClutterActor:depth
 *   instead.
 */
struct _ClutterBehaviourDepth
{
  /*< private >*/
  ClutterBehaviour parent_instance;

  ClutterBehaviourDepthPrivate *priv;
};

/**
 * ClutterBehaviourDepthClass:
 *
 * The #ClutterBehaviourDepthClass structure contains only private data
 *
 * Since: 0.2
 *
 * Deprecated: 1.6
 */
struct _ClutterBehaviourDepthClass
{
  /*< private >*/
  ClutterBehaviourClass parent_class;
};

CLUTTER_DEPRECATED
GType             clutter_behaviour_depth_get_type (void) G_GNUC_CONST;

CLUTTER_DEPRECATED_FOR(clutter_actor_animate and ClutterActor:depth)
ClutterBehaviour *clutter_behaviour_depth_new      (ClutterAlpha *alpha,
                                                    gint          depth_start,
                                                    gint          depth_end);

CLUTTER_DEPRECATED
void clutter_behaviour_depth_set_bounds (ClutterBehaviourDepth *behaviour,
                                         gint                   depth_start,
                                         gint                   depth_end);
CLUTTER_DEPRECATED
void clutter_behaviour_depth_get_bounds (ClutterBehaviourDepth *behaviour,
                                         gint                  *depth_start,
                                         gint                  *depth_end);

G_END_DECLS

#endif /* __CLUTTER_BEHAVIOUR_DEPTH__ */
