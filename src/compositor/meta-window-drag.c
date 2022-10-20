/*
 * Copyright (C) 2022 Red Hat Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta-window-drag.h"

#include "compositor/compositor-private.h"
#include "core/window-private.h"
#include "meta/meta-enum-types.h"

enum {
  PROP_0,
  PROP_WINDOW,
  PROP_GRAB_OP,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

enum {
  ENDED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _MetaWindowDrag {
  GObject parent_class;
  ClutterActor *handler;

  MetaWindow *window;
  MetaWindow *effective_grab_window;
  MetaGrabOp grab_op;
  ClutterGrab *grab;

  int anchor_root_x;
  int anchor_root_y;
  MetaRectangle anchor_window_pos;
  MetaTileMode tile_mode;
  int tile_monitor_number;
  int latest_motion_x;
  int latest_motion_y;
  MetaRectangle initial_window_pos;
  int initial_x, initial_y;            /* These are only relevant for */
  gboolean threshold_movement_reached; /* raise_on_click == FALSE.    */
  unsigned int last_edge_resistance_flags;
  unsigned int move_resize_later_id;

  gulong unmanaging_id;
  gulong size_changed_id;
};

G_DEFINE_FINAL_TYPE (MetaWindowDrag, meta_window_drag, G_TYPE_OBJECT)

static void
meta_window_drag_finalize (GObject *object)
{
  MetaWindowDrag *window_drag = META_WINDOW_DRAG (object);

  g_clear_pointer (&window_drag->handler, clutter_actor_destroy);
  g_clear_pointer (&window_drag->grab, clutter_grab_unref);
  g_clear_object (&window_drag->effective_grab_window);

  G_OBJECT_CLASS (meta_window_drag_parent_class)->finalize (object);
}

