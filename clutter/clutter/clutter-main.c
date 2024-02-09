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

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-backend-private.h"
#include "clutter/clutter-context-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-event-private.h"
#include "clutter/clutter-input-device-private.h"
#include "clutter/clutter-input-pointer-a11y-private.h"
#include "clutter/clutter-graphene.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-settings-private.h"
#include "clutter/clutter-stage.h"
#include "clutter/clutter-stage-manager.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-backend-private.h"

#include "cogl/cogl.h"
#include "cogl-pango/cogl-pango.h"

#include "cally/cally.h" /* For accessibility support */

typedef struct
{
  GSourceFunc func;
  gpointer data;
  GDestroyNotify notify;
} ClutterThreadsDispatch;

/* main context */
static ClutterContext *ClutterCntx       = NULL;

/* command line options */
static gboolean clutter_is_initialized       = FALSE;
static gboolean clutter_enable_accessibility = TRUE;

/* debug flags */
guint clutter_debug_flags       = 0;
guint clutter_paint_debug_flags = 0;
guint clutter_pick_debug_flags  = 0;

/* A constant added to heuristic max render time to account for variations
 * in the estimates.
 */
int clutter_max_render_time_constant_us = 1000;

gboolean
_clutter_context_get_show_fps (void)
{
  ClutterContext *context = _clutter_context_get_default ();

  return context->show_fps;
}

/**
 * clutter_get_accessibility_enabled:
 *
 * Returns whether Clutter has accessibility support enabled.  As
 * least, a value of TRUE means that there are a proper AtkUtil
 * implementation available
 *
 * Return value: %TRUE if Clutter has accessibility support enabled
 */
gboolean
clutter_get_accessibility_enabled (void)
{
  return cally_get_cally_initialized ();
}

/**
 * clutter_disable_accessibility:
 *
 * Disable loading the accessibility support. It has the same effect
 * as setting the environment variable
 * CLUTTER_DISABLE_ACCESSIBILITY. For the same reason, this method
 * should be called before clutter_init().
 */
void
clutter_disable_accessibility (void)
{
  if (clutter_is_initialized)
    {
      g_warning ("clutter_disable_accessibility() can only be called before "
                 "initializing Clutter.");
      return;
    }

  clutter_enable_accessibility = FALSE;
}

static gboolean
_clutter_threads_dispatch (gpointer data)
{
  ClutterThreadsDispatch *dispatch = data;
  gboolean ret = FALSE;

  if (!g_source_is_destroyed (g_main_current_source ()))
    ret = dispatch->func (dispatch->data);

  return ret;
}

static void
_clutter_threads_dispatch_free (gpointer data)
{
  ClutterThreadsDispatch *dispatch = data;

  /* XXX - we cannot hold the thread lock here because the main loop
   * might destroy a source while still in the dispatcher function; so
   * knowing whether the lock is being held or not is not known a priori.
   *
   * see bug: http://bugzilla.gnome.org/show_bug.cgi?id=459555
   */
  if (dispatch->notify)
    dispatch->notify (dispatch->data);

  g_free (dispatch);
}

/**
 * clutter_threads_add_idle_full: (rename-to clutter_threads_add_idle)
 * @priority: the priority of the timeout source. Typically this will be in the
 *    range between #G_PRIORITY_DEFAULT_IDLE and #G_PRIORITY_HIGH_IDLE
 * @func: function to call
 * @data: data to pass to the function
 * @notify: function to call when the idle source is removed
 *
 * Adds a function to be called whenever there are no higher priority
 * events pending. If the function returns %FALSE it is automatically
 * removed from the list of event sources and will not be called again.
 *
 * This function can be considered a thread-safe variant of g_idle_add_full():
 * it will call @function while holding the Clutter lock. It is logically
 * equivalent to the following implementation:
 *
 * ```c
 * static gboolean
 * idle_safe_callback (gpointer data)
 * {
 *    SafeClosure *closure = data;
 *    gboolean res = FALSE;
 *
 *    // the callback does not need to acquire the Clutter
 *     / lock itself, as it is held by the this proxy handler
 *     //
 *    res = closure->callback (closure->data);
 *
 *    return res;
 * }
 * static gulong
 * add_safe_idle (GSourceFunc callback,
 *                gpointer    data)
 * {
 *   SafeClosure *closure = g_new0 (SafeClosure, 1);
 *
 *   closure->callback = callback;
 *   closure->data = data;
 *
 *   return g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
 *                           idle_safe_callback,
 *                           closure,
 *                           g_free)
 * }
 * ```
 *
 * This function should be used by threaded applications to make sure
 * that @func is emitted under the Clutter threads lock and invoked
 * from the same thread that started the Clutter main loop. For instance,
 * it can be used to update the UI using the results from a worker
 * thread:
 *
 * ```c
 * static gboolean
 * update_ui (gpointer data)
 * {
 *   SomeClosure *closure = data;
 *
 *   // it is safe to call Clutter API from this function because
 *    / it is invoked from the same thread that started the main
 *    / loop and under the Clutter thread lock
 *    //
 *   clutter_label_set_text (CLUTTER_LABEL (closure->label),
 *                           closure->text);
 *
 *   g_object_unref (closure->label);
 *   g_free (closure);
 *
 *   return FALSE;
 * }
 *
 *   // within another thread //
 *   closure = g_new0 (SomeClosure, 1);
 *   // always take a reference on GObject instances //
 *   closure->label = g_object_ref (my_application->label);
 *   closure->text = g_strdup (processed_text_to_update_the_label);
 *
 *   clutter_threads_add_idle_full (G_PRIORITY_HIGH_IDLE,
 *                                  update_ui,
 *                                  closure,
 *                                  NULL);
 * ```
 *
 * Return value: the ID (greater than 0) of the event source.
 */
