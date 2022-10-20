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
#include "core/edge-resistance.h"
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

static void
update_keyboard_resize (MetaWindowDrag *window_drag,
                        gboolean        update_cursor)
{
  int x, y;

  warp_grab_pointer (window_drag,
                     window_drag->effective_grab_window,
                     window_drag->grab_op,
                     &x, &y);

  if (update_cursor)
    meta_window_drag_update_cursor (window_drag);
}

static void
update_keyboard_move (MetaWindowDrag *window_drag)
{
  int x, y;

  warp_grab_pointer (window_drag,
                     window_drag->effective_grab_window,
                     window_drag->grab_op,
                     &x, &y);
}

static gboolean
is_modifier (xkb_keysym_t keysym)
{
  switch (keysym)
    {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Shift_Lock:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Hyper_L:
    case XKB_KEY_Hyper_R:
      return TRUE;
    default:
      return FALSE;
    }
}

static gboolean
process_mouse_move_resize_grab (MetaWindowDrag  *window_drag,
                                MetaWindow      *window,
                                ClutterKeyEvent *event)
{
  MetaDisplay *display = meta_window_get_display (window);

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      MetaTileMode tile_mode;

      /* Hide the tiling preview if necessary */
      if (display->preview_tile_mode != META_TILE_NONE)
        meta_display_hide_tile_preview (display);

      /* Restore the original tile mode */
      tile_mode = window_drag->tile_mode;
      window->tile_monitor_number = window_drag->tile_monitor_number;

      /* End move or resize and restore to original state.  If the
       * window was a maximized window that had been "shaken loose" we
       * need to remaximize it.  In normal cases, we need to do a
       * moveresize now to get the position back to the original.
       */
      if (window->shaken_loose || tile_mode == META_TILE_MAXIMIZED)
        meta_window_maximize (window, META_MAXIMIZE_BOTH);
      else if (tile_mode != META_TILE_NONE)
        meta_window_restore_tile (window,
                                  tile_mode,
                                  window_drag->initial_window_pos.width,
                                  window_drag->initial_window_pos.height);
      else
        meta_window_move_resize_frame (window_drag->effective_grab_window,
                                       TRUE,
                                       window_drag->initial_window_pos.x,
                                       window_drag->initial_window_pos.y,
                                       window_drag->initial_window_pos.width,
                                       window_drag->initial_window_pos.height);

      /* End grab */
      return FALSE;
    }

  return TRUE;
}

static gboolean
process_keyboard_move_grab (MetaWindowDrag  *window_drag,
                            MetaWindow      *window,
                            ClutterKeyEvent *event)
{
  MetaEdgeResistanceFlags flags;
  gboolean handled;
  MetaRectangle frame_rect;
  int x, y;
  int incr;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (event->keyval))
    return TRUE;

  meta_window_get_frame_rect (window, &frame_rect);
  x = frame_rect.x;
  y = frame_rect.y;

  flags = META_EDGE_RESISTANCE_KEYBOARD_OP | META_EDGE_RESISTANCE_WINDOWS;

  if ((event->modifier_state & CLUTTER_SHIFT_MASK) != 0)
    flags |= META_EDGE_RESISTANCE_SNAP;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (flags & META_EDGE_RESISTANCE_SNAP)
    incr = 1;
  else if (event->modifier_state & CLUTTER_CONTROL_MASK)
    incr = SMALL_INCREMENT;
  else
    incr = NORMAL_INCREMENT;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      /* End move and restore to original state.  If the window was a
       * maximized window that had been "shaken loose" we need to
       * remaximize it.  In normal cases, we need to do a moveresize
       * now to get the position back to the original.
       */
      if (window->shaken_loose)
        meta_window_maximize (window, META_MAXIMIZE_BOTH);
      else
        meta_window_move_resize_frame (window_drag->effective_grab_window,
                                       TRUE,
                                       window_drag->initial_window_pos.x,
                                       window_drag->initial_window_pos.y,
                                       window_drag->initial_window_pos.width,
                                       window_drag->initial_window_pos.height);
    }

  /* When moving by increments, we still snap to edges if the move
   * to the edge is smaller than the increment. This is because
   * Shift + arrow to snap is sort of a hidden feature. This way
   * people using just arrows shouldn't get too frustrated.
   */
  switch (event->keyval)
    {
    case CLUTTER_KEY_KP_Home:
    case CLUTTER_KEY_KP_Prior:
    case CLUTTER_KEY_Up:
    case CLUTTER_KEY_KP_Up:
      y -= incr;
      handled = TRUE;
      break;
    case CLUTTER_KEY_KP_End:
    case CLUTTER_KEY_KP_Next:
    case CLUTTER_KEY_Down:
    case CLUTTER_KEY_KP_Down:
      y += incr;
      handled = TRUE;
      break;
    }

  switch (event->keyval)
    {
    case CLUTTER_KEY_KP_Home:
    case CLUTTER_KEY_KP_End:
    case CLUTTER_KEY_Left:
    case CLUTTER_KEY_KP_Left:
      x -= incr;
      handled = TRUE;
      break;
    case CLUTTER_KEY_KP_Prior:
    case CLUTTER_KEY_KP_Next:
    case CLUTTER_KEY_Right:
    case CLUTTER_KEY_KP_Right:
      x += incr;
      handled = TRUE;
      break;
    }

  if (handled)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Computed new window location %d,%d due to keypress",
                  x, y);

      meta_window_edge_resistance_for_move (window,
                                            &x,
                                            &y,
                                            flags);

      meta_window_move_frame (window, TRUE, x, y);
      update_keyboard_move (window_drag);
    }

  return handled;
}

