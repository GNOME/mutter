/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "core/events.h"

#include "backends/meta-a11y-manager.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-dnd-private.h"
#include "backends/meta-idle-manager.h"
#include "compositor/compositor-private.h"
#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "meta/meta-backend.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-backend-native.h"
#endif

#ifdef HAVE_WAYLAND
#include "wayland/meta-wayland-private.h"
#endif

#define IS_KEY_EVENT(et) ((et) == CLUTTER_KEY_PRESS || \
                          (et) == CLUTTER_KEY_RELEASE)

static ClutterStage *
stage_from_display (MetaDisplay *display)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  return CLUTTER_STAGE (meta_backend_get_stage (backend));
}

static gboolean
stage_has_key_focus (MetaDisplay *display)
{
  ClutterStage *stage = stage_from_display (display);

  return clutter_stage_get_key_focus (stage) == NULL;
}

static gboolean
stage_has_grab (MetaDisplay *display)
{
  ClutterStage *stage = stage_from_display (display);

  return clutter_stage_get_grab_actor (stage) != NULL;
}

static MetaWindow *
get_window_for_event (MetaDisplay        *display,
                      const ClutterEvent *event,
                      ClutterActor       *event_actor)
{
  MetaWindowActor *window_actor;

  if (stage_has_grab (display))
    return NULL;

  /* Always use the key focused window for key events. */
  if (IS_KEY_EVENT (clutter_event_type (event)))
    {
      return stage_has_key_focus (display) ? display->focus_window
        : NULL;
    }

  window_actor = meta_window_actor_from_actor (event_actor);
  if (window_actor)
    return meta_window_actor_get_meta_window (window_actor);
  else
    return NULL;
}

static void
handle_idletime_for_event (MetaDisplay        *display,
                           const ClutterEvent *event)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaIdleManager *idle_manager;
  ClutterEventType event_type;
  ClutterEventFlags flags;

  flags = clutter_event_get_flags (event);
  event_type = clutter_event_type (event);

  if (flags & CLUTTER_EVENT_FLAG_SYNTHETIC ||
      event_type == CLUTTER_ENTER ||
      event_type == CLUTTER_LEAVE)
    return;

  idle_manager = meta_backend_get_idle_manager (backend);
  meta_idle_manager_reset_idle_time (idle_manager);
}

static gboolean
sequence_is_pointer_emulated (MetaDisplay        *display,
                              const ClutterEvent *event)
{
  ClutterEventSequence *sequence;

  sequence = clutter_event_get_event_sequence (event);

  if (!sequence)
    return FALSE;

  if (clutter_event_get_flags (event) & CLUTTER_EVENT_FLAG_POINTER_EMULATED)
    return TRUE;

#ifdef HAVE_NATIVE_BACKEND
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  /* When using Clutter's native input backend there is no concept of
   * pointer emulating sequence, we still must make up our own to be
   * able to implement single-touch (hence pointer alike) behavior.
   *
   * This is implemented similarly to X11, where only the first touch
   * on screen gets the "pointer emulated" flag, and it won't get assigned
   * to another sequence until the next first touch on an idle touchscreen.
   */
  if (META_IS_BACKEND_NATIVE (backend))
    {
      MetaGestureTracker *tracker;

      tracker = meta_display_get_gesture_tracker (display);

      if (clutter_event_type (event) == CLUTTER_TOUCH_BEGIN &&
          meta_gesture_tracker_get_n_current_touches (tracker) == 0)
        return TRUE;
    }
#endif /* HAVE_NATIVE_BACKEND */

  return FALSE;
}