static void
meta_window_drag_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaWindowDrag *window_drag = META_WINDOW_DRAG (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      window_drag->window = g_value_get_object (value);
      break;
    case PROP_GRAB_OP:
      window_drag->grab_op = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_drag_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaWindowDrag *window_drag = META_WINDOW_DRAG (object);

  switch (prop_id)
    {
    case PROP_WINDOW:
      g_value_set_object (value, window_drag->window);
      break;
    case PROP_GRAB_OP:
      g_value_set_uint (value, window_drag->grab_op);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_drag_class_init (MetaWindowDragClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_window_drag_finalize;
  object_class->set_property = meta_window_drag_set_property;
  object_class->get_property = meta_window_drag_get_property;

  props[PROP_WINDOW] = g_param_spec_object ("window",
                                            "Window",
                                            "Window",
                                            META_TYPE_WINDOW,
                                            G_PARAM_READWRITE |
                                            G_PARAM_CONSTRUCT_ONLY |
                                            G_PARAM_STATIC_STRINGS);
  props[PROP_GRAB_OP] = g_param_spec_uint ("grab-op",
                                           "Grab op",
                                           "Grab op",
                                           0, G_MAXUINT,
                                           META_GRAB_OP_NONE,
                                           G_PARAM_READWRITE |
                                           G_PARAM_CONSTRUCT_ONLY |
                                           G_PARAM_STATIC_STRINGS);

  signals[ENDED] =
    g_signal_new ("ended",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_window_drag_init (MetaWindowDrag *window_drag)
{
}

MetaWindowDrag *
meta_window_drag_new (MetaWindow *window,
                      MetaGrabOp  grab_op)
{
  return g_object_new (META_TYPE_WINDOW_DRAG,
                       "window", window,
                       "grab-op", grab_op,
                       NULL);
}

static void
clear_move_resize_later (MetaWindowDrag *window_drag)
{
  if (window_drag->move_resize_later_id)
    {
      MetaDisplay *display;
      MetaCompositor *compositor;
      MetaLaters *laters;

      display = meta_window_get_display (window_drag->effective_grab_window);
      compositor = meta_display_get_compositor (display);
      laters = meta_compositor_get_laters (compositor);
      meta_laters_remove (laters, window_drag->move_resize_later_id);
      window_drag->move_resize_later_id = 0;
    }
}

static MetaCursor
meta_cursor_for_grab_op (MetaGrabOp op)
{
  op &= ~(META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED);

  switch (op)
    {
    case META_GRAB_OP_RESIZING_SE:
    case META_GRAB_OP_KEYBOARD_RESIZING_SE:
      return META_CURSOR_SE_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_S:
    case META_GRAB_OP_KEYBOARD_RESIZING_S:
      return META_CURSOR_SOUTH_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_SW:
    case META_GRAB_OP_KEYBOARD_RESIZING_SW:
      return META_CURSOR_SW_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_N:
    case META_GRAB_OP_KEYBOARD_RESIZING_N:
      return META_CURSOR_NORTH_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_NE:
    case META_GRAB_OP_KEYBOARD_RESIZING_NE:
      return META_CURSOR_NE_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_NW:
    case META_GRAB_OP_KEYBOARD_RESIZING_NW:
      return META_CURSOR_NW_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_W:
    case META_GRAB_OP_KEYBOARD_RESIZING_W:
      return META_CURSOR_WEST_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_E:
    case META_GRAB_OP_KEYBOARD_RESIZING_E:
      return META_CURSOR_EAST_RESIZE;
      break;
    case META_GRAB_OP_MOVING:
    case META_GRAB_OP_KEYBOARD_MOVING:
    case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      return META_CURSOR_MOVE_OR_RESIZE_WINDOW;
      break;
    default:
      break;
    }

  return META_CURSOR_DEFAULT;
}

static void
meta_window_drag_update_cursor (MetaWindowDrag *window_drag)
{
  MetaDisplay *display;
  MetaCursor cursor;

  display = meta_window_get_display (window_drag->effective_grab_window);

  cursor = meta_cursor_for_grab_op (window_drag->grab_op);
  meta_display_set_cursor (display, cursor);
}

void
meta_window_drag_end (MetaWindowDrag *window_drag)
{
  MetaWindow *grab_window = window_drag->effective_grab_window;
  MetaGrabOp grab_op = window_drag->grab_op;
  MetaDisplay *display = meta_window_get_display (grab_window);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Ending grab op %u", grab_op);

  g_assert (grab_window != NULL);

  /* Clear out the edge cache */
  meta_display_cleanup_edges (display);

  /* Only raise the window in orthogonal raise
   * ('do-not-raise-on-click') mode if the user didn't try to move
   * or resize the given window by at least a threshold amount.
   * For raise on click mode, the window was raised at the
   * beginning of the grab_op.
   */
  if (!meta_prefs_get_raise_on_click () &&
      !window_drag->threshold_movement_reached)
    meta_window_raise (grab_window);

  meta_window_grab_op_ended (grab_window, grab_op);

  clutter_grab_dismiss (window_drag->grab);

  g_clear_signal_handler (&window_drag->unmanaging_id, grab_window);
  g_clear_signal_handler (&window_drag->size_changed_id, grab_window);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Restoring passive key grabs on %s", grab_window->desc);
  meta_window_grab_keys (grab_window);

  meta_display_set_cursor (display, META_CURSOR_DEFAULT);

  clear_move_resize_later (window_drag);

  if (meta_is_wayland_compositor ())
    meta_display_sync_wayland_input_focus (display);

  g_signal_emit_by_name (display, "grab-op-end", grab_window, grab_op);

  g_signal_emit (window_drag, signals[ENDED], 0);
}

static void
on_grab_window_unmanaging (MetaWindow     *window,
                           MetaWindowDrag *window_drag)
{
  meta_window_drag_end (window_drag);
}

static void
on_grab_window_size_changed (MetaWindow     *window,
                             MetaWindowDrag *window_drag)
{
  meta_window_get_frame_rect (window,
                              &window_drag->anchor_window_pos);
}

static MetaWindow *
get_first_freefloating_window (MetaWindow *window)
{
  while (meta_window_is_attached_dialog (window))
    window = meta_window_get_transient_for (window);

  /* Attached dialogs should always have a non-NULL transient-for */
  g_assert (window != NULL);

  return window;
}

/* Warp pointer to location appropriate for keyboard grab,
 * return root coordinates where pointer ended up.
 */
static gboolean
warp_grab_pointer (MetaWindowDrag *window_drag,
                   MetaWindow     *window,
                   MetaGrabOp      grab_op,
                   int            *x,
                   int            *y)
{
  MetaRectangle rect;
  MetaRectangle display_rect = { 0 };
  MetaDisplay *display;
  ClutterSeat *seat;

  display = window->display;
  meta_display_get_size (display,
                         &display_rect.width,
                         &display_rect.height);

  /* We may not have done begin_grab_op yet, i.e. may not be in a grab
   */

  meta_window_get_frame_rect (window, &rect);

  if (grab_op & META_GRAB_OP_WINDOW_DIR_WEST)
    *x = 0;
  else if (grab_op & META_GRAB_OP_WINDOW_DIR_EAST)
    *x = rect.width - 1;
  else
    *x = rect.width / 2;

  if (grab_op & META_GRAB_OP_WINDOW_DIR_NORTH)
    *y = 0;
  else if (grab_op & META_GRAB_OP_WINDOW_DIR_SOUTH)
    *y = rect.height - 1;
  else
    *y = rect.height / 2;

  *x += rect.x;
  *y += rect.y;

  /* Avoid weird bouncing at the screen edge; see bug 154706 */
  *x = CLAMP (*x, 0, display_rect.width - 1);
  *y = CLAMP (*y, 0, display_rect.height - 1);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Warping pointer to %d,%d with window at %d,%d",
              *x, *y, rect.x, rect.y);

  /* Need to update the grab positions so that the MotionNotify and other
   * events generated by the XWarpPointer() call below don't cause complete
   * funkiness.  See bug 124582 and bug 122670.
   */
  window_drag->anchor_root_x = *x;
  window_drag->anchor_root_y = *y;
  window_drag->latest_motion_x = *x;
  window_drag->latest_motion_y = *y;
  meta_window_get_frame_rect (window,
                              &window_drag->anchor_window_pos);

  seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  clutter_seat_warp_pointer (seat, *x, *y);

  return TRUE;
}

gboolean
meta_window_drag_begin (MetaWindowDrag *window_drag,
                        uint32_t        timestamp)
{
  MetaWindow *window = window_drag->window, *grab_window = NULL;
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaGrabOp grab_op = window_drag->grab_op;
  ClutterActor *stage;
  int root_x, root_y;

  if ((grab_op & META_GRAB_OP_KEYBOARD_MOVING) == META_GRAB_OP_KEYBOARD_MOVING)
    {
      warp_grab_pointer (window_drag, window, grab_op, &root_x, &root_y);
    }
  else
    {
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
      ClutterInputDevice *device;
      graphene_point_t pos;

      device = clutter_seat_get_pointer (seat);
      clutter_seat_query_state (seat, device, NULL, &pos, NULL);
      root_x = pos.x;
      root_y = pos.y;
    }

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Doing grab op %u on window %s pointer pos %d,%d",
              grab_op, window->desc,
              root_x, root_y);

  if (meta_prefs_get_raise_on_click ())
    meta_window_raise (window);
  else
    {
      window_drag->initial_x = root_x;
      window_drag->initial_y = root_y;
      window_drag->threshold_movement_reached = FALSE;
    }

  grab_window = window;

  /* If we're trying to move a window, move the first
   * non-attached dialog instead.
   */
  if (meta_grab_op_is_moving (grab_op))
    grab_window = get_first_freefloating_window (window);

  g_assert (grab_window != NULL);
  g_assert (grab_op != META_GRAB_OP_NONE);

  /* Make sure the window is focused, otherwise the keyboard grab
   * won't do a lot of good.
   */
  meta_topic (META_DEBUG_FOCUS,
              "Focusing %s because we're grabbing all its keys",
              window->desc);
  meta_window_focus (window, timestamp);

  stage = meta_backend_get_stage (backend);

  window_drag->handler = clutter_actor_new ();
  clutter_actor_set_name (window_drag->handler,
                          "Window drag helper");
  clutter_actor_add_child (stage, window_drag->handler);

  window_drag->grab = clutter_stage_grab (CLUTTER_STAGE (stage),
                                          window_drag->handler);

  if ((clutter_grab_get_seat_state (window_drag->grab) &
       CLUTTER_GRAB_STATE_POINTER) == 0 &&
      !meta_grab_op_is_keyboard (grab_op))
    {
      meta_topic (META_DEBUG_WINDOW_OPS,
                  "Pointer grab failed on a pointer grab op");
      return FALSE;
    }

  /* Temporarily release the passive key grabs on the window */
  meta_window_ungrab_keys (grab_window);

  g_set_object (&window_drag->effective_grab_window, grab_window);
  window_drag->unmanaging_id =
    g_signal_connect (grab_window, "unmanaging",
                      G_CALLBACK (on_grab_window_unmanaging), window_drag);

  if (meta_grab_op_is_moving (grab_op))
    {
      window_drag->size_changed_id =
        g_signal_connect (grab_window, "size-changed",
                          G_CALLBACK (on_grab_window_size_changed), window_drag);
    }

  window_drag->tile_mode = grab_window->tile_mode;
  window_drag->tile_monitor_number = grab_window->tile_monitor_number;
  window_drag->anchor_root_x = root_x;
  window_drag->anchor_root_y = root_y;
  window_drag->latest_motion_x = root_x;
  window_drag->latest_motion_y = root_y;
  window_drag->last_edge_resistance_flags = META_EDGE_RESISTANCE_DEFAULT;

  meta_window_drag_update_cursor (window_drag);

  clear_move_resize_later (window_drag);

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Grab op %u on window %s successful",
              grab_op, window ? window->desc : "(null)");

  meta_window_get_frame_rect (window_drag->effective_grab_window,
                              &window_drag->initial_window_pos);
  window_drag->anchor_window_pos = window_drag->initial_window_pos;

  if (meta_is_wayland_compositor ())
    {
      meta_display_sync_wayland_input_focus (display);
      meta_display_cancel_touch (display);
    }

  g_signal_emit_by_name (display, "grab-op-begin", grab_window, grab_op);

  meta_window_grab_op_began (grab_window, grab_op);

  return TRUE;
}
