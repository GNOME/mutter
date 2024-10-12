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
#include "config.h"

#include <stdlib.h>
#include <glib/gi18n-lib.h>

#include "clutter/clutter-accessibility-private.h"
#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-input-device-private.h"
#include "clutter/clutter-input-pointer-a11y-private.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-settings-private.h"
#include "clutter/clutter-stage.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-backend-private.h"

#include "cogl/cogl.h"

G_DEFINE_QUARK (clutter_pipeline_capability, clutter_pipeline_capability)

/* main context */
static ClutterContext *ClutterCntx       = NULL;

/* debug flags */
guint clutter_debug_flags       = 0;
guint clutter_paint_debug_flags = 0;
guint clutter_pick_debug_flags  = 0;

/* A constant added to heuristic max render time to account for variations
 * in the estimates.
 */
int clutter_max_render_time_constant_us = 1000;


ClutterContext *
_clutter_context_get_default (void)
{
  g_assert (ClutterCntx);
  return ClutterCntx;
}

ClutterContext *
clutter_create_context (ClutterBackendConstructor   backend_constructor,
                        gpointer                    user_data,
                        GError                    **error)
{
  if (ClutterCntx)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Currently only creating one clutter context is supported");
      return NULL;
    }

  ClutterCntx = clutter_context_new (backend_constructor, user_data,
                                     error);
  if (!ClutterCntx)
    return NULL;

  g_object_add_weak_pointer (G_OBJECT (ClutterCntx), (gpointer *) &ClutterCntx);
  return ClutterCntx;
}

gboolean
_clutter_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                                      GValue                *return_accu,
                                      const GValue          *handler_return,
                                      gpointer               dummy)
{
  gboolean continue_emission;
  gboolean signal_handled;

  signal_handled = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, signal_handled);
  continue_emission = !signal_handled;

  return continue_emission;
}

gboolean
_clutter_boolean_continue_accumulator (GSignalInvocationHint *ihint,
                                       GValue                *return_accu,
                                       const GValue          *handler_return,
                                       gpointer               dummy)
{
  gboolean continue_emission;

  continue_emission = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, continue_emission);

  return continue_emission;
}

/*
 * Emits a pointer event after having prepared the event for delivery (setting
 * source, generating enter/leave etc.).
 */

static inline void
emit_event (ClutterStage *stage,
            ClutterEvent *event)
{
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_KEY_PRESS ||
      event_type == CLUTTER_KEY_RELEASE)
    clutter_accessibility_snoop_key_event (stage, (ClutterKeyEvent *) event);

  clutter_stage_emit_event (stage, event);
}

static void
maybe_remove_device_for_event (ClutterStage *stage,
                               ClutterEvent *event,
                               gboolean      emit_crossing)
{
  ClutterInputDevice *device = clutter_event_get_device (event);
  ClutterEventSequence *sequence = clutter_event_get_event_sequence (event);
  graphene_point_t point;
  uint32_t time;

  if (clutter_event_type (event) == CLUTTER_DEVICE_REMOVED)
    {
      ClutterInputDeviceType device_type =
        clutter_input_device_get_device_type (device);

      if (device_type != CLUTTER_POINTER_DEVICE &&
          device_type != CLUTTER_TABLET_DEVICE &&
          device_type != CLUTTER_PEN_DEVICE &&
          device_type != CLUTTER_ERASER_DEVICE &&
          device_type != CLUTTER_CURSOR_DEVICE)
        return;
    }

  clutter_event_get_coords (event, &point.x, &point.y);
  time = clutter_event_get_time (event);

  clutter_stage_update_device (stage,
                               device, sequence,
                               NULL,
                               point,
                               time,
                               NULL,
                               NULL,
                               TRUE);

  clutter_stage_remove_device_entry (stage, device, sequence);
}

/**
 * clutter_stage_handle_event:
 * @stage: a #ClutterStage.
 * @event: a #ClutterEvent.
 *
 * Processes an event.
 *
 * The @event must be a valid #ClutterEvent and have a #ClutterStage
 * associated to it.
 *
 * This function is only useful when embedding Clutter inside another
 * toolkit, and it should never be called by applications.
 */
