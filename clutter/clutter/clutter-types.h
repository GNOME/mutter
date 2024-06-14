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

#include <graphene-gobject.h>

#include "cogl/cogl.h"
#include "clutter/clutter-macros.h"
#include "clutter/clutter-enums.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_MARGIN             (clutter_margin_get_type ())
#define CLUTTER_TYPE_PAINT_VOLUME       (clutter_paint_volume_get_type ())
#define CLUTTER_TYPE_PERSPECTIVE        (clutter_perspective_get_type ())

typedef struct _ClutterActor                    ClutterActor;

typedef struct _ClutterContext                  ClutterContext;
typedef struct _ClutterStage                    ClutterStage;
typedef struct _ClutterFrame                    ClutterFrame;
typedef struct _ClutterFrameInfo                ClutterFrameInfo;
typedef struct _ClutterLayoutMeta               ClutterLayoutMeta;
typedef struct _ClutterActorMeta                ClutterActorMeta;
typedef struct _ClutterLayoutManager            ClutterLayoutManager;
typedef struct _ClutterActorIter                ClutterActorIter;
typedef struct _ClutterPaintContext             ClutterPaintContext;
typedef struct _ClutterPaintNode                ClutterPaintNode;
typedef struct _ClutterContent                  ClutterContent; /* dummy */
typedef struct _ClutterScrollActor	        ClutterScrollActor;
typedef struct _ClutterFrameClock               ClutterFrameClock;

typedef struct _ClutterInterval         	ClutterInterval;
typedef struct _ClutterAnimatable       	ClutterAnimatable; /* dummy */
typedef struct _ClutterTimeline         	ClutterTimeline;
typedef struct _ClutterTransition       	ClutterTransition;
typedef struct _ClutterPropertyTransition       ClutterPropertyTransition;
typedef struct _ClutterKeyframeTransition       ClutterKeyframeTransition;
typedef struct _ClutterTransitionGroup		ClutterTransitionGroup;
typedef struct _ClutterText               ClutterText;

typedef struct _ClutterAction                   ClutterAction;
typedef struct _ClutterConstraint               ClutterConstraint;
typedef struct _ClutterEffect                   ClutterEffect;

typedef struct _ClutterColor                    ClutterColor;
typedef struct _ClutterColorState               ClutterColorState;
typedef struct _ClutterMargin                   ClutterMargin;
typedef struct _ClutterPerspective              ClutterPerspective;

typedef struct _ClutterInputDeviceTool          ClutterInputDeviceTool;
typedef struct _ClutterInputDevice              ClutterInputDevice;
typedef struct _ClutterVirtualInputDevice       ClutterVirtualInputDevice;

typedef struct _ClutterInputMethod              ClutterInputMethod;
typedef struct _ClutterInputFocus               ClutterInputFocus;

typedef union _ClutterEvent                     ClutterEvent;

typedef enum _ClutterPaintFlag                  ClutterPaintFlag;

/**
 * ClutterEventSequence:
 *
 * The #ClutterEventSequence structure is an opaque
 * type used to denote the event sequence of a touch event.
 */
typedef struct _ClutterEventSequence            ClutterEventSequence;

/**
 * ClutterPaintVolume:
 *
 * A #ClutterPaintVolume represents a bounding volume whose internal
 * representation isn't defined but can be set and queried in terms
 * of an axis aligned bounding box.
 *
 * A #ClutterPaintVolume for a [class@Actor]
 * is defined to be relative from the current actor modelview matrix.
 *
 * Other internal representation and methods for describing the
 * bounding volume may be added in the future.
 */
typedef struct _ClutterPaintVolume      ClutterPaintVolume;

/*
 * ClutterPaintVolume
 */

CLUTTER_EXPORT
GType clutter_paint_volume_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintVolume *clutter_paint_volume_copy                (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_free                (ClutterPaintVolume       *pv);

CLUTTER_EXPORT
void                clutter_paint_volume_set_origin          (ClutterPaintVolume       *pv,
                                                              const graphene_point3d_t *origin);
CLUTTER_EXPORT
void                clutter_paint_volume_get_origin          (const ClutterPaintVolume *pv,
                                                              graphene_point3d_t       *vertex);
CLUTTER_EXPORT
void                clutter_paint_volume_set_width           (ClutterPaintVolume       *pv,
                                                              gfloat                    width);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_width           (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_set_height          (ClutterPaintVolume       *pv,
                                                              gfloat                    height);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_height          (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_set_depth           (ClutterPaintVolume       *pv,
                                                              gfloat                    depth);
CLUTTER_EXPORT
gfloat              clutter_paint_volume_get_depth           (const ClutterPaintVolume *pv);
CLUTTER_EXPORT
void                clutter_paint_volume_union               (ClutterPaintVolume       *pv,
                                                              const ClutterPaintVolume *another_pv);
CLUTTER_EXPORT
void                clutter_paint_volume_union_box           (ClutterPaintVolume       *pv,
                                                              const graphene_rect_t    *box);

CLUTTER_EXPORT
gboolean            clutter_paint_volume_set_from_allocation (ClutterPaintVolume       *pv,
                                                              ClutterActor             *actor);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterPaintVolume, clutter_paint_volume_free)

/**
 * ClutterMargin:
 * @left: the margin from the left
 * @right: the margin from the right
 * @top: the margin from the top
 * @bottom: the margin from the bottom
 *
 * A representation of the components of a margin.
 */
struct _ClutterMargin
{
  float left;
  float right;
  float top;
  float bottom;
};

CLUTTER_EXPORT
GType clutter_margin_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterMargin * clutter_margin_new      (void) G_GNUC_MALLOC;
CLUTTER_EXPORT
ClutterMargin * clutter_margin_copy     (const ClutterMargin *margin_);
CLUTTER_EXPORT
void            clutter_margin_free     (ClutterMargin       *margin_);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterMargin, clutter_margin_free)

/**
 * ClutterProgressFunc:
 * @a: the initial value of an interval
 * @b: the final value of an interval
 * @progress: the progress factor, between 0 and 1
 * @retval: the value used to store the progress
 *
 * Prototype of the progress function used to compute the value
 * between the two ends @a and @b of an interval depending on
 * the value of @progress.
 *
 * The #GValue in @retval is already initialized with the same
 * type as @a and @b.
 *
 * This function will be called by #ClutterInterval if the
 * type of the values of the interval was registered using
 * [func@Clutter.Interval.register_progress_func].
 *
 * Return value: %TRUE if the function successfully computed
 *   the value and stored it inside @retval
 */
typedef gboolean (* ClutterProgressFunc) (const GValue *a,
                                          const GValue *b,
                                          gdouble       progress,
                                          GValue       *retval);

CLUTTER_EXPORT
void clutter_interval_register_progress_func (GType               value_type,
                                              ClutterProgressFunc func);

G_END_DECLS