guint
clutter_threads_add_idle_full (gint           priority,
                               GSourceFunc    func,
                               gpointer       data,
                               GDestroyNotify notify)
{
  ClutterThreadsDispatch *dispatch;

  g_return_val_if_fail (func != NULL, 0);

  dispatch = g_new0 (ClutterThreadsDispatch, 1);
  dispatch->func = func;
  dispatch->data = data;
  dispatch->notify = notify;

  return g_idle_add_full (priority,
                          _clutter_threads_dispatch, dispatch,
                          _clutter_threads_dispatch_free);
}

/**
 * clutter_threads_add_idle: (skip)
 * @func: function to call
 * @data: data to pass to the function
 *
 * Simple wrapper around clutter_threads_add_idle_full() using the
 * default priority.
 *
 * Return value: the ID (greater than 0) of the event source.
 */
guint
clutter_threads_add_idle (GSourceFunc func,
                          gpointer    data)
{
  g_return_val_if_fail (func != NULL, 0);

  return clutter_threads_add_idle_full (G_PRIORITY_DEFAULT_IDLE,
                                        func, data,
                                        NULL);
}

/**
 * clutter_threads_add_timeout_full: (rename-to clutter_threads_add_timeout)
 * @priority: the priority of the timeout source. Typically this will be in the
 *            range between #G_PRIORITY_DEFAULT and #G_PRIORITY_HIGH.
 * @interval: the time between calls to the function, in milliseconds
 * @func: function to call
 * @data: data to pass to the function
 * @notify: function to call when the timeout source is removed
 *
 * Sets a function to be called at regular intervals holding the Clutter
 * threads lock, with the given priority. The function is called repeatedly
 * until it returns %FALSE, at which point the timeout is automatically
 * removed and the function will not be called again. The @notify function
 * is called when the timeout is removed.
 *
 * The first call to the function will be at the end of the first @interval.
 *
 * It is important to note that, due to how the Clutter main loop is
 * implemented, the timing will not be accurate and it will not try to
 * "keep up" with the interval.
 *
 * See also clutter_threads_add_idle_full().
 *
 * Return value: the ID (greater than 0) of the event source.
 */
guint
clutter_threads_add_timeout_full (gint           priority,
                                  guint          interval,
                                  GSourceFunc    func,
                                  gpointer       data,
                                  GDestroyNotify notify)
{
  ClutterThreadsDispatch *dispatch;

  g_return_val_if_fail (func != NULL, 0);

  dispatch = g_new0 (ClutterThreadsDispatch, 1);
  dispatch->func = func;
  dispatch->data = data;
  dispatch->notify = notify;

  return g_timeout_add_full (priority,
                             interval,
                             _clutter_threads_dispatch, dispatch,
                             _clutter_threads_dispatch_free);
}

/**
 * clutter_threads_add_timeout: (skip)
 * @interval: the time between calls to the function, in milliseconds
 * @func: function to call
 * @data: data to pass to the function
 *
 * Simple wrapper around clutter_threads_add_timeout_full().
 *
 * Return value: the ID (greater than 0) of the event source.
 */
guint
clutter_threads_add_timeout (guint       interval,
                             GSourceFunc func,
                             gpointer    data)
{
  g_return_val_if_fail (func != NULL, 0);

  return clutter_threads_add_timeout_full (G_PRIORITY_DEFAULT,
                                           interval,
                                           func, data,
                                           NULL);
}