void
clutter_stage_handle_event (ClutterStage *stage,
                            ClutterEvent *event)
{
  ClutterContext *context;
  ClutterActor *event_actor = NULL;
  ClutterEventType event_type;
  gboolean filtered;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));
  g_return_if_fail (event != NULL);

  /* stages in destruction do not process events */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (stage))
    return;

  context = clutter_actor_get_context (CLUTTER_ACTOR (stage));
  event_type = clutter_event_type (event);

  switch (event_type)
    {
    case CLUTTER_ENTER:
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_TOUCH_BEGIN:
    case CLUTTER_TOUCH_UPDATE:
    case CLUTTER_TOUCHPAD_PINCH:
    case CLUTTER_TOUCHPAD_SWIPE:
    case CLUTTER_TOUCHPAD_HOLD:
    case CLUTTER_PROXIMITY_IN:
    case CLUTTER_SCROLL:
      clutter_stage_update_device_for_event (stage, event);
      break;
    default:
      break;
    }

  if (event_type != CLUTTER_DEVICE_ADDED &&
      event_type != CLUTTER_DEVICE_REMOVED &&
      event_type != CLUTTER_NOTHING &&
      event_type != CLUTTER_EVENT_LAST)
    {
      event_actor = clutter_stage_get_event_actor (stage, event);
    }

  context->current_event = g_slist_prepend (context->current_event, event);

  filtered = _clutter_event_process_filters (event, event_actor);

  context->current_event =
    g_slist_delete_link (context->current_event, context->current_event);

  if (filtered)
    {
      if (event_type == CLUTTER_MOTION ||
          event_type == CLUTTER_BUTTON_RELEASE ||
          event_type == CLUTTER_TOUCH_UPDATE ||
          event_type == CLUTTER_TOUCH_END ||
          event_type == CLUTTER_TOUCH_CANCEL)
        {
          ClutterInputDevice *device = clutter_event_get_device (event);
          ClutterEventSequence *sequence = clutter_event_get_event_sequence (event);

          clutter_stage_maybe_lost_implicit_grab (stage, device, sequence);
        }
    }
  else
    {
      _clutter_stage_queue_event (stage, event, TRUE);
    }

  if (event_type == CLUTTER_TOUCH_END ||
      event_type == CLUTTER_TOUCH_CANCEL ||
      event_type == CLUTTER_DEVICE_REMOVED)
    {
      _clutter_stage_process_queued_events (stage);
      maybe_remove_device_for_event (stage, event, TRUE);
    }
}

static void
_clutter_process_event_details (ClutterActor    *stage,
                                ClutterContext  *context,
                                ClutterEvent    *event)
{
  switch (clutter_event_type (event))
    {
      case CLUTTER_NOTHING:
        break;

      case CLUTTER_KEY_PRESS:
      case CLUTTER_KEY_RELEASE:
      case CLUTTER_PAD_BUTTON_PRESS:
      case CLUTTER_PAD_BUTTON_RELEASE:
      case CLUTTER_PAD_STRIP:
      case CLUTTER_PAD_RING:
      case CLUTTER_IM_COMMIT:
      case CLUTTER_IM_DELETE:
      case CLUTTER_IM_PREEDIT:
      case CLUTTER_ENTER:
      case CLUTTER_LEAVE:
      case CLUTTER_MOTION:
      case CLUTTER_BUTTON_PRESS:
      case CLUTTER_BUTTON_RELEASE:
      case CLUTTER_SCROLL:
      case CLUTTER_TOUCHPAD_PINCH:
      case CLUTTER_TOUCHPAD_SWIPE:
      case CLUTTER_TOUCHPAD_HOLD:
      case CLUTTER_TOUCH_UPDATE:
      case CLUTTER_TOUCH_BEGIN:
      case CLUTTER_TOUCH_CANCEL:
      case CLUTTER_TOUCH_END:
      case CLUTTER_PROXIMITY_IN:
      case CLUTTER_PROXIMITY_OUT:
        emit_event (CLUTTER_STAGE (stage), event);
        break;

      case CLUTTER_DEVICE_REMOVED:
      case CLUTTER_DEVICE_ADDED:
      case CLUTTER_EVENT_LAST:
        break;
    }
}

/*
 * clutter_stage_process_event
 * @event: a #ClutterEvent.
 *
 * Does the actual work of processing an event that was queued earlier
 * out of clutter_stage_handle_event().
 */
void
clutter_stage_process_event (ClutterStage *stage,
                             ClutterEvent *event)
{
  ClutterContext *context;
  ClutterSeat *seat;

  COGL_TRACE_BEGIN_SCOPED (ProcessEvent, "Clutter::Stage::process_event()");

  context = clutter_actor_get_context (CLUTTER_ACTOR (stage));
  seat = clutter_backend_get_default_seat (context->backend);

  /* push events on a stack, so that we don't need to
   * add an event parameter to all signals that can be emitted within
   * an event chain
   */
  context->current_event = g_slist_prepend (context->current_event, event);

  clutter_seat_handle_event_post (seat, event);
  _clutter_process_event_details (CLUTTER_ACTOR (stage), context, event);

  context->current_event = g_slist_delete_link (context->current_event, context->current_event);
}

