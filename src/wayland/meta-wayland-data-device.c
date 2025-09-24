/*
 * Copyright © 2011 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* The file is based on src/data-device.c from Weston */

#include "config.h"

#include "wayland/meta-wayland-data-device.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/meta-dnd-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "compositor/meta-dnd-actor-private.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-drag.h"
#include "core/meta-selection-private.h"
#include "meta/meta-debug.h"
#include "meta/meta-selection-source-memory.h"
#include "meta/meta-wayland-surface.h"
#include "wayland/meta-selection-source-wayland-private.h"
#include "wayland/meta-wayland-data-source.h"
#include "wayland/meta-wayland-dnd-surface.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-toplevel-drag.h"
#include "wayland/meta-wayland-types.h"

#ifdef HAVE_X11_CLIENT
#include "wayland/meta-xwayland-dnd-private.h"
#endif

#define ROOTWINDOW_DROP_MIME "application/x-rootwindow-drop"

static void unset_selection_source (MetaWaylandDataDevice *data_device,
                                    MetaSelectionType      selection_type);

static void
drag_grab_data_source_destroyed (gpointer data, GObject *where_the_object_was);

static struct wl_resource * create_and_send_clipboard_offer (MetaWaylandDataDevice *data_device,
                                                             struct wl_resource    *target);

static MetaDisplay *
display_from_data_device (MetaWaylandDataDevice *data_device)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (data_device->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);

  return meta_context_get_display (context);
}