gboolean
_clutter_context_is_initialized (void)
{
  if (ClutterCntx == NULL)
    return FALSE;

  return ClutterCntx->is_initialized;
}

ClutterContext *
_clutter_context_get_default (void)
{
  g_assert (ClutterCntx);
  return ClutterCntx;
}

ClutterContext *
clutter_create_context (ClutterContextFlags         flags,
                        ClutterBackendConstructor   backend_constructor,
                        gpointer                    user_data,
                        GError                    **error)
{
  if (ClutterCntx)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Currently only creating one clutter context is supported");
      return NULL;
    }

  ClutterCntx = clutter_context_new (flags,
                                     backend_constructor, user_data,
                                     error);
  if (!ClutterCntx)
    return NULL;

  clutter_is_initialized = TRUE;
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
    cally_snoop_key_event (stage, (ClutterKeyEvent *) event);

  clutter_stage_emit_event (stage, event);
}

static ClutterActor *
update_device_for_event (ClutterStage *stage,
                         ClutterEvent *event,
                         gboolean      emit_crossing)
{
  ClutterInputDevice *device = clutter_event_get_device (event);
  ClutterInputDevice *source_device = clutter_event_get_source_device (event);
  ClutterEventSequence *sequence = clutter_event_get_event_sequence (event);
  ClutterDeviceUpdateFlags flags = CLUTTER_DEVICE_UPDATE_NONE;
  graphene_point_t point;
  uint32_t time_ms;

  clutter_event_get_coords (event, &point.x, &point.y);
  time_ms = clutter_event_get_time (event);

  if (emit_crossing)
    flags |= CLUTTER_DEVICE_UPDATE_EMIT_CROSSING;

  return clutter_stage_pick_and_update_device (stage,
                                               device,
                                               sequence,
                                               source_device,
                                               flags,
                                               point,
                                               time_ms);
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
  ClutterContext *context = _clutter_context_get_default();
  ClutterActor *event_actor = NULL;
  ClutterEventType event_type;
  gboolean filtered;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));
  g_return_if_fail (event != NULL);

  /* stages in destruction do not process events */
  if (CLUTTER_ACTOR_IN_DESTRUCTION (stage))
    return;

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
      update_device_for_event (stage, event, TRUE);
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

  context = _clutter_context_get_default ();
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
 * clutter_get_font_map:
 *
 * Retrieves the #PangoFontMap instance used by Clutter.
 * You can use the global font map object with the COGL
 * Pango API.
 *
 * Return value: (transfer none): the #PangoFontMap instance. The returned
 *   value is owned by Clutter and it should never be unreferenced.
 */
PangoFontMap *
clutter_get_font_map (void)
{
  return PANGO_FONT_MAP (clutter_context_get_pango_fontmap (ClutterCntx));
}

typedef struct _ClutterRepaintFunction
{
  guint id;
  ClutterRepaintFlags flags;
  GSourceFunc func;
  gpointer data;
  GDestroyNotify notify;
} ClutterRepaintFunction;

/**
 * clutter_threads_remove_repaint_func:
 * @handle_id: an unsigned integer greater than zero
 *
 * Removes the repaint function with @handle_id as its id
 */
void
clutter_threads_remove_repaint_func (guint handle_id)
{
  ClutterRepaintFunction *repaint_func;
  ClutterContext *context;
  GList *l;

  g_return_if_fail (handle_id > 0);

  context = _clutter_context_get_default ();
  l = context->repaint_funcs;
  while (l != NULL)
    {
      repaint_func = l->data;

      if (repaint_func->id == handle_id)
        {
          context->repaint_funcs =
            g_list_remove_link (context->repaint_funcs, l);

          g_list_free (l);

          if (repaint_func->notify)
            repaint_func->notify (repaint_func->data);

          g_free (repaint_func);

          break;
        }

      l = l->next;
    }
}

/**
 * clutter_threads_add_repaint_func:
 * @func: the function to be called within the paint cycle
 * @data: data to be passed to the function, or %NULL
 * @notify: function to be called when removing the repaint
 *    function, or %NULL
 *
 * Adds a function to be called whenever Clutter is processing a new
 * frame.
 *
 * If the function returns %FALSE it is automatically removed from the
 * list of repaint functions and will not be called again.
 *
 * This function is guaranteed to be called from within the same thread
 * that called clutter_main(), and while the Clutter lock is being held;
 * the function will be called within the main loop, so it is imperative
 * that it does not block, otherwise the frame time budget may be lost.
 *
 * A repaint function is useful to ensure that an update of the scenegraph
 * is performed before the scenegraph is repainted. By default, a repaint
 * function added using this function will be invoked prior to the frame
 * being processed.
 *
 * Adding a repaint function does not automatically ensure that a new
 * frame will be queued.
 *
 * When the repaint function is removed (either because it returned %FALSE
 * or because clutter_threads_remove_repaint_func() has been called) the
 * @notify function will be called, if any is set.
 *
 * See also: clutter_threads_add_repaint_func_full()
 *
 * Return value: the ID (greater than 0) of the repaint function. You
 *   can use the returned integer to remove the repaint function by
 *   calling clutter_threads_remove_repaint_func().
 */