/**
 * clutter_get_default_text_direction:
 *
 * Retrieves the default direction for the text. The text direction is
 * determined by the locale and/or by the `CLUTTER_TEXT_DIRECTION`
 * environment variable.
 *
 * The default text direction can be overridden on a per-actor basis by using
 * [method@Actor.set_text_direction].
 *
 * Return value: the default text direction
 */
ClutterTextDirection
clutter_get_default_text_direction (void)
{
  return clutter_context_get_text_direction (ClutterCntx);
}

/*< private >
 * clutter_clear_events_queue:
 *
 * Clears the events queue stored in the main context.
 */
void
_clutter_clear_events_queue (void)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterEvent *event;
  GAsyncQueue *events_queue;

  if (!context->events_queue)
    return;

  g_async_queue_lock (context->events_queue);

  while ((event = g_async_queue_try_pop_unlocked (context->events_queue)))
    clutter_event_free (event);

  events_queue = context->events_queue;
  context->events_queue = NULL;

  g_async_queue_unlock (events_queue);
  g_async_queue_unref (events_queue);
}

/**
 * clutter_add_debug_flags: (skip)
 *
 * Adds the debug flags passed to the list of debug flags.
 */
void
clutter_add_debug_flags (ClutterDebugFlag     debug_flags,
                         ClutterDrawDebugFlag draw_flags,
                         ClutterPickDebugFlag pick_flags)
{
  clutter_debug_flags |= debug_flags;
  clutter_paint_debug_flags |= draw_flags;
  clutter_pick_debug_flags |= pick_flags;
}

/**
 * clutter_remove_debug_flags: (skip)
 *
 * Removes the debug flags passed from the list of debug flags.
 */
void
clutter_remove_debug_flags (ClutterDebugFlag     debug_flags,
                            ClutterDrawDebugFlag draw_flags,
                            ClutterPickDebugFlag pick_flags)
{
  clutter_debug_flags &= ~debug_flags;
  clutter_paint_debug_flags &= ~draw_flags;
  clutter_pick_debug_flags &= ~pick_flags;
}

void
clutter_debug_set_max_render_time_constant (int max_render_time_constant_us)
{
  clutter_max_render_time_constant_us = max_render_time_constant_us;
}

void
clutter_get_debug_flags (ClutterDebugFlag     *debug_flags,
                         ClutterDrawDebugFlag *draw_flags,
                         ClutterPickDebugFlag *pick_flags)
{
  if (debug_flags)
    *debug_flags = clutter_debug_flags;
  if (draw_flags)
    *draw_flags = clutter_paint_debug_flags;
  if (pick_flags)
    *pick_flags = clutter_pick_debug_flags;
}

void
_clutter_debug_messagev (const char *format,
                         va_list     var_args)
{
  static gint64 last_debug_stamp;
  gchar *stamp, *fmt;
  gint64 cur_time, debug_stamp;

  cur_time = g_get_monotonic_time ();

  /* if the last debug message happened less than a second ago, just
   * show the increments instead of the full timestamp
   */
  if (last_debug_stamp == 0 ||
      cur_time - last_debug_stamp >= G_USEC_PER_SEC)
    {
      debug_stamp = cur_time;
      last_debug_stamp = debug_stamp;

      stamp = g_strdup_printf ("[%16" G_GINT64_FORMAT "]", debug_stamp);
    }
  else
    {
      debug_stamp = cur_time - last_debug_stamp;

      stamp = g_strdup_printf ("[%+16" G_GINT64_FORMAT "]", debug_stamp);
    }

  fmt = g_strconcat (stamp, ":", format, NULL);
  g_free (stamp);

  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, var_args);

  g_free (fmt);
}

void
_clutter_debug_message (const char *format, ...)
{
  va_list args;

  va_start (args, format);
  _clutter_debug_messagev (format, args);
  va_end (args);
}

gboolean
_clutter_diagnostic_enabled (void)
{
  static const char *clutter_enable_diagnostic = NULL;

  if (G_UNLIKELY (clutter_enable_diagnostic == NULL))
    {
      clutter_enable_diagnostic = g_getenv ("CLUTTER_ENABLE_DIAGNOSTIC");

      if (clutter_enable_diagnostic == NULL)
        clutter_enable_diagnostic = "0";
    }

  return *clutter_enable_diagnostic != '0';
}