static gboolean
meta_display_handle_event (MetaDisplay        *display,
                           const ClutterEvent *event,
                           ClutterActor       *event_actor)
{
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaA11yManager *a11y_manager = meta_backend_get_a11y_manager (backend);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  ClutterInputDevice *source_device;
  MetaWindow *window = NULL;
  MetaGestureTracker *gesture_tracker;
  ClutterEventSequence *sequence;
  ClutterEventType event_type;
  ClutterSeat *seat;
  gboolean has_grab;
  gboolean a11y_grabbed;
  MetaTabletActionMapper *mapper;
  MetaEventMode mode_hint;
#ifdef HAVE_WAYLAND
  MetaWaylandCompositor *wayland_compositor;
  MetaWaylandTextInput *wayland_text_input = NULL;
#endif

#ifdef HAVE_WAYLAND
  wayland_compositor = meta_context_get_wayland_compositor (context);
  if (wayland_compositor)
    {
      wayland_text_input =
        meta_wayland_compositor_get_text_input (wayland_compositor);
    }
#endif

  COGL_TRACE_BEGIN_SCOPED (MetaDisplayHandleEvent,
                           "Meta::Display::handle_event()");
  COGL_TRACE_DESCRIBE (MetaDisplayHandleEvent,
                       clutter_event_get_name (event));

  has_grab = stage_has_grab (display);

  sequence = clutter_event_get_event_sequence (event);
  event_type = clutter_event_type (event);

  if (meta_display_process_captured_input (display, event))
    return CLUTTER_EVENT_STOP;

  if (IS_KEY_EVENT (event_type))
    {
      a11y_grabbed = meta_a11y_manager_notify_clients (a11y_manager, event);
      if (a11y_grabbed)
        return CLUTTER_EVENT_STOP;
    }

  source_device = clutter_event_get_source_device (event);
  seat = clutter_input_device_get_seat (clutter_event_get_source_device (event));
  clutter_seat_a11y_update (seat, event);

  /* Set the pointer emulating sequence on touch begin, if eligible */
  if (event_type == CLUTTER_TOUCH_BEGIN)
    {
      if (sequence_is_pointer_emulated (display, event))
        {
          /* This is the new pointer emulating sequence */
          display->pointer_emulating_sequence = sequence;
        }
      else if (display->pointer_emulating_sequence == sequence)
        {
          /* This sequence was "pointer emulating" in a prior incarnation,
           * but now it isn't. We unset the pointer emulating sequence at
           * this point so the current sequence is not mistaken as pointer
           * emulating, while we've ensured that it's been deemed
           * "pointer emulating" throughout all of the event processing
           * of the previous incarnation.
           */
          display->pointer_emulating_sequence = NULL;
        }
    }

#ifdef HAVE_WAYLAND
  if (wayland_text_input &&
      !meta_compositor_get_current_window_drag (compositor) &&
      meta_wayland_text_input_update (wayland_text_input, event))
    return CLUTTER_EVENT_STOP;

  if (wayland_compositor)
    meta_wayland_compositor_update (wayland_compositor, event);
#endif

  if (event_type == CLUTTER_PAD_BUTTON_PRESS ||
      event_type == CLUTTER_PAD_BUTTON_RELEASE ||
      event_type == CLUTTER_PAD_RING ||
      event_type == CLUTTER_PAD_STRIP ||
      event_type == CLUTTER_PAD_DIAL)
    {
      gboolean handle_pad_event;
      gboolean is_mode_switch = FALSE;

      if (event_type == CLUTTER_PAD_BUTTON_PRESS ||
          event_type == CLUTTER_PAD_BUTTON_RELEASE)
        {
          ClutterInputDevice *pad;
          uint32_t button;

          pad = clutter_event_get_source_device (event);
          button = clutter_event_get_button (event);

          is_mode_switch =
            clutter_input_device_get_mode_switch_button_group (pad, button) >= 0;
        }

      handle_pad_event = !display->current_pad_osd || is_mode_switch;
      mapper = META_TABLET_ACTION_MAPPER (display->pad_action_mapper);

      if (handle_pad_event &&
          meta_tablet_action_mapper_handle_event (mapper, event))
        return CLUTTER_EVENT_STOP;
    }
  else if (event_type == CLUTTER_BUTTON_PRESS ||
           event_type == CLUTTER_BUTTON_RELEASE)
    {
      mapper = META_TABLET_ACTION_MAPPER (display->tool_action_mapper);
      if ((!!(clutter_input_device_get_capabilities (source_device) &
              CLUTTER_INPUT_CAPABILITY_TABLET_TOOL) &&
           meta_tablet_action_mapper_handle_event (mapper, event)) ||
          clutter_event_get_button (event) == 0)
        return CLUTTER_EVENT_STOP;
    }

  if (event_type != CLUTTER_DEVICE_ADDED &&
      event_type != CLUTTER_DEVICE_REMOVED)
    {
      handle_idletime_for_event (display, event);
    }
  else
    {
      mapper = META_TABLET_ACTION_MAPPER (display->pad_action_mapper);
      meta_tablet_action_mapper_handle_event (mapper, event);
    }

  if (event_type == CLUTTER_MOTION &&
      !(clutter_input_device_get_capabilities (source_device) &
        CLUTTER_INPUT_CAPABILITY_TABLET_TOOL))
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (backend);

      meta_cursor_tracker_invalidate_position (cursor_tracker);
    }

  window = get_window_for_event (display, event, event_actor);

  if (window && !window->override_redirect &&
      (event_type == CLUTTER_KEY_PRESS ||
       event_type == CLUTTER_BUTTON_PRESS ||
       event_type == CLUTTER_TOUCH_BEGIN))
    {
      if (META_CURRENT_TIME == display->current_time)
        {
          /* We can't use missing (i.e. invalid) timestamps to set user time,
           * nor do we want to use them to sanity check other timestamps.
           * See bug 313490 for more details.
           */
          meta_topic (META_DEBUG_X11,
                      "Event has no timestamp! You may be using a program "
                      "injecting events with invalid timestamps.");
        }
      else
        {
          meta_window_set_user_time (window, display->current_time);
          meta_display_sanity_check_timestamps (display, display->current_time);
        }
    }

  gesture_tracker = meta_display_get_gesture_tracker (display);

  if (meta_gesture_tracker_handle_event (gesture_tracker,
                                         stage_from_display (display),
                                         event))
    return CLUTTER_EVENT_PROPAGATE;

  /* For key events, it's important to enforce single-handling, or
   * we can get into a confused state. So if a keybinding is
   * handled (because it's one of our hot-keys, or because we are
   * in a keyboard-grabbed mode like moving a window, we don't
   * want to pass the key event to the compositor or Wayland at all.
   */
  if (!meta_compositor_get_current_window_drag (compositor) &&
      meta_keybindings_process_event (display, window, event))
    return CLUTTER_EVENT_STOP;

  /* Do not pass keyboard events to Wayland if key focus is not on the
   * stage in normal mode (e.g. during keynav in the panel)
   */
  if (!has_grab)
    {
      if (IS_KEY_EVENT (event_type) && !stage_has_key_focus (display))
        return CLUTTER_EVENT_PROPAGATE;
    }

  if (meta_is_wayland_compositor () &&
      event_type == CLUTTER_SCROLL &&
      meta_prefs_get_mouse_button_mods () > 0)
    {
      ClutterModifierType grab_mods;

      grab_mods = meta_display_get_compositor_modifiers (display);
      if ((clutter_event_get_state (event) & grab_mods) == grab_mods)
        return CLUTTER_EVENT_PROPAGATE;
    }

  if (display->current_pad_osd)
    return CLUTTER_EVENT_PROPAGATE;

  if (stage_has_grab (display))
    return CLUTTER_EVENT_PROPAGATE;

  if (window)
    {
      if (meta_window_handle_ungrabbed_event (window, event))
        return CLUTTER_EVENT_STOP;

      /* If the focus window has an active close dialog let clutter
       * events go through, so fancy clutter dialogs can get to handle
       * all events.
       */
      if (window->close_dialog &&
          meta_close_dialog_is_visible (window->close_dialog))
        return CLUTTER_EVENT_PROPAGATE;

      /* Now replay the button press event to release our own sync grab. */
      mode_hint = META_EVENT_MODE_REPLAY;
    }
  else
    {
      /* We could not match the event with a window, make sure we sync
       * the pointer to discard the sequence and don't keep events frozen.
       */
      mode_hint = META_EVENT_MODE_KEEP_FROZEN;
    }

#ifdef HAVE_WAYLAND
  if (wayland_compositor)
    {
      uint32_t time_ms;

      time_ms = clutter_event_get_time (event);
      if (window && event_type == CLUTTER_MOTION &&
          time_ms != CLUTTER_CURRENT_TIME)
        meta_window_check_alive_on_event (window, time_ms);

      if (meta_wayland_compositor_handle_event (wayland_compositor, event))
        return CLUTTER_EVENT_STOP;
    }
#endif

  return meta_compositor_handle_event (compositor, event, window, mode_hint);
}

static gboolean
event_callback (const ClutterEvent *event,
                ClutterActor       *event_actor,
                gpointer            data)
{
  MetaDisplay *display = data;
  gboolean retval;

  display->current_time = clutter_event_get_time (event);
  retval = meta_display_handle_event (display, event, event_actor);
  display->current_time = META_CURRENT_TIME;

  return retval;
}

void
meta_display_init_events (MetaDisplay *display)
{
  display->clutter_event_filter = clutter_event_add_filter (NULL,
                                                            event_callback,
                                                            NULL,
                                                            display);
}

void
meta_display_free_events (MetaDisplay *display)
{
  clutter_event_remove_filter (display->clutter_event_filter);
  display->clutter_event_filter = 0;
}
