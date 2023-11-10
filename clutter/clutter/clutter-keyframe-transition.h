/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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
 *
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "clutter/clutter-property-transition.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_KEYFRAME_TRANSITION                (clutter_keyframe_transition_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterKeyframeTransition,
                          clutter_keyframe_transition,
                          CLUTTER,
                          KEYFRAME_TRANSITION,
                          ClutterPropertyTransition)


/**
 * ClutterKeyframeTransitionClass:
 *
 * The `ClutterKeyframeTransitionClass` structure contains only
 * private data.
 */
struct _ClutterKeyframeTransitionClass
{
  /*< private >*/
  ClutterPropertyTransitionClass parent_class;
};

CLUTTER_EXPORT
ClutterTransition *     clutter_keyframe_transition_new                 (const char *property_name);

CLUTTER_EXPORT
void                    clutter_keyframe_transition_set_key_frames      (ClutterKeyframeTransition  *transition,
                                                                         guint                       n_key_frames,
                                                                         const double               *key_frames);
CLUTTER_EXPORT
void                    clutter_keyframe_transition_set_values          (ClutterKeyframeTransition  *transition,
                                                                         guint                       n_values,
                                                                         const GValue               *values);
CLUTTER_EXPORT
void                    clutter_keyframe_transition_set_modes           (ClutterKeyframeTransition  *transition,
                                                                         guint                       n_modes,
                                                                         const ClutterAnimationMode *modes);
CLUTTER_EXPORT
void                    clutter_keyframe_transition_set                 (ClutterKeyframeTransition  *transition,
                                                                         GType                       gtype,
                                                                         guint                       n_key_frames,
                                                                         ...);

CLUTTER_EXPORT
void                    clutter_keyframe_transition_set_key_frame       (ClutterKeyframeTransition  *transition,
                                                                         guint                       index_,
                                                                         double                      key,
                                                                         ClutterAnimationMode        mode,
                                                                         const GValue               *value);
CLUTTER_EXPORT
void                    clutter_keyframe_transition_get_key_frame       (ClutterKeyframeTransition  *transition,
                                                                         guint                       index_,
                                                                         double                     *key,
                                                                         ClutterAnimationMode       *mode,
                                                                         GValue                     *value);
CLUTTER_EXPORT
guint                   clutter_keyframe_transition_get_n_key_frames    (ClutterKeyframeTransition  *transition);

CLUTTER_EXPORT
void                    clutter_keyframe_transition_clear               (ClutterKeyframeTransition  *transition);

G_END_DECLS
