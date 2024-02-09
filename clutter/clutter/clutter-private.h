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
 *
 *
 */

#pragma once

#include <string.h>
#include <glib.h>
#include <pango/pango.h>

#include "cogl-pango/cogl-pango.h"

#include "clutter/clutter-backend.h"
#include "clutter/clutter-context.h"
#include "clutter/clutter-effect.h"
#include "clutter/clutter-event.h"
#include "clutter/clutter-layout-manager.h"
#include "clutter/clutter-settings.h"
#include "clutter/clutter-stage-manager.h"
#include "clutter/clutter-stage.h"

G_BEGIN_DECLS

typedef struct _ClutterContext      ClutterContext;

#define CLUTTER_REGISTER_VALUE_TRANSFORM_TO(TYPE_TO,func)             { \
  g_value_register_transform_func (g_define_type_id, TYPE_TO, func);    \
}

#define CLUTTER_REGISTER_VALUE_TRANSFORM_FROM(TYPE_FROM,func)         { \
  g_value_register_transform_func (TYPE_FROM, g_define_type_id, func);  \
}

#define CLUTTER_REGISTER_INTERVAL_PROGRESS(func)                      { \
  clutter_interval_register_progress_func (g_define_type_id, func);     \
}

#define CLUTTER_PRIVATE_FLAGS(a)	 (((ClutterActor *) (a))->private_flags)
#define CLUTTER_SET_PRIVATE_FLAGS(a,f)	 (CLUTTER_PRIVATE_FLAGS (a) |= (f))
#define CLUTTER_UNSET_PRIVATE_FLAGS(a,f) (CLUTTER_PRIVATE_FLAGS (a) &= ~(f))

#define CLUTTER_ACTOR_IS_TOPLEVEL(a)            ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IS_TOPLEVEL) != FALSE)
#define CLUTTER_ACTOR_IN_DESTRUCTION(a)         ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_DESTRUCTION) != FALSE)
#define CLUTTER_ACTOR_IN_PAINT(a)               ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PAINT) != FALSE)
#define CLUTTER_ACTOR_IN_PICK(a)                ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_PICK) != FALSE)
#define CLUTTER_ACTOR_IN_RELAYOUT(a)            ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_RELAYOUT) != FALSE)
#define CLUTTER_ACTOR_IN_MAP_UNMAP(a)           ((CLUTTER_PRIVATE_FLAGS (a) & CLUTTER_IN_MAP_UNMAP) != FALSE)

#define CLUTTER_PARAM_ANIMATABLE        (1 << G_PARAM_USER_SHIFT)

/* automagic interning of a static string */
#define I_(str)  (g_intern_static_string ((str)))

/* This is a replacement for the nearbyint function which always rounds to the
 * nearest integer. nearbyint is apparently a C99 function so it might not
 * always be available but also it seems in glibc it is defined as a function
 * call so this macro could end up faster anyway. We can't just add 0.5f
 * because it will break for negative numbers. */
#define CLUTTER_NEARBYINT(x) ((int) ((x) < 0.0f ? (x) - 0.5f : (x) + 0.5f))

typedef enum
{
  CLUTTER_ACTOR_UNUSED_FLAG = 0,

  CLUTTER_IN_DESTRUCTION = 1 << 0,
  CLUTTER_IS_TOPLEVEL    = 1 << 1,
  CLUTTER_IN_PREF_WIDTH  = 1 << 3,
  CLUTTER_IN_PREF_HEIGHT = 1 << 4,

  /* Used to avoid recursion */
  CLUTTER_IN_PAINT       = 1 << 5,
  CLUTTER_IN_PICK        = 1 << 6,

  /* Used to avoid recursion */
  CLUTTER_IN_RELAYOUT    = 1 << 7,

  CLUTTER_IN_MAP_UNMAP   = 1 << 8,
} ClutterPrivateFlags;

ClutterContext *        _clutter_context_get_default                    (void);

CLUTTER_EXPORT
gboolean                _clutter_context_is_initialized                 (void);
gboolean                _clutter_context_get_show_fps                   (void);

/* Diagnostic mode */
gboolean        _clutter_diagnostic_enabled     (void);

/* use this function as the accumulator if you have a signal with
 * a G_TYPE_BOOLEAN return value; this will stop the emission as
 * soon as one handler returns TRUE
 */
gboolean _clutter_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                                               GValue                *return_accu,
                                               const GValue          *handler_return,
                                               gpointer               dummy);

/* use this function as the accumulator if you have a signal with
 * a G_TYPE_BOOLEAN return value; this will stop the emission as
 * soon as one handler returns FALSE
 */
gboolean _clutter_boolean_continue_accumulator (GSignalInvocationHint *ihint,
                                                GValue                *return_accu,
                                                const GValue          *handler_return,
                                                gpointer               dummy);

void _clutter_run_repaint_functions (ClutterRepaintFlags flags);

void  _clutter_util_fully_transform_vertices (const graphene_matrix_t  *modelview,
                                              const graphene_matrix_t  *projection,
                                              const float              *viewport,
                                              const graphene_point3d_t *vertices_in,
                                              graphene_point3d_t       *vertices_out,
                                              int                       n_vertices);

CLUTTER_EXPORT
ClutterTextDirection clutter_unichar_direction (gunichar ch);

ClutterTextDirection _clutter_find_base_dir (const gchar *text,
                                             gint         length);

PangoDirection
clutter_text_direction_to_pango_direction (ClutterTextDirection dir);

typedef enum _ClutterCullResult
{
  CLUTTER_CULL_RESULT_UNKNOWN,
  CLUTTER_CULL_RESULT_IN,
  CLUTTER_CULL_RESULT_OUT,
} ClutterCullResult;

gboolean        _clutter_has_progress_function  (GType gtype);
gboolean        _clutter_run_progress_function  (GType gtype,
                                                 const GValue *initial,
                                                 const GValue *final,
                                                 gdouble progress,
                                                 GValue *retval);

void            clutter_timeline_cancel_delay (ClutterTimeline *timeline);

static inline void
clutter_round_to_256ths (float *f)
{
  *f = roundf ((*f) * 256) / 256;
}

static inline uint64_t
ns (uint64_t ns)
{
  return ns;
}

static inline int64_t
us (int64_t us)
{
  return us;
}

static inline int64_t
ms (int64_t ms)
{
  return ms;
}

static inline int64_t
ms2us (int64_t ms)
{
  return us (ms * 1000);
}

static inline int64_t
us2ns (int64_t us)
{
  return ns (us * 1000);
}

static inline int64_t
us2ms (int64_t us)
{
  return (int64_t) (us / 1000);
}

static inline int64_t
ns2us (int64_t ns)
{
  return us (ns / 1000);
}

static inline int64_t
s2us (int64_t s)
{
  return s * G_USEC_PER_SEC;
}

static inline int64_t
us2s (int64_t us)
{
  return us / G_USEC_PER_SEC;
}

static inline int64_t
s2ns (int64_t s)
{
  return us2ns (s2us (s));
}

static inline int64_t
s2ms (int64_t s)
{
  return (int64_t) ms (s * 1000);
}

G_END_DECLS