static void
move_resources (struct wl_list *destination,
                struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list   *destination,
			   struct wl_list   *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;
  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
default_destructor (struct wl_client   *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static struct wl_resource *
create_and_send_dnd_offer (MetaWaylandDataSource *source,
                           struct wl_resource *target)
{
  MetaWaylandCompositor *compositor;
  MetaWaylandDataOffer *offer;
  struct wl_array *mime_types;
  struct wl_resource *resource;
  char **p;

  compositor = meta_wayland_data_source_get_compositor (source);
  offer = meta_wayland_data_offer_new (compositor,
                                       META_SELECTION_DND,
                                       source, target);
  resource = meta_wayland_data_offer_get_resource (offer);

  wl_data_device_send_data_offer (target, resource);

  mime_types = meta_wayland_data_source_get_mime_types (source);

  wl_array_for_each (p, mime_types)
    wl_data_offer_send_offer (resource, *p);

  meta_wayland_data_offer_update_action (offer);
  meta_wayland_data_source_set_current_offer (source, offer);

  return resource;
}

struct _MetaWaylandDragGrab {
  MetaWaylandEventHandler *handler;

  ClutterSprite *sprite;

  MetaWaylandSeat        *seat;
  struct wl_client       *drag_client;

  MetaWaylandSurface     *drag_focus;
  gulong                  drag_focus_destroy_handler_id;
  struct wl_resource     *drag_focus_data_device;
  struct wl_listener      drag_focus_listener;

  MetaWaylandSurface     *drag_surface;
  struct wl_listener      drag_icon_listener;

  MetaWaylandDataSource  *drag_data_source;

  ClutterActor           *feedback_actor;

  MetaWaylandSurface     *drag_origin;
  struct wl_listener      drag_origin_listener;

  int                     drag_start_x, drag_start_y;
  ClutterModifierType     buttons;

  guint                   need_initial_focus : 1;
};

static void
set_selection_source (MetaWaylandDataDevice *data_device,
                      MetaSelectionType      selection_type,
                      MetaSelectionSource   *selection_source)

{
  MetaDisplay *display = display_from_data_device (data_device);

  meta_selection_set_owner (meta_display_get_selection (display),
                            selection_type, selection_source);
  g_set_object (&data_device->owners[selection_type], selection_source);
}

static void
unset_selection_source (MetaWaylandDataDevice *data_device,
                        MetaSelectionType      selection_type)
{
  MetaDisplay *display = display_from_data_device (data_device);

  if (!data_device->owners[selection_type])
    return;

  meta_selection_unset_owner (meta_display_get_selection (display),
                              selection_type,
                              data_device->owners[selection_type]);
  g_clear_object (&data_device->owners[selection_type]);
}

static void
destroy_drag_focus (struct wl_listener *listener,
                    void               *data)
{
  MetaWaylandDragGrab *grab = wl_container_of (listener, grab, drag_focus_listener);

  grab->drag_focus_data_device = NULL;
  wl_list_remove (&grab->drag_focus_listener.link);

  g_clear_signal_handler (&grab->drag_focus_destroy_handler_id,
                          grab->drag_focus);
  grab->drag_focus = NULL;
}

static void
on_drag_focus_destroyed (MetaWaylandSurface  *surface,
                         MetaWaylandDragGrab *grab)
{
  meta_wayland_surface_drag_dest_focus_out (grab->drag_focus);
  grab->drag_focus = NULL;
}

static void
meta_wayland_drag_grab_set_cursor (MetaWaylandDragGrab *drag_grab,
                                   MetaCursor           cursor)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (drag_grab->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  g_autoptr (MetaCursorSprite) cursor_sprite = NULL;
  MetaCursorRenderer *cursor_renderer;

#ifdef HAVE_X11_CLIENT
  /* X11 DnD lets the drag source client drive pointer cursor updates */
  if (META_IS_WAYLAND_DATA_SOURCE_XWAYLAND (drag_grab->drag_data_source))
    return;
#endif

  cursor_sprite =
    META_CURSOR_SPRITE (meta_cursor_sprite_xcursor_new (cursor, cursor_tracker));

  cursor_renderer =
    meta_backend_get_cursor_renderer_for_sprite (backend, drag_grab->sprite);

  if (cursor_renderer && cursor_sprite)
    {
      if (cursor_renderer == meta_backend_get_cursor_renderer (backend))
        meta_cursor_tracker_set_window_cursor (cursor_tracker, cursor_sprite);
      else
        meta_cursor_renderer_set_cursor (cursor_renderer, cursor_sprite);
    }
}

static void
meta_wayland_drag_grab_update_cursor (MetaWaylandDragGrab *drag_grab)
{
  enum wl_data_device_manager_dnd_action action =
    meta_wayland_data_source_get_current_action (drag_grab->drag_data_source);
  MetaCursor cursor = META_CURSOR_DEFAULT;

  switch (action)
    {
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE:
      cursor = META_CURSOR_NO_DROP;
      break;
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE:
      cursor = META_CURSOR_MOVE;
      break;
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY:
      cursor = META_CURSOR_COPY;
      break;
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK:
      cursor = META_CURSOR_DND_ASK;
      break;
    default:
      break;
    }

  meta_wayland_drag_grab_set_cursor (drag_grab, cursor);
}

static void
on_data_source_action_changed (MetaWaylandDataSource *source,
                               MetaWaylandDragGrab   *drag_grab)
{
  meta_wayland_drag_grab_update_cursor (drag_grab);
}

static void
meta_wayland_drag_grab_set_source (MetaWaylandDragGrab   *drag_grab,
                                   MetaWaylandDataSource *source)
{
  if (drag_grab->drag_data_source)
    {
      g_signal_handlers_disconnect_by_func (drag_grab->drag_data_source,
                                            on_data_source_action_changed,
                                            drag_grab);
      g_object_weak_unref (G_OBJECT (drag_grab->drag_data_source),
                           drag_grab_data_source_destroyed,
                           drag_grab);
    }

  drag_grab->drag_data_source = source;

  if (source)
    {
      g_signal_connect (drag_grab->drag_data_source, "action-changed",
                        G_CALLBACK (on_data_source_action_changed),
                        drag_grab);
      g_object_weak_ref (G_OBJECT (source),
                         drag_grab_data_source_destroyed,
                         drag_grab);
    }
}

static void
meta_wayland_drag_source_fake_acceptance (MetaWaylandDataSource *source,
                                          const gchar           *mimetype)
{
  uint32_t actions, user_action, action = 0;

  meta_wayland_data_source_get_actions (source, &actions);
  user_action = meta_wayland_data_source_get_user_action (source);

  /* Pick a suitable action */
  if ((user_action & actions) != 0)
    action = user_action;
  else if (actions != 0)
    action = 1 << (ffs (actions) - 1);

  /* Bail out if there is none, source didn't cooperate */
  if (action == 0)
    return;

  meta_wayland_data_source_target (source, mimetype);
  meta_wayland_data_source_set_current_action (source, action);
  meta_wayland_data_source_set_has_target (source, TRUE);
}

void
meta_wayland_drag_grab_set_focus (MetaWaylandDragGrab *drag_grab,
                                  MetaWaylandSurface  *surface)
{
  MetaWaylandSeat *seat = drag_grab->seat;
  MetaWaylandDataSource *source = drag_grab->drag_data_source;
  struct wl_client *client;
  struct wl_resource *data_device_resource, *offer = NULL;

  if (!drag_grab->need_initial_focus &&
      drag_grab->drag_focus == surface)
    return;

  drag_grab->need_initial_focus = FALSE;

  if (drag_grab->drag_focus)
    {
      meta_wayland_surface_drag_dest_focus_out (drag_grab->drag_focus);
      g_clear_signal_handler (&drag_grab->drag_focus_destroy_handler_id,
                              drag_grab->drag_focus);
      drag_grab->drag_focus = NULL;
    }

  if (source)
    meta_wayland_data_source_set_current_offer (source, NULL);

  if (!surface && source &&
      meta_wayland_data_source_has_mime_type (source, ROOTWINDOW_DROP_MIME))
    meta_wayland_drag_source_fake_acceptance (source, ROOTWINDOW_DROP_MIME);
  else if (source)
    meta_wayland_data_source_target (source, NULL);

  if (!surface)
    return;

  if (!source &&
      wl_resource_get_client (surface->resource) != drag_grab->drag_client)
    return;

  client = wl_resource_get_client (surface->resource);

  data_device_resource = wl_resource_find_for_client (&seat->data_device.resource_list, client);
  if (!data_device_resource)
    {
      data_device_resource =
        wl_resource_find_for_client (&seat->data_device.focus_resource_list,
                                     client);
    }

  if (source && data_device_resource)
    offer = create_and_send_dnd_offer (source, data_device_resource);

  drag_grab->drag_focus = surface;
  drag_grab->drag_focus_destroy_handler_id =
    g_signal_connect (surface, "destroy",
                      G_CALLBACK (on_drag_focus_destroyed),
                      drag_grab);
  drag_grab->drag_focus_data_device = data_device_resource;

  meta_wayland_surface_drag_dest_focus_in (drag_grab->drag_focus,
                                           offer ? wl_resource_get_user_data (offer) : NULL);
}

MetaWaylandSurface *
meta_wayland_drag_grab_get_focus (MetaWaylandDragGrab *drag_grab)
{
  return drag_grab->drag_focus;
}

MetaWaylandSeat *
meta_wayland_drag_grab_get_seat (MetaWaylandDragGrab *drag_grab)
{
  return drag_grab->seat;
}

ClutterSprite *
meta_wayland_drag_grab_get_sprite (MetaWaylandDragGrab *drag_grab)
{
  return drag_grab->sprite;
}

MetaWaylandSurface *
meta_wayland_drag_grab_get_origin (MetaWaylandDragGrab *drag_grab)
{
  return drag_grab->drag_origin;
}

MetaWaylandDataSource *
meta_wayland_drag_grab_get_data_source (MetaWaylandDragGrab *drag_grab)
{
  return drag_grab->drag_data_source;
}

static void
data_source_update_user_dnd_action (MetaWaylandDataSource *source,
                                    ClutterModifierType    modifiers)
{
  enum wl_data_device_manager_dnd_action user_dnd_action = 0;

  if (modifiers & CLUTTER_SHIFT_MASK)
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  else if (modifiers & CLUTTER_CONTROL_MASK)
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  else if (modifiers & (CLUTTER_MOD1_MASK | CLUTTER_BUTTON2_MASK))
    user_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;

  meta_wayland_data_source_set_user_action (source, user_dnd_action);
}

static void
data_device_end_drag_grab (MetaWaylandDragGrab *drag_grab)
{
  MetaWaylandDataDevice *data_device = &drag_grab->seat->data_device;
  MetaDisplay *display = display_from_data_device (data_device);
  MetaCompositor *compositor = meta_display_get_compositor (display);

  meta_wayland_drag_grab_set_cursor (drag_grab, META_CURSOR_DEFAULT);

  meta_wayland_drag_grab_set_source (drag_grab, NULL);
  meta_wayland_drag_grab_set_focus (drag_grab, NULL);

  if (drag_grab->drag_origin)
    {
      drag_grab->drag_origin = NULL;
      wl_list_remove (&drag_grab->drag_origin_listener.link);
    }

  if (drag_grab->drag_surface)
    {
      drag_grab->drag_surface = NULL;
      wl_list_remove (&drag_grab->drag_icon_listener.link);
    }

  if (drag_grab->feedback_actor)
    {
      clutter_actor_remove_all_children (drag_grab->feedback_actor);
      clutter_actor_destroy (drag_grab->feedback_actor);
    }

  drag_grab->seat->data_device.current_grab = NULL;

  if (drag_grab->handler)
    {
      MetaWaylandInput *input;

      input = meta_wayland_seat_get_input (data_device->seat);
      meta_wayland_input_detach_event_handler (input, drag_grab->handler);
      drag_grab->handler = NULL;
    }

  meta_dnd_wayland_handle_end_modal (compositor);

  g_free (drag_grab);
}

static MetaWaylandSurface *
drag_grab_get_focus_surface (MetaWaylandEventHandler *handler,
                             ClutterFocus            *focus,
                             gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;

  if (!CLUTTER_IS_SPRITE (focus) || drag_grab->sprite != CLUTTER_SPRITE (focus))
    return NULL;

  return meta_wayland_seat_get_current_surface (drag_grab->seat, focus);
}

static void
drag_grab_focus (MetaWaylandEventHandler *handler,
                 ClutterFocus            *focus,
                 MetaWaylandSurface      *surface,
                 gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;

  meta_wayland_event_handler_chain_up_focus (handler, focus, NULL);

  if (CLUTTER_IS_SPRITE (focus) && drag_grab->sprite == CLUTTER_SPRITE (focus))
    meta_wayland_drag_grab_set_focus (drag_grab, surface);
}

static void
data_device_update_position (MetaWaylandDragGrab *drag_grab,
                             graphene_point_t    *point)
{
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (drag_grab->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaFeedbackActor *feedback_actor =
    META_FEEDBACK_ACTOR (drag_grab->feedback_actor);
  MetaLogicalMonitor *monitor;

  if (!drag_grab->drag_surface)
    return;

  meta_feedback_actor_set_position (feedback_actor, point->x, point->y);

  monitor = meta_monitor_manager_get_logical_monitor_at (monitor_manager,
                                                         point->x, point->y);
  meta_wayland_surface_set_main_monitor (drag_grab->drag_surface, monitor);
}

static gboolean
is_dragging_window (MetaWaylandSeat *seat)
{
  return meta_wayland_data_device_get_toplevel_drag (&seat->data_device) != NULL;
}

static gboolean
drag_grab_motion (MetaWaylandEventHandler *handler,
		  const ClutterEvent      *event,
                  gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (drag_grab->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterSprite *clutter_sprite;
  graphene_point_t point;
  uint32_t time_ms;

  clutter_sprite = clutter_backend_get_sprite (clutter_backend, stage, event);

  if (drag_grab->sprite != clutter_sprite)
    return CLUTTER_EVENT_STOP;

  clutter_event_get_position (event, &point);

  if (drag_grab->drag_focus)
    {
      time_ms = clutter_event_get_time (event);
      meta_wayland_surface_drag_dest_motion (drag_grab->drag_focus,
                                             point.x, point.y, time_ms);
    }

  data_device_update_position (drag_grab, &point);

  meta_dnd_wayland_on_motion_event (meta_backend_get_dnd (backend), event);

  return !is_dragging_window (drag_grab->seat);
}

static gboolean
drag_grab_release (MetaWaylandEventHandler *handler,
                   const ClutterEvent      *event,
                   gpointer                 user_data)
{
  MetaWaylandDragGrab *drag_grab = user_data;
  MetaWaylandSeat *seat = drag_grab->seat;
  MetaWaylandDataSource *source = drag_grab->drag_data_source;
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (drag_grab->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterSprite *clutter_sprite;
  MetaWaylandToplevelDrag *toplevel_drag;
  gboolean success;

  clutter_sprite = clutter_backend_get_sprite (clutter_backend, stage, event);

  if (drag_grab->sprite != clutter_sprite)
    return CLUTTER_EVENT_STOP;

  if (__builtin_popcount (clutter_event_get_state (event) &
                          (CLUTTER_BUTTON1_MASK |
                           CLUTTER_BUTTON2_MASK |
                           CLUTTER_BUTTON3_MASK |
                           CLUTTER_BUTTON4_MASK |
                           CLUTTER_BUTTON5_MASK)) > 1)
    return CLUTTER_EVENT_STOP;

  toplevel_drag = meta_wayland_data_device_get_toplevel_drag (&seat->data_device);
  if (toplevel_drag)
    {
      meta_topic (META_DEBUG_WAYLAND, "Will end xdg_toplevel_drag#%u.",
                  wl_resource_get_id (toplevel_drag->resource));
      meta_wayland_data_source_notify_drop_performed (source);
      meta_wayland_toplevel_drag_end (toplevel_drag);
    }

  if (drag_grab->drag_focus && source &&
      meta_wayland_data_source_has_target (source) &&
      meta_wayland_data_source_get_current_action (source))
    {
      meta_wayland_surface_drag_dest_drop (drag_grab->drag_focus);
      if (!meta_wayland_data_source_get_drop_performed (source))
        meta_wayland_data_source_notify_drop_performed (source);

      meta_wayland_data_source_update_in_ask (source);
      success = TRUE;
    }
  else if (!drag_grab->drag_focus && source &&
           meta_wayland_data_source_has_target (source) &&
           meta_wayland_data_source_get_current_action (source) &&
           meta_wayland_data_source_has_mime_type (source,
                                                   ROOTWINDOW_DROP_MIME))
    {
      /* Perform a fake read, that will lead to notify_finish() being called */
      meta_wayland_data_source_fake_read (source, ROOTWINDOW_DROP_MIME);
      success = TRUE;
    }
  else
    {
      if (source)
        meta_wayland_data_source_set_current_offer (source, NULL);
      meta_wayland_data_device_set_dnd_source (&seat->data_device, NULL);
      unset_selection_source (&seat->data_device, META_SELECTION_DND);
      success = FALSE;
    }

  /* Finish drag and let actor self-destruct */
  if (drag_grab->feedback_actor)
    {
      meta_dnd_actor_drag_finish (META_DND_ACTOR (drag_grab->feedback_actor),
                                  success);
      drag_grab->feedback_actor = NULL;
    }

  data_device_end_drag_grab (drag_grab);

  return CLUTTER_EVENT_STOP;
}

static gboolean
drag_grab_key (MetaWaylandEventHandler *handler,
               const ClutterEvent      *event,
               gpointer                 user_data)
{
  MetaWaylandToplevelDrag *toplevel_drag;
  MetaWaylandDragGrab *drag_grab = user_data;
  ClutterEventType event_type;

  event_type = clutter_event_type (event);

  if (event_type == CLUTTER_KEY_PRESS &&
      clutter_event_get_key_symbol (event) == CLUTTER_KEY_Escape)
    {
      toplevel_drag = meta_wayland_data_device_get_toplevel_drag (&drag_grab->seat->data_device);
      if (toplevel_drag)
        {
          meta_topic (META_DEBUG_WAYLAND, "Will cancel xdg_toplevel_drag#%u.",
                      wl_resource_get_id (toplevel_drag->resource));
          meta_wayland_toplevel_drag_end (toplevel_drag);
        }

      meta_wayland_data_device_set_dnd_source (&drag_grab->seat->data_device,
                                               NULL);
      unset_selection_source (&drag_grab->seat->data_device, META_SELECTION_DND);
      meta_wayland_data_source_set_current_offer (drag_grab->drag_data_source,
                                                  NULL);
      meta_dnd_actor_drag_finish (META_DND_ACTOR (drag_grab->feedback_actor),
                                  FALSE);
      drag_grab->feedback_actor = NULL;
      data_device_end_drag_grab (drag_grab);
    }
  else if (event_type == CLUTTER_KEY_STATE &&
           drag_grab->drag_data_source)
    {
      data_source_update_user_dnd_action (drag_grab->drag_data_source,
                                          clutter_event_get_state (event));

      if (drag_grab->drag_focus)
        meta_wayland_surface_drag_dest_update (drag_grab->drag_focus);
    }

  return CLUTTER_EVENT_STOP;
}

static gboolean
drag_grab_discard_event (MetaWaylandEventHandler *handler,
                         const ClutterEvent      *event,
                         gpointer                 user_data)
{
  return CLUTTER_EVENT_STOP;
}

static const MetaWaylandEventInterface dnd_event_interface = {
  drag_grab_get_focus_surface,
  drag_grab_focus,
  drag_grab_motion,
  drag_grab_discard_event, /* press */
  drag_grab_release,
  drag_grab_key,
  drag_grab_discard_event, /* other */
};

static void
destroy_data_device_origin (struct wl_listener *listener,
                            void               *data)
{
  MetaWaylandDragGrab *drag_grab =
    wl_container_of (listener, drag_grab, drag_origin_listener);

  drag_grab->drag_origin = NULL;
  meta_wayland_data_device_set_dnd_source (&drag_grab->seat->data_device, NULL);
  unset_selection_source (&drag_grab->seat->data_device, META_SELECTION_DND);
  meta_wayland_data_source_set_current_offer (drag_grab->drag_data_source, NULL);
  data_device_end_drag_grab (drag_grab);
}

static void
drag_grab_data_source_destroyed (gpointer  data,
                                 GObject  *where_the_object_was)
{
  MetaWaylandDragGrab *drag_grab = data;

  drag_grab->drag_data_source = NULL;
  data_device_end_drag_grab (drag_grab);
}

static void
destroy_data_device_icon (struct wl_listener *listener,
                          void               *data)
{
  MetaWaylandDragGrab *drag_grab =
    wl_container_of (listener, drag_grab, drag_icon_listener);

  drag_grab->drag_surface = NULL;
  wl_list_remove (&drag_grab->drag_icon_listener.link);

  if (drag_grab->feedback_actor)
    clutter_actor_remove_all_children (drag_grab->feedback_actor);
}

void
meta_wayland_data_device_start_drag (MetaWaylandDataDevice           *data_device,
                                     struct wl_client                *client,
                                     const MetaWaylandEventInterface *event_iface,
                                     MetaWaylandSurface              *surface,
                                     MetaWaylandDataSource           *source,
                                     MetaWaylandSurface              *icon_surface,
                                     ClutterSprite                   *sprite,
                                     graphene_point_t                 drag_start)
{
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  MetaDisplay *display = display_from_data_device (data_device);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaWaylandDragGrab *drag_grab;
  graphene_point_t pos, surface_pos;
  ClutterModifierType modifiers;
  MetaSurfaceActor *surface_actor;
  MetaWaylandInput *input;

  data_device->current_grab = drag_grab = g_new0 (MetaWaylandDragGrab, 1);

  drag_grab->drag_client = client;
  drag_grab->seat = seat;

  drag_grab->sprite = sprite;

  drag_grab->drag_origin = surface;
  drag_grab->drag_origin_listener.notify = destroy_data_device_origin;
  wl_resource_add_destroy_listener (surface->resource,
                                    &drag_grab->drag_origin_listener);

  surface_actor = meta_wayland_surface_get_actor (surface);

  clutter_actor_transform_stage_point (CLUTTER_ACTOR (surface_actor),
                                       drag_start.x,
                                       drag_start.y,
                                       &surface_pos.x, &surface_pos.y);
  drag_grab->drag_start_x = (int) surface_pos.x;
  drag_grab->drag_start_y = (int) surface_pos.y;

  drag_grab->need_initial_focus = TRUE;

  clutter_seat_query_state (seat->clutter_seat, sprite,
                            &pos, &modifiers);
  drag_grab->buttons = modifiers &
    (CLUTTER_BUTTON1_MASK | CLUTTER_BUTTON2_MASK | CLUTTER_BUTTON3_MASK |
     CLUTTER_BUTTON4_MASK | CLUTTER_BUTTON5_MASK);

  meta_wayland_drag_grab_set_source (drag_grab, source);
  meta_wayland_data_device_set_dnd_source (data_device,
                                           drag_grab->drag_data_source);
  data_source_update_user_dnd_action (source, modifiers);

  if (icon_surface)
    {
      ClutterActor *drag_surface_actor;

      drag_grab->drag_surface = icon_surface;

      drag_grab->drag_icon_listener.notify = destroy_data_device_icon;
      wl_resource_add_destroy_listener (icon_surface->resource,
                                        &drag_grab->drag_icon_listener);

      drag_surface_actor =
        CLUTTER_ACTOR (meta_wayland_surface_get_actor (drag_grab->drag_surface));

      drag_grab->feedback_actor =
        meta_dnd_actor_new (compositor,
                            CLUTTER_ACTOR (surface_actor),
                            drag_grab->drag_start_x,
                            drag_grab->drag_start_y);
      meta_feedback_actor_set_anchor (META_FEEDBACK_ACTOR (drag_grab->feedback_actor),
                                      0, 0);
      clutter_actor_add_child (drag_grab->feedback_actor, drag_surface_actor);

      data_device_update_position (drag_grab, &pos);
    }

  input = meta_wayland_seat_get_input (seat);
  drag_grab->handler =
    meta_wayland_input_attach_event_handler (input,
                                             event_iface,
                                             TRUE,
                                             drag_grab);
  meta_wayland_data_source_set_seat (source, seat);

  meta_dnd_wayland_handle_begin_modal (compositor);
}

void
meta_wayland_data_device_end_drag (MetaWaylandDataDevice *data_device)
{
  if (data_device->current_grab)
    data_device_end_drag_grab (data_device->current_grab);
}

static void
data_device_start_drag (struct wl_client  *client,
                        struct wl_resource *resource,
                        struct wl_resource *source_resource,
                        struct wl_resource *origin_resource,
                        struct wl_resource *icon_resource,
                        uint32_t            serial)
{
  MetaWaylandDataDevice *data_device = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  MetaWaylandSurface *surface = NULL, *icon_surface = NULL;
  MetaWaylandDataSource *drag_source = NULL;
  MetaSelectionSource *selection_source;
  ClutterSprite *sprite;
  float x, y;

  if (origin_resource)
    surface = wl_resource_get_user_data (origin_resource);

  if (!surface)
    return;

  if (!meta_wayland_seat_get_grab_info (seat,
                                        surface,
                                        serial,
                                        TRUE,
                                        &sprite,
                                        &x, &y))
    return;

  /* FIXME: Check that the data source type array isn't empty. */

  if (data_device->current_grab)
    return;

  if (icon_resource)
    icon_surface = wl_resource_get_user_data (icon_resource);
  if (source_resource)
    drag_source = wl_resource_get_user_data (source_resource);

  if (icon_resource &&
      !meta_wayland_surface_assign_role (icon_surface,
                                         META_TYPE_WAYLAND_SURFACE_ROLE_DND,
                                         "sprite", sprite,
                                         NULL))
    {
      wl_resource_post_error (resource, WL_DATA_DEVICE_ERROR_ROLE,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (icon_resource));
      return;
    }

  selection_source = meta_selection_source_wayland_new (drag_source);
  set_selection_source (data_device, META_SELECTION_DND,
                        selection_source);
  g_object_unref (selection_source);

  meta_wayland_data_device_start_drag (data_device, client,
                                       &dnd_event_interface,
                                       surface, drag_source, icon_surface,
                                       sprite,
                                       GRAPHENE_POINT_INIT (x, y));
}

static void
selection_data_source_destroyed (gpointer data, GObject *object_was_here)
{
  MetaWaylandDataDevice *data_device = data;

  data_device->selection_data_source = NULL;
  unset_selection_source (data_device, META_SELECTION_CLIPBOARD);
}

static void
meta_wayland_drag_dest_focus_in (MetaWaylandDataDevice *data_device,
                                 MetaWaylandSurface    *surface,
                                 MetaWaylandDataOffer  *offer)
{
  MetaWaylandDragGrab *grab = data_device->current_grab;
  MetaWaylandDataSource *source;
  struct wl_display *display;
  struct wl_client *client;
  struct wl_resource *resource;
  uint32_t source_actions;
  graphene_point_t pos;

  if (!grab->drag_focus_data_device)
    return;

  client = wl_resource_get_client (surface->resource);
  display = wl_client_get_display (client);

  grab->drag_focus_listener.notify = destroy_drag_focus;
  wl_resource_add_destroy_listener (grab->drag_focus_data_device,
                                    &grab->drag_focus_listener);

  resource = meta_wayland_data_offer_get_resource (offer);

  if (wl_resource_get_version (resource) >=
      WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
    {
      source = meta_wayland_data_offer_get_source (offer);
      meta_wayland_data_source_get_actions (source, &source_actions);
      wl_data_offer_send_source_actions (resource, source_actions);
    }

  clutter_seat_query_state (data_device->seat->clutter_seat,
                            grab->sprite,
                            &pos, NULL);
  meta_wayland_surface_get_relative_coordinates (surface, pos.x, pos.y,
                                                 &pos.x, &pos.y);

  wl_data_device_send_enter (grab->drag_focus_data_device,
                             wl_display_next_serial (display),
                             surface->resource,
                             wl_fixed_from_double (pos.x),
                             wl_fixed_from_double (pos.y),
                             resource);
}

static void
meta_wayland_drag_dest_focus_out (MetaWaylandDataDevice *data_device,
                                  MetaWaylandSurface    *surface)
{
  MetaWaylandDragGrab *grab = data_device->current_grab;

  if (!grab->drag_focus_data_device)
    return;

  wl_data_device_send_leave (grab->drag_focus_data_device);
  wl_list_remove (&grab->drag_focus_listener.link);
  grab->drag_focus_data_device = NULL;
}

static void
meta_wayland_drag_dest_motion (MetaWaylandDataDevice *data_device,
                               MetaWaylandSurface    *surface,
                               float                  x,
                               float                  y,
                               uint32_t               time_ms)
{
  MetaWaylandDragGrab *grab = data_device->current_grab;

  if (!grab->drag_focus_data_device)
    return;

  meta_wayland_surface_get_relative_coordinates (surface, x, y, &x, &y);
  wl_data_device_send_motion (grab->drag_focus_data_device,
                              time_ms,
                              wl_fixed_from_double (x),
                              wl_fixed_from_double (y));
}

static void
meta_wayland_drag_dest_drop (MetaWaylandDataDevice *data_device,
                             MetaWaylandSurface    *surface)
{
  MetaWaylandDragGrab *grab = data_device->current_grab;

  if (!grab->drag_focus_data_device)
    return;

  wl_data_device_send_drop (grab->drag_focus_data_device);
}

static void
meta_wayland_drag_dest_update (MetaWaylandDataDevice *data_device,
                               MetaWaylandSurface    *surface)
{
}

static const MetaWaylandDragDestFuncs meta_wayland_drag_dest_funcs = {
  meta_wayland_drag_dest_focus_in,
  meta_wayland_drag_dest_focus_out,
  meta_wayland_drag_dest_motion,
  meta_wayland_drag_dest_drop,
  meta_wayland_drag_dest_update
};

const MetaWaylandDragDestFuncs *
meta_wayland_data_device_get_drag_dest_funcs (void)
{
  return &meta_wayland_drag_dest_funcs;
}

static void
dnd_data_source_destroyed (gpointer  data,
                           GObject  *object_was_here)
{
  MetaWaylandDataDevice *data_device = data;

  data_device->dnd_data_source = NULL;
  unset_selection_source (data_device, META_SELECTION_DND);
}

void
meta_wayland_data_device_set_dnd_source (MetaWaylandDataDevice *data_device,
                                         MetaWaylandDataSource *source)
{
  if (data_device->dnd_data_source == source)
    return;

  if (data_device->dnd_data_source)
    {
      g_object_weak_unref (G_OBJECT (data_device->dnd_data_source),
                           dnd_data_source_destroyed,
                           data_device);
    }

  data_device->dnd_data_source = source;

  if (source)
    {
      g_object_weak_ref (G_OBJECT (source),
                         dnd_data_source_destroyed,
                         data_device);
    }
}

void
meta_wayland_data_device_set_selection (MetaWaylandDataDevice *data_device,
                                        MetaWaylandDataSource *source,
                                        uint32_t               serial)
{
  MetaWaylandSeat *seat = wl_container_of (data_device, seat, data_device);
  MetaSelectionSource *selection_source;

  if (data_device->selection_data_source &&
      data_device->selection_serial - serial < UINT32_MAX / 2)
    return;

  if (data_device->selection_data_source)
    {
      g_object_weak_unref (G_OBJECT (data_device->selection_data_source),
                           selection_data_source_destroyed,
                           data_device);
      data_device->selection_data_source = NULL;
    }

  data_device->selection_data_source = source;
  data_device->selection_serial = serial;

  if (source)
    {
      meta_wayland_data_source_set_seat (source, seat);
      g_object_weak_ref (G_OBJECT (source),
                         selection_data_source_destroyed,
                         data_device);

      selection_source = meta_selection_source_wayland_new (source);
    }
  else
    {
      selection_source = g_object_new (META_TYPE_SELECTION_SOURCE_MEMORY, NULL);
    }

  set_selection_source (data_device, META_SELECTION_CLIPBOARD,
                        selection_source);
  g_object_unref (selection_source);
}

static void
data_device_set_selection (struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *source_resource,
                           uint32_t            serial)
{
  MetaWaylandDataDevice *data_device = wl_resource_get_user_data (resource);
  MetaWaylandDataSource *source;

  if (source_resource)
    source = wl_resource_get_user_data (source_resource);
  else
    source = NULL;

  if (source)
    {
      if (meta_wayland_data_source_get_actions (source, NULL))
        {
          wl_resource_post_error(source_resource,
                                 WL_DATA_SOURCE_ERROR_INVALID_SOURCE,
                                 "cannot set drag-and-drop source as selection");
          return;
        }
    }

  if (wl_resource_get_client (resource) != data_device->focus_client)
    {
      if (source)
        meta_wayland_data_source_cancel (source);
      return;
    }

  /* FIXME: Store serial and check against incoming serial here. */
  meta_wayland_data_device_set_selection (data_device, source, serial);
}

static const struct wl_data_device_interface data_device_interface = {
  data_device_start_drag,
  data_device_set_selection,
  default_destructor,
};

static void
create_data_source (struct wl_client   *client,
                    struct wl_resource *resource,
                    uint32_t            id)
{
  MetaWaylandCompositor *compositor = wl_resource_get_user_data (resource);
  struct wl_resource *source_resource;

  source_resource = wl_resource_create (client, &wl_data_source_interface,
                                        wl_resource_get_version (resource), id);
  meta_wayland_data_source_new (compositor, source_resource);
}

static void
owner_changed_cb (MetaSelection         *selection,
                  MetaSelectionType      selection_type,
                  MetaSelectionSource   *new_owner,
                  MetaWaylandDataDevice *data_device)
{
  struct wl_resource *data_device_resource;

  if (!data_device->focus_client)
    return;

  if (selection_type == META_SELECTION_CLIPBOARD)
    {
      wl_resource_for_each (data_device_resource,
                            &data_device->focus_resource_list)
        {
          struct wl_resource *offer = NULL;

          if (new_owner)
            {
              offer = create_and_send_clipboard_offer (data_device,
                                                       data_device_resource);
            }

          wl_data_device_send_selection (data_device_resource, offer);
        }
    }
}

static void
ensure_owners_changed_handler_connected (MetaWaylandDataDevice *data_device)
{
  MetaDisplay *display;

  if (data_device->selection_owner_signal_id != 0)
    return;

  display = display_from_data_device (data_device);
  data_device->selection_owner_signal_id =
    g_signal_connect (meta_display_get_selection (display),
                      "owner-changed",
                      G_CALLBACK (owner_changed_cb), data_device);
}

static void
get_data_device (struct wl_client   *client,
                 struct wl_resource *manager_resource,
                 uint32_t            id,
                 struct wl_resource *seat_resource)
{
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  struct wl_resource *cr;
  struct wl_resource *data_device_resource;

  cr = wl_resource_create (client, &wl_data_device_interface,
                           wl_resource_get_version (manager_resource), id);
  wl_resource_set_implementation (cr, &data_device_interface,
                                  &seat->data_device, unbind_resource);

  data_device_resource =
    wl_resource_find_for_client (&seat->data_device.resource_list, client);
  if (data_device_resource)
    {
      wl_list_remove (wl_resource_get_link (data_device_resource));
      wl_list_init (wl_resource_get_link (data_device_resource));
    }

  wl_list_insert (&seat->data_device.resource_list, wl_resource_get_link (cr));

  ensure_owners_changed_handler_connected (&seat->data_device);
}

static const struct wl_data_device_manager_interface manager_interface = {
  create_data_source,
  get_data_device
};

static void
bind_manager (struct wl_client *client,
              void             *data,
              uint32_t          version,
              uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wl_data_device_manager_interface,
                                 version, id);
  wl_resource_set_implementation (resource, &manager_interface,
                                  compositor, NULL);
}

void
meta_wayland_data_device_manager_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
			&wl_data_device_manager_interface,
			META_WL_DATA_DEVICE_MANAGER_VERSION,
			compositor, bind_manager) == NULL)
    g_error ("Could not create data_device");

  meta_wayland_init_toplevel_drag (compositor);
}

void
meta_wayland_data_device_init (MetaWaylandDataDevice *data_device,
                               MetaWaylandSeat       *seat)
{
  data_device->seat = seat;
  wl_list_init (&data_device->resource_list);
  wl_list_init (&data_device->focus_resource_list);
}

MetaWaylandSeat *
meta_wayland_data_device_get_seat (MetaWaylandDataDevice *data_device)
{
  return data_device->seat;
}

static struct wl_resource *
create_and_send_clipboard_offer (MetaWaylandDataDevice *data_device,
                                 struct wl_resource    *target)
{
  MetaWaylandDataOffer *offer;
  MetaWaylandCompositor *compositor =
    meta_wayland_seat_get_compositor (data_device->seat);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  struct wl_resource *resource;
  GList *mimetypes, *l;

  mimetypes = meta_selection_get_mimetypes (meta_display_get_selection (display),
                                            META_SELECTION_CLIPBOARD);
  if (!mimetypes)
    return NULL;

  offer = meta_wayland_data_offer_new (compositor,
                                       META_SELECTION_CLIPBOARD,
                                       NULL, target);
  resource = meta_wayland_data_offer_get_resource (offer);

  wl_data_device_send_data_offer (target, resource);

  for (l = mimetypes; l; l = l->next)
    wl_data_offer_send_offer (resource, l->data);

  g_list_free_full (mimetypes, g_free);

  return resource;
}

void
meta_wayland_data_device_set_focus (MetaWaylandDataDevice *data_device,
                                    MetaWaylandSurface    *surface)
{
  struct wl_client *focus_client = NULL;
  struct wl_resource *data_device_resource;

  if (surface)
    focus_client = wl_resource_get_client (surface->resource);

  if (focus_client == data_device->focus_client)
    return;

  data_device->focus_client = focus_client;
  move_resources (&data_device->resource_list,
                  &data_device->focus_resource_list);

  if (!focus_client)
    return;

  move_resources_for_client (&data_device->focus_resource_list,
                             &data_device->resource_list,
                             focus_client);

  wl_resource_for_each (data_device_resource, &data_device->focus_resource_list)
    {
      struct wl_resource *offer;

      offer = create_and_send_clipboard_offer (data_device,
                                               data_device_resource);
      wl_data_device_send_selection (data_device_resource, offer);
    }
}

MetaWaylandDragGrab *
meta_wayland_data_device_get_current_grab (MetaWaylandDataDevice *data_device)
{
  return data_device->current_grab;
}

void
meta_wayland_data_device_unset_dnd_selection (MetaWaylandDataDevice *data_device)
{
  unset_selection_source (data_device, META_SELECTION_DND);
}

MetaWaylandToplevelDrag *
meta_wayland_data_device_get_toplevel_drag (MetaWaylandDataDevice *data_device)
{
  if (!data_device->current_grab || !data_device->current_grab->drag_data_source)
    return NULL;

  return meta_wayland_data_source_get_toplevel_drag (data_device->current_grab->drag_data_source);
}