static gboolean
process_keyboard_resize_grab_op_change (MetaWindowDrag  *window_drag,
                                        MetaWindow      *window,
                                        ClutterKeyEvent *event)
{
  gboolean handled;

  handled = FALSE;
  switch (window_drag->grab_op)
    {
    case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_S:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_N:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Left:
        case CLUTTER_KEY_KP_Left:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_W;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Right:
        case CLUTTER_KEY_KP_Right:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_E;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_W:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_E:
      switch (event->keyval)
        {
        case CLUTTER_KEY_Up:
        case CLUTTER_KEY_KP_Up:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_N;
          handled = TRUE;
          break;
        case CLUTTER_KEY_Down:
        case CLUTTER_KEY_KP_Down:
          window_drag->grab_op = META_GRAB_OP_KEYBOARD_RESIZING_S;
          handled = TRUE;
          break;
        }
      break;

    case META_GRAB_OP_KEYBOARD_RESIZING_SE:
    case META_GRAB_OP_KEYBOARD_RESIZING_NE:
    case META_GRAB_OP_KEYBOARD_RESIZING_SW:
    case META_GRAB_OP_KEYBOARD_RESIZING_NW:
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if (handled)
    {
      update_keyboard_resize (window_drag, TRUE);
      return TRUE;
    }

  return FALSE;
}

static gboolean
process_keyboard_resize_grab (MetaWindowDrag  *window_drag,
                              MetaWindow      *window,
                              ClutterKeyEvent *event)
{
  MetaRectangle frame_rect;
  gboolean handled;
  int height_inc;
  int width_inc;
  int width, height;
  MetaEdgeResistanceFlags flags;
  MetaGravity gravity;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (event->type == CLUTTER_KEY_RELEASE)
    return TRUE;

  /* don't end grab on modifier key presses */
  if (is_modifier (event->keyval))
    return TRUE;

  if (event->keyval == CLUTTER_KEY_Escape)
    {
      /* End resize and restore to original state. */
      meta_window_move_resize_frame (window_drag->effective_grab_window,
                                     TRUE,
                                     window_drag->initial_window_pos.x,
                                     window_drag->initial_window_pos.y,
                                     window_drag->initial_window_pos.width,
                                     window_drag->initial_window_pos.height);

      return FALSE;
    }

  if (process_keyboard_resize_grab_op_change (window_drag, window, event))
    return TRUE;

  width = window->rect.width;
  height = window->rect.height;

  meta_window_get_frame_rect (window, &frame_rect);
  width = frame_rect.width;
  height = frame_rect.height;

  gravity = meta_resize_gravity_from_grab_op (window_drag->grab_op);

  flags = META_EDGE_RESISTANCE_KEYBOARD_OP;

  if ((event->modifier_state & CLUTTER_SHIFT_MASK) != 0)
    flags |= META_EDGE_RESISTANCE_SNAP;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (flags & META_EDGE_RESISTANCE_SNAP)
    {
      height_inc = 1;
      width_inc = 1;
    }
  else if (event->modifier_state & CLUTTER_CONTROL_MASK)
    {
      width_inc = SMALL_INCREMENT;
      height_inc = SMALL_INCREMENT;
    }
  else
    {
      width_inc = NORMAL_INCREMENT;
      height_inc = NORMAL_INCREMENT;
    }

  /* If this is a resize increment window, make the amount we resize
   * the window by match that amount (well, unless snap resizing...)
   */
  if (window->size_hints.width_inc > 1)
    width_inc = window->size_hints.width_inc;
  if (window->size_hints.height_inc > 1)
    height_inc = window->size_hints.height_inc;

  switch (event->keyval)
    {
    case CLUTTER_KEY_Up:
    case CLUTTER_KEY_KP_Up:
      switch (gravity)
        {
        case META_GRAVITY_NORTH:
        case META_GRAVITY_NORTH_WEST:
        case META_GRAVITY_NORTH_EAST:
          /* Move bottom edge up */
          height -= height_inc;
          break;

        case META_GRAVITY_SOUTH:
        case META_GRAVITY_SOUTH_WEST:
        case META_GRAVITY_SOUTH_EAST:
          /* Move top edge up */
          height += height_inc;
          break;

        case META_GRAVITY_EAST:
        case META_GRAVITY_WEST:
        case META_GRAVITY_CENTER:
        case META_GRAVITY_NONE:
        case META_GRAVITY_STATIC:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Down:
    case CLUTTER_KEY_KP_Down:
      switch (gravity)
        {
        case META_GRAVITY_NORTH:
        case META_GRAVITY_NORTH_WEST:
        case META_GRAVITY_NORTH_EAST:
          /* Move bottom edge down */
          height += height_inc;
          break;

        case META_GRAVITY_SOUTH:
        case META_GRAVITY_SOUTH_WEST:
        case META_GRAVITY_SOUTH_EAST:
          /* Move top edge down */
          height -= height_inc;
          break;

        case META_GRAVITY_EAST:
        case META_GRAVITY_WEST:
        case META_GRAVITY_CENTER:
        case META_GRAVITY_NONE:
        case META_GRAVITY_STATIC:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Left:
    case CLUTTER_KEY_KP_Left:
      switch (gravity)
        {
        case META_GRAVITY_EAST:
        case META_GRAVITY_SOUTH_EAST:
        case META_GRAVITY_NORTH_EAST:
          /* Move left edge left */
          width += width_inc;
          break;

        case META_GRAVITY_WEST:
        case META_GRAVITY_SOUTH_WEST:
        case META_GRAVITY_NORTH_WEST:
          /* Move right edge left */
          width -= width_inc;
          break;

        case META_GRAVITY_NORTH:
        case META_GRAVITY_SOUTH:
        case META_GRAVITY_CENTER:
        case META_GRAVITY_NONE:
        case META_GRAVITY_STATIC:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    case CLUTTER_KEY_Right:
    case CLUTTER_KEY_KP_Right:
      switch (gravity)
        {
        case META_GRAVITY_EAST:
        case META_GRAVITY_SOUTH_EAST:
        case META_GRAVITY_NORTH_EAST:
          /* Move left edge right */
          width -= width_inc;
          break;

        case META_GRAVITY_WEST:
        case META_GRAVITY_SOUTH_WEST:
        case META_GRAVITY_NORTH_WEST:
          /* Move right edge right */
          width += width_inc;
          break;

        case META_GRAVITY_NORTH:
        case META_GRAVITY_SOUTH:
        case META_GRAVITY_CENTER:
        case META_GRAVITY_NONE:
        case META_GRAVITY_STATIC:
          g_assert_not_reached ();
          break;
        }

      handled = TRUE;
      break;

    default:
      break;
    }

  /* fixup hack (just paranoia, not sure it's required) */
  if (height < 1)
    height = 1;
  if (width < 1)
    width = 1;

  if (handled)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Computed new window size due to keypress: "
                  "%dx%d, gravity %s",
                  width, height, meta_gravity_to_string (gravity));

      /* Do any edge resistance/snapping */
      meta_window_edge_resistance_for_resize (window,
                                              &width,
                                              &height,
                                              gravity,
                                              flags);

      meta_window_resize_frame_with_gravity (window,
                                             TRUE,
                                             width,
                                             height,
                                             gravity);

      update_keyboard_resize (window_drag, FALSE);
    }

  return handled;
}

static void
process_key_event (MetaWindowDrag  *window_drag,
                   ClutterKeyEvent *event)
{
  MetaWindow *window;
  gboolean keep_grab = TRUE;

  window = window_drag->effective_grab_window;
  if (!window)
    return;

  if (window_drag->grab_op & META_GRAB_OP_WINDOW_FLAG_KEYBOARD)
    {
      if (window_drag->grab_op == META_GRAB_OP_KEYBOARD_MOVING)
        {
          meta_topic (META_DEBUG_KEYBINDINGS,
                      "Processing event for keyboard move");
          keep_grab = process_keyboard_move_grab (window_drag, window, event);
        }
      else
        {
          meta_topic (META_DEBUG_KEYBINDINGS,
                      "Processing event for keyboard resize");
          keep_grab = process_keyboard_resize_grab (window_drag, window, event);
        }
    }
  else if (window_drag->grab_op & META_GRAB_OP_MOVING)
    {
      meta_topic (META_DEBUG_KEYBINDINGS,
                  "Processing event for mouse-only move/resize");
      keep_grab = process_mouse_move_resize_grab (window_drag, window, event);
    }

  if (!keep_grab)
    meta_window_drag_end (window_drag);
}

static gboolean
on_window_drag_event (MetaWindowDrag *window_drag,
                      ClutterEvent   *event)
{
  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      process_key_event (window_drag, &event->key);
      break;
    default:
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
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
  g_signal_connect_swapped (window_drag->handler, "event",
                            G_CALLBACK (on_window_drag_event), window_drag);
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