guint
clutter_threads_add_repaint_func (GSourceFunc    func,
                                  gpointer       data,
                                  GDestroyNotify notify)
{
  return clutter_threads_add_repaint_func_full (CLUTTER_REPAINT_FLAGS_PRE_PAINT,
                                                func,
                                                data, notify);
}

/**
 * clutter_threads_add_repaint_func_full:
 * @flags: flags for the repaint function
 * @func: the function to be called within the paint cycle
 * @data: data to be passed to the function, or %NULL
 * @notify: function to be called when removing the repaint
 *    function, or %NULL
 *
 * Adds a function to be called whenever Clutter is processing a new
 * frame.
 *
 * If the function returns %FALSE it is automatically removed from the
 * list of repaint functions and will not be called again.
 *
 * This function is guaranteed to be called from within the same thread
 * that called clutter_main(), and while the Clutter lock is being held;
 * the function will be called within the main loop, so it is imperative
 * that it does not block, otherwise the frame time budget may be lost.
 *
 * A repaint function is useful to ensure that an update of the scenegraph
 * is performed before the scenegraph is repainted. The @flags passed to this
 * function will determine the section of the frame processing that will
 * result in @func being called.
 *
 * Adding a repaint function does not automatically ensure that a new
 * frame will be queued.
 *
 * When the repaint function is removed (either because it returned %FALSE
 * or because clutter_threads_remove_repaint_func() has been called) the
 * @notify function will be called, if any is set.
 *
 * Return value: the ID (greater than 0) of the repaint function. You
 *   can use the returned integer to remove the repaint function by
 *   calling clutter_threads_remove_repaint_func().
 */
guint
clutter_threads_add_repaint_func_full (ClutterRepaintFlags flags,
                                       GSourceFunc         func,
                                       gpointer            data,
                                       GDestroyNotify      notify)
{
  ClutterContext *context;
  ClutterRepaintFunction *repaint_func;

  g_return_val_if_fail (func != NULL, 0);

  context = _clutter_context_get_default ();

  repaint_func = g_new0 (ClutterRepaintFunction, 1);

  repaint_func->id = context->last_repaint_id++;

  repaint_func->flags = flags;
  repaint_func->func = func;
  repaint_func->data = data;
  repaint_func->notify = notify;

  context->repaint_funcs = g_list_prepend (context->repaint_funcs,
                                           repaint_func);

  return repaint_func->id;
}

/*
 * _clutter_run_repaint_functions:
 * @flags: only run the repaint functions matching the passed flags
 *
 * Executes the repaint functions added using the
 * clutter_threads_add_repaint_func() function.
 *
 * Must be called with the Clutter thread lock held.
 */
void
_clutter_run_repaint_functions (ClutterRepaintFlags flags)
{
  ClutterContext *context = _clutter_context_get_default ();
  ClutterRepaintFunction *repaint_func;
  GList *invoke_list, *reinvoke_list, *l;

  if (context->repaint_funcs == NULL)
    return;

  /* steal the list */
  invoke_list = context->repaint_funcs;
  context->repaint_funcs = NULL;

  reinvoke_list = NULL;

  /* consume the whole list while we execute the functions */
  while (invoke_list != NULL)
    {
      gboolean res = FALSE;

      repaint_func = invoke_list->data;

      l = invoke_list;
      invoke_list = g_list_remove_link (invoke_list, invoke_list);

      g_list_free (l);

      if ((repaint_func->flags & flags) != 0)
        res = repaint_func->func (repaint_func->data);
      else
        res = TRUE;

      if (res)
        reinvoke_list = g_list_prepend (reinvoke_list, repaint_func);
      else
        {
          if (repaint_func->notify != NULL)
            repaint_func->notify (repaint_func->data);

          g_free (repaint_func);
        }
    }

  if (context->repaint_funcs != NULL)
    {
      context->repaint_funcs = g_list_concat (context->repaint_funcs,
                                              g_list_reverse (reinvoke_list));
    }
  else
    context->repaint_funcs = g_list_reverse (reinvoke_list);
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
