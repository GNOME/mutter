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

#include "clutter/clutter-actor.h"
#include "clutter/clutter-stage.h"
#include <pango/pango.h>

G_BEGIN_DECLS

typedef enum
{
  CLUTTER_DEBUG_MISC                = 1 << 0,
  CLUTTER_DEBUG_ACTOR               = 1 << 1,
  CLUTTER_DEBUG_TEXTURE             = 1 << 2,
  CLUTTER_DEBUG_EVENT               = 1 << 3,
  CLUTTER_DEBUG_PAINT               = 1 << 4,
  CLUTTER_DEBUG_PANGO               = 1 << 5,
  CLUTTER_DEBUG_BACKEND             = 1 << 6,
  CLUTTER_DEBUG_SCHEDULER           = 1 << 7,
  CLUTTER_DEBUG_SCRIPT              = 1 << 8,
  CLUTTER_DEBUG_SHADER              = 1 << 9,
  CLUTTER_DEBUG_MULTISTAGE          = 1 << 10,
  CLUTTER_DEBUG_ANIMATION           = 1 << 11,
  CLUTTER_DEBUG_LAYOUT              = 1 << 12,
  CLUTTER_DEBUG_PICK                = 1 << 13,
  CLUTTER_DEBUG_EVENTLOOP           = 1 << 14,
  CLUTTER_DEBUG_CLIPPING            = 1 << 15,
  CLUTTER_DEBUG_OOB_TRANSFORMS      = 1 << 16,
  CLUTTER_DEBUG_FRAME_TIMINGS       = 1 << 17,
  CLUTTER_DEBUG_DETAILED_TRACE      = 1 << 18,
  CLUTTER_DEBUG_GRABS               = 1 << 19,
  CLUTTER_DEBUG_FRAME_CLOCK         = 1 << 20,
  CLUTTER_DEBUG_GESTURES            = 1 << 21,
} ClutterDebugFlag;

typedef enum
{
  CLUTTER_DEBUG_NOP_PICKING = 1 << 0,
} ClutterPickDebugFlag;

typedef enum
{
  CLUTTER_DEBUG_DISABLE_SWAP_EVENTS             = 1 << 0,
  CLUTTER_DEBUG_DISABLE_CLIPPED_REDRAWS         = 1 << 1,
  CLUTTER_DEBUG_REDRAWS                         = 1 << 2,
  CLUTTER_DEBUG_PAINT_VOLUMES                   = 1 << 3,
  CLUTTER_DEBUG_DISABLE_CULLING                 = 1 << 4,
  CLUTTER_DEBUG_DISABLE_OFFSCREEN_REDIRECT      = 1 << 5,
  CLUTTER_DEBUG_CONTINUOUS_REDRAW               = 1 << 6,
  CLUTTER_DEBUG_PAINT_DEFORM_TILES              = 1 << 7,
  CLUTTER_DEBUG_PAINT_DAMAGE_REGION             = 1 << 8,
  CLUTTER_DEBUG_DISABLE_DYNAMIC_MAX_RENDER_TIME = 1 << 9,
  CLUTTER_DEBUG_PAINT_MAX_RENDER_TIME           = 1 << 10,
} ClutterDrawDebugFlag;

/**
 * CLUTTER_PRIORITY_REDRAW:
 *
 * Priority of the redraws. This is chosen to be lower than the GTK+
 * redraw and resize priorities, because in application with both
 * GTK+ and Clutter it's more likely that the Clutter part will be
 * continually animating (and thus able to starve GTK+) than
 * vice-versa.
 */
#define CLUTTER_PRIORITY_REDRAW         (G_PRIORITY_HIGH_IDLE + 50)

CLUTTER_EXPORT
void                    clutter_stage_handle_event              (ClutterStage *stage,
                                                                 ClutterEvent *event);

/* Debug utility functions */
CLUTTER_EXPORT
gboolean                clutter_get_accessibility_enabled       (void);

CLUTTER_EXPORT
void                    clutter_disable_accessibility           (void);

/* Threading functions */
CLUTTER_EXPORT
guint                   clutter_threads_add_idle                (GSourceFunc    func,
                                                                 gpointer       data);
CLUTTER_EXPORT
guint                   clutter_threads_add_idle_full           (gint           priority,
                                                                 GSourceFunc    func,
                                                                 gpointer       data,
                                                                 GDestroyNotify notify);
CLUTTER_EXPORT
guint                   clutter_threads_add_timeout             (guint          interval,
                                                                 GSourceFunc    func,
                                                                 gpointer       data);
CLUTTER_EXPORT
guint                   clutter_threads_add_timeout_full        (gint           priority,
                                                                 guint          interval,
                                                                 GSourceFunc    func,
                                                                 gpointer       data,
                                                                 GDestroyNotify notify);
CLUTTER_EXPORT
guint                   clutter_threads_add_repaint_func        (GSourceFunc    func,
                                                                 gpointer       data,
                                                                 GDestroyNotify notify);
CLUTTER_EXPORT
guint                   clutter_threads_add_repaint_func_full   (ClutterRepaintFlags flags,
                                                                 GSourceFunc    func,
                                                                 gpointer       data,
                                                                 GDestroyNotify notify);
CLUTTER_EXPORT
void                    clutter_threads_remove_repaint_func     (guint          handle_id);

CLUTTER_EXPORT
PangoFontMap *          clutter_get_font_map                    (void);

CLUTTER_EXPORT
ClutterTextDirection    clutter_get_default_text_direction      (void);

CLUTTER_EXPORT
void                    clutter_add_debug_flags                 (ClutterDebugFlag     debug_flags,
                                                                 ClutterDrawDebugFlag draw_flags,
                                                                 ClutterPickDebugFlag pick_flags);

CLUTTER_EXPORT
void                    clutter_remove_debug_flags              (ClutterDebugFlag     debug_flags,
                                                                 ClutterDrawDebugFlag draw_flags,
                                                                 ClutterPickDebugFlag pick_flags);

CLUTTER_EXPORT
void                    clutter_debug_set_max_render_time_constant (int max_render_time_constant_us);

CLUTTER_EXPORT
ClutterTextDirection    clutter_get_text_direction (void);

G_END_DECLS
