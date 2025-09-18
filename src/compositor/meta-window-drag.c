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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "compositor/meta-window-drag.h"

#include "compositor/compositor-private.h"
#include "compositor/edge-resistance.h"
#include "core/meta-window-config-private.h"
#include "core/window-private.h"
#include "meta/meta-enum-types.h"

#ifdef HAVE_X11_CLIENT
#include "x11/meta-x11-frame.h"
#include "x11/window-x11.h"
#endif

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

  MetaWindow *window;
  MetaWindow *effective_grab_window;
  MetaGrabOp grab_op;
  ClutterGrab *grab;

  graphene_point_t pos_hint;

  ClutterSprite *leading_sprite;
  double anchor_rel_x;
  double anchor_rel_y;
  int anchor_root_x;
  int anchor_root_y;
  MetaWindowConfig *initial_window_config;
  int latest_motion_x;
  int latest_motion_y;
  MtkRectangle initial_window_pos;
  int initial_x, initial_y;            /* These are only relevant for */
  gboolean threshold_movement_reached; /* raise_on_click == FALSE.    */
  unsigned int last_edge_resistance_flags;
  unsigned int move_resize_later_id;
  /* if TRUE, window was maximized at start of current grab op */
  gboolean shaken_loose;
  int target_monitor_number;

  MtkRectangle drag_rect;

  gulong unmanaged_id;
  gulong size_changed_id;

  guint tile_preview_timeout_id;
  guint preview_tile_mode : 2;
  guint pos_hint_set : 1;
};

G_DEFINE_FINAL_TYPE (MetaWindowDrag, meta_window_drag, G_TYPE_OBJECT)

static gboolean
update_tile_preview_timeout (MetaWindowDrag *window_drag)
{
  MetaWindow *window = meta_window_drag_get_window (window_drag);
  MetaDisplay *display = window->display;
  gboolean needs_preview = FALSE;

  window_drag->tile_preview_timeout_id = 0;

  if (window)
    {
      switch (window_drag->preview_tile_mode)
        {
        case META_TILE_LEFT:
        case META_TILE_RIGHT:
          if (!meta_window_is_tiled_side_by_side (window))
            needs_preview = TRUE;
          break;

        case META_TILE_MAXIMIZED:
          if (!meta_window_is_maximized (window))
            needs_preview = TRUE;
          break;

        default:
          needs_preview = FALSE;
          break;
        }
    }

  if (needs_preview)
    {
      MtkRectangle tile_rect;
      int monitor;

      monitor = meta_window_get_current_tile_monitor_number (window);
      meta_window_get_tile_area (window, window_drag->preview_tile_mode,
                                 &tile_rect);
      meta_compositor_show_tile_preview (display->compositor,
                                         window, &tile_rect, monitor);
    }
  else
    {
      meta_compositor_hide_tile_preview (display->compositor);
    }

  return FALSE;
}

#define TILE_PREVIEW_TIMEOUT_MS 200

static void
update_tile_preview (MetaWindowDrag *window_drag,
                     gboolean        delay)
{
  if (delay)
    {
      if (window_drag->tile_preview_timeout_id > 0)
        return;

      window_drag->tile_preview_timeout_id =
        g_timeout_add (TILE_PREVIEW_TIMEOUT_MS,
                       (GSourceFunc) update_tile_preview_timeout,
                       window_drag);
      g_source_set_name_by_id (window_drag->tile_preview_timeout_id,
                               "[mutter] meta_display_update_tile_preview_timeout");
    }
  else
    {
      g_clear_handle_id (&window_drag->tile_preview_timeout_id, g_source_remove);

      update_tile_preview_timeout ((gpointer) window_drag);
    }
}

static void
hide_tile_preview (MetaWindowDrag *window_drag)
{
  MetaWindow *window;

  g_clear_handle_id (&window_drag->tile_preview_timeout_id, g_source_remove);

  window_drag->preview_tile_mode = META_TILE_NONE;
  window = meta_window_drag_get_window (window_drag);
  if (window)
    meta_compositor_hide_tile_preview (window->display->compositor);
}

static void
meta_window_drag_dispose (GObject *object)
{
  MetaWindowDrag *window_drag = META_WINDOW_DRAG (object);

  hide_tile_preview (window_drag);
  if (window_drag->grab)
    g_clear_object (&window_drag->grab);
  g_clear_object (&window_drag->initial_window_config);

  G_OBJECT_CLASS (meta_window_drag_parent_class)->dispose (object);
}

static void
meta_window_drag_finalize (GObject *object)
{
  MetaWindowDrag *window_drag = META_WINDOW_DRAG (object);

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

  object_class->dispose = meta_window_drag_dispose;
  object_class->finalize = meta_window_drag_finalize;
  object_class->set_property = meta_window_drag_set_property;
  object_class->get_property = meta_window_drag_get_property;

  props[PROP_WINDOW] = g_param_spec_object ("window", NULL, NULL,
                                            META_TYPE_WINDOW,
                                            G_PARAM_READWRITE |
                                            G_PARAM_CONSTRUCT_ONLY |
                                            G_PARAM_STATIC_STRINGS);
  props[PROP_GRAB_OP] = g_param_spec_uint ("grab-op", NULL, NULL,
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
  window_drag->target_monitor_number = -1;
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
      return META_CURSOR_S_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_SW:
    case META_GRAB_OP_KEYBOARD_RESIZING_SW:
      return META_CURSOR_SW_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_N:
    case META_GRAB_OP_KEYBOARD_RESIZING_N:
      return META_CURSOR_N_RESIZE;
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
      return META_CURSOR_W_RESIZE;
      break;
    case META_GRAB_OP_RESIZING_E:
    case META_GRAB_OP_KEYBOARD_RESIZING_E:
      return META_CURSOR_E_RESIZE;
      break;
    case META_GRAB_OP_MOVING:
      return META_CURSOR_DEFAULT;
      break;
    case META_GRAB_OP_KEYBOARD_MOVING:
    case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      return META_CURSOR_MOVE;
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
  meta_window_drag_update_edges (window_drag);

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

  if (window_drag->grab)
    clutter_grab_dismiss (window_drag->grab);

  g_clear_signal_handler (&window_drag->unmanaged_id, grab_window);
  g_clear_signal_handler (&window_drag->size_changed_id, grab_window);

  meta_display_set_cursor (display, META_CURSOR_DEFAULT);

  clear_move_resize_later (window_drag);

  g_signal_emit_by_name (display, "grab-op-end", grab_window, grab_op);

  g_signal_emit (window_drag, signals[ENDED], 0);
}

static void
on_grab_window_unmanaged (MetaWindow     *window,
                          MetaWindowDrag *window_drag)
{
  meta_window_drag_end (window_drag);
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
  MtkRectangle rect = window_drag->drag_rect;
  MtkRectangle display_rect = { 0 };
  MetaDisplay *display = window->display;
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaBackend *backend = meta_compositor_get_backend (compositor);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat;

  meta_display_get_size (display,
                         &display_rect.width,
                         &display_rect.height);

  /* We may not have done begin_grab_op yet, i.e. may not be in a grab
   */

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

  seat = clutter_backend_get_default_seat (clutter_backend);
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
  /* don't care about releases, but eat them, don't end grab */
  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return TRUE;

  if (clutter_event_get_key_symbol ((ClutterEvent *) event) == CLUTTER_KEY_Escape)
    {
      MetaWindowConfig *initial_window_config =
        window_drag->initial_window_config;
      MetaTileMode initial_tile_mode;
      int tile_monitor_number;

      /* Hide the tiling preview if necessary */
      if (window_drag->preview_tile_mode != META_TILE_NONE)
        hide_tile_preview (window_drag);

      /* Restore the original tile mode */
      initial_tile_mode = meta_window_config_get_tile_mode (initial_window_config);
      tile_monitor_number =
        meta_window_config_get_tile_monitor_number (initial_window_config);
      meta_window_config_set_tile_monitor_number (window->config,
                                                  tile_monitor_number);

      /* End move or resize and restore to original state.  If the
       * window was a maximized window that had been "shaken loose" we
       * need to remaximize it.  In normal cases, we need to do a
       * moveresize now to get the position back to the original.
       */
      if (window_drag->shaken_loose ||
          meta_window_config_is_maximized (initial_window_config) ||
          initial_tile_mode == META_TILE_MAXIMIZED)
        meta_window_maximize (window);
      else if (initial_tile_mode != META_TILE_NONE)
        meta_window_restore_tile (window,
                                  initial_tile_mode,
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
  MtkRectangle frame_rect;
  ClutterModifierType modifiers;
  uint32_t keyval;
  int x, y;
  int incr;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return TRUE;

  keyval = clutter_event_get_key_symbol ((ClutterEvent *) event);
  modifiers = clutter_event_get_state ((ClutterEvent *) event);

  /* don't end grab on modifier key presses */
  if (is_modifier (keyval))
    return TRUE;

  meta_window_get_frame_rect (window, &frame_rect);
  x = frame_rect.x;
  y = frame_rect.y;

  flags = META_EDGE_RESISTANCE_KEYBOARD_OP | META_EDGE_RESISTANCE_WINDOWS;

  if ((modifiers & CLUTTER_SHIFT_MASK) != 0)
    flags |= META_EDGE_RESISTANCE_SNAP;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (flags & META_EDGE_RESISTANCE_SNAP)
    incr = 1;
  else if (modifiers & CLUTTER_CONTROL_MASK)
    incr = SMALL_INCREMENT;
  else
    incr = NORMAL_INCREMENT;

  if (keyval == CLUTTER_KEY_Escape)
    {
      meta_topic (META_DEBUG_KEYBINDINGS, "Cancelling keyboard resize drag");

      /* End move and restore to original state.  If the window was a
       * maximized window that had been "shaken loose" we need to
       * remaximize it.  In normal cases, we need to do a moveresize
       * now to get the position back to the original.
       */
      if (window_drag->shaken_loose)
        {
          meta_window_maximize (window);
        }
      else
        {
          meta_window_move_resize_frame (window_drag->effective_grab_window,
                                         TRUE,
                                         window_drag->initial_window_pos.x,
                                         window_drag->initial_window_pos.y,
                                         window_drag->initial_window_pos.width,
                                         window_drag->initial_window_pos.height);
          window_drag->drag_rect = window_drag->initial_window_pos;
        }
    }

  /* When moving by increments, we still snap to edges if the move
   * to the edge is smaller than the increment. This is because
   * Shift + arrow to snap is sort of a hidden feature. This way
   * people using just arrows shouldn't get too frustrated.
   */
  switch (keyval)
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

  switch (keyval)
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

      window_drag->last_edge_resistance_flags =
        flags & ~META_EDGE_RESISTANCE_KEYBOARD_OP;

      meta_window_drag_edge_resistance_for_move (window_drag,
                                                 &x,
                                                 &y,
                                                 flags);

      meta_window_move_frame (window, TRUE, x, y);
      window_drag->drag_rect.x = x;
      window_drag->drag_rect.y = y;

      update_keyboard_move (window_drag);
    }

  return handled;
}

static gboolean
process_keyboard_resize_grab_op_change (MetaWindowDrag  *window_drag,
                                        MetaWindow      *window,
                                        ClutterKeyEvent *event)
{
  MetaGrabOp op, unconstrained;
  gboolean handled;
  uint32_t keyval;

  op = (window_drag->grab_op & ~META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED);
  unconstrained = (window_drag->grab_op & META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED);

  keyval = clutter_event_get_key_symbol ((ClutterEvent *) event);

  handled = FALSE;
  switch (op)
    {
    case META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN:
      switch (keyval)
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
      switch (keyval)
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
      switch (keyval)
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
      switch (keyval)
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
      switch (keyval)
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

  window_drag->grab_op |= unconstrained;

  if (handled)
    {
      update_keyboard_resize (window_drag, TRUE);
      return TRUE;
    }

  return FALSE;
}

static void
adjust_size_for_tile_match (MetaWindow *window,
                            int        *new_width,
                            int        *new_height)
{
  MtkRectangle work_area, rect;
  MetaWindow *tile_match = meta_window_config_get_tile_match (window->config);
  int tile_monitor_number;

  if (!meta_window_is_tiled_side_by_side (window) || !tile_match)
    return;

  tile_monitor_number =
    meta_window_config_get_tile_monitor_number (window->config);
  meta_window_get_work_area_for_monitor (window, tile_monitor_number,
                                         &work_area);

  /* Make sure the resize does not break minimum sizes */
  rect = work_area;
  rect.width = *new_width;

  meta_window_frame_rect_to_client_rect (window, &rect, &rect);
  *new_width += MAX (0, window->size_hints.min_width - rect.width);

  /* Make sure we're not resizing the tile match below its min width */
  rect = work_area;
  rect.width = work_area.width - *new_width;

  meta_window_frame_rect_to_client_rect (tile_match, &rect, &rect);
  *new_width -= MAX (0, tile_match->size_hints.min_width - rect.width);
}

static void
resize_window_frame (MetaWindowDrag *window_drag,
                     MetaWindow     *window,
                     int             width,
                     int             height)
{
  MtkRectangle rect = MTK_RECTANGLE_INIT (0, 0, width, height);

  adjust_size_for_tile_match (window, &rect.width, &rect.height);
  meta_window_update_tile_fraction (window, rect.width, rect.height);
  meta_window_move_resize_internal (window,
                                    (META_MOVE_RESIZE_USER_ACTION |
                                     META_MOVE_RESIZE_RESIZE_ACTION |
                                     META_MOVE_RESIZE_CONSTRAIN),
                                    META_PLACE_FLAG_NONE,
                                    rect,
                                    &window_drag->drag_rect);
}

static gboolean
process_keyboard_resize_grab (MetaWindowDrag  *window_drag,
                              MetaWindow      *window,
                              ClutterKeyEvent *event)
{
  MtkRectangle frame_rect;
  gboolean handled;
  int height_inc;
  int width_inc;
  int width, height;
  MetaEdgeResistanceFlags flags;
  MetaGravity gravity;
  ClutterModifierType modifiers;
  uint32_t keyval;

  handled = FALSE;

  /* don't care about releases, but eat them, don't end grab */
  if (clutter_event_type ((ClutterEvent *) event) == CLUTTER_KEY_RELEASE)
    return TRUE;

  keyval = clutter_event_get_key_symbol ((ClutterEvent *) event);
  modifiers = clutter_event_get_state ((ClutterEvent *) event);

  /* don't end grab on modifier key presses */
  if (is_modifier (keyval))
    return TRUE;

  if (keyval == CLUTTER_KEY_Escape)
    {
      /* End resize and restore to original state. */
      meta_window_move_resize_frame (window_drag->effective_grab_window,
                                     TRUE,
                                     window_drag->initial_window_pos.x,
                                     window_drag->initial_window_pos.y,
                                     window_drag->initial_window_pos.width,
                                     window_drag->initial_window_pos.height);
      window_drag->drag_rect = window_drag->initial_window_pos;

      return FALSE;
    }

  if (process_keyboard_resize_grab_op_change (window_drag, window, event))
    return TRUE;

  meta_window_get_frame_rect (window, &frame_rect);
  width = frame_rect.width;
  height = frame_rect.height;

  gravity = meta_resize_gravity_from_grab_op (window_drag->grab_op);

  flags = META_EDGE_RESISTANCE_KEYBOARD_OP;

  if ((modifiers & CLUTTER_SHIFT_MASK) != 0)
    flags |= META_EDGE_RESISTANCE_SNAP;

#define SMALL_INCREMENT 1
#define NORMAL_INCREMENT 10

  if (flags & META_EDGE_RESISTANCE_SNAP)
    {
      height_inc = 1;
      width_inc = 1;
    }
  else if (modifiers & CLUTTER_CONTROL_MASK)
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

  switch (keyval)
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

      window_drag->last_edge_resistance_flags =
        flags & ~META_EDGE_RESISTANCE_KEYBOARD_OP;

      /* Do any edge resistance/snapping */
      meta_window_drag_edge_resistance_for_resize (window_drag,
                                                   &width,
                                                   &height,
                                                   gravity,
                                                   flags);

      resize_window_frame (window_drag, window, width, height);

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
      if ((window_drag->grab_op & (META_GRAB_OP_WINDOW_DIR_MASK |
                                   META_GRAB_OP_WINDOW_FLAG_UNKNOWN)) == 0)
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

static void
update_move_maybe_tile (MetaWindowDrag *window_drag,
                        int             shake_threshold,
                        int             x,
                        int             y)
{
  MetaWindow *window = meta_window_drag_get_window (window_drag);
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle work_area;

  /* For side-by-side tiling we are interested in the inside vertical
   * edges of the work area of the monitor where the pointer is located,
   * and in the outside top edge for maximized tiling.
   *
   * For maximized tiling we use the outside edge instead of the
   * inside edge, because we don't want to force users to maximize
   * windows they are placing near the top of their screens.
   *
   * The "current" idea of meta_window_get_work_area_current_monitor() and
   * meta_screen_get_current_monitor() is slightly different: the former
   * refers to the monitor which contains the largest part of the window,
   * the latter to the one where the pointer is located.
   */
  logical_monitor =
    meta_monitor_manager_get_logical_monitor_at (monitor_manager, x, y);
  if (!logical_monitor)
    return;

  meta_window_get_work_area_for_monitor (window,
                                         logical_monitor->number,
                                         &work_area);

  /* Check if the cursor is in a position which triggers tiling
   * and set tile_mode accordingly.
   */
  if (meta_window_can_tile_side_by_side (window, logical_monitor->number) &&
      x >= logical_monitor->rect.x && x < (work_area.x + shake_threshold))
    window_drag->preview_tile_mode = META_TILE_LEFT;
  else if (meta_window_can_tile_side_by_side (window, logical_monitor->number) &&
           x >= work_area.x + work_area.width - shake_threshold &&
           x < (logical_monitor->rect.x + logical_monitor->rect.width))
    window_drag->preview_tile_mode = META_TILE_RIGHT;
  else if (meta_window_can_maximize (window) &&
           y >= logical_monitor->rect.y && y <= work_area.y)
    window_drag->preview_tile_mode = META_TILE_MAXIMIZED;
  else
    window_drag->preview_tile_mode = META_TILE_NONE;

  if (window_drag->preview_tile_mode != META_TILE_NONE)
    {
      meta_window_config_set_tile_monitor_number (window->config,
                                                  logical_monitor->number);
    }
}

static void
calculate_window_position (MetaWindowDrag          *window_drag,
                           MetaEdgeResistanceFlags  flags,
                           int                      window_width,
                           int                      window_height,
                           int                      motion_x,
                           int                      motion_y,
                           int                     *out_x,
                           int                     *out_y)
{
  MetaWindow *window;
  MtkRectangle frame_rect;
  int new_x, new_y;

  window = window_drag->effective_grab_window;
  g_assert (window);

  meta_window_get_frame_rect (window, &frame_rect);
  new_x = (int) (motion_x - (window_width * window_drag->anchor_rel_x));
  new_y = (int) (motion_y - (window_height * window_drag->anchor_rel_y));

  /* Don't allow movement in the maximized directions or while tiled */
  if (meta_window_config_is_maximized_horizontally (window->config) ||
      meta_window_is_tiled_side_by_side (window))
    new_x = frame_rect.x;
  if (meta_window_config_is_maximized_vertically (window->config))
    new_y = frame_rect.y;

  /* Do any edge resistance/snapping */
  meta_window_drag_edge_resistance_for_move (window_drag,
                                             &new_x,
                                             &new_y,
                                             flags);

  *out_x = new_x;
  *out_y = new_y;
}

void
meta_window_drag_calculate_window_position (MetaWindowDrag *window_drag,
                                            int             window_width,
                                            int             window_height,
                                            int            *out_x,
                                            int            *out_y)
{
  calculate_window_position (window_drag,
                             window_drag->last_edge_resistance_flags,
                             window_width,
                             window_height,
                             window_drag->latest_motion_x,
                             window_drag->latest_motion_y,
                             out_x,
                             out_y);
}

static void
update_move (MetaWindowDrag          *window_drag,
             MetaEdgeResistanceFlags  flags,
             int                      x,
             int                      y)
{
  MetaWindow *window;
  int dx, dy;
  int new_x, new_y;
  MtkRectangle frame_rect;
  int shake_threshold;

  window = window_drag->effective_grab_window;
  if (!window)
    return;

  window_drag->latest_motion_x = x;
  window_drag->latest_motion_y = y;

  clear_move_resize_later (window_drag);

  dx = x - window_drag->anchor_root_x;
  dy = y - window_drag->anchor_root_y;

  meta_window_get_frame_rect (window, &frame_rect);
  calculate_window_position (window_drag,
                             flags,
                             frame_rect.width,
                             frame_rect.height,
                             x, y,
                             &new_x, &new_y);

  meta_topic (META_DEBUG_RENDER,
              "x,y = %d,%d anchor ptr %d,%d rel anchor pos %f,%f dx,dy %d,%d",
              x, y,
              window_drag->anchor_root_x,
              window_drag->anchor_root_y,
              window_drag->anchor_rel_x,
              window_drag->anchor_rel_y,
              dx, dy);

  /* Don't bother doing anything if no move has been specified.  (This
   * happens often, even in keyboard moving, due to the warping of the
   * pointer.
   */
  if (dx == 0 && dy == 0)
    return;

  /* Originally for detaching maximized windows, but we use this
   * for the zones at the sides of the monitor where trigger tiling
   * because it's about the right size
   */
#define DRAG_THRESHOLD_TO_SHAKE_THRESHOLD_FACTOR 6
  shake_threshold = meta_prefs_get_drag_threshold () *
    DRAG_THRESHOLD_TO_SHAKE_THRESHOLD_FACTOR;

  if (flags & META_EDGE_RESISTANCE_SNAP)
    {
      /* We don't want to tile while snapping. Also, clear any previous tile
         request. */
      window_drag->preview_tile_mode = META_TILE_NONE;
      meta_window_config_set_tile_monitor_number (window->config, -1);
    }
  else if (meta_prefs_get_edge_tiling () &&
           !meta_window_is_maximized (window) &&
           !meta_window_is_tiled_side_by_side (window))
    {
      update_move_maybe_tile (window_drag, shake_threshold, x, y);
    }

  /* shake loose (unmaximize) maximized or tiled window if dragged beyond
   * the threshold in the Y direction. Tiled windows can also be pulled
   * loose via X motion.
   */

  if ((meta_window_is_maximized (window) && ABS (dy) >= shake_threshold) ||
      (meta_window_is_tiled_side_by_side (window) &&
       (MAX (ABS (dx), ABS (dy)) >= shake_threshold)))
    {
      double prop;

      /* Shake loose, so that the window snaps back to maximized
       * when dragged near the top; do not snap back if tiling
       * is enabled, as top edge tiling can be used in that case
       */
      window_drag->shaken_loose = !meta_prefs_get_edge_tiling ();
      meta_window_config_set_tile_mode (window->config, META_TILE_NONE);

      /* move the unmaximized window to the cursor */
      prop =
        ((double) (x - window_drag->initial_window_pos.x)) /
        ((double) window_drag->initial_window_pos.width);

      window_drag->initial_window_pos.x =
        (int) (x - window->saved_rect.width * prop);

      /* If we started dragging the window from above the top of the window,
       * pretend like we started dragging from the middle of the titlebar
       * instead, as the "correct" anchoring looks wrong. */
      if (window_drag->anchor_root_y < window_drag->initial_window_pos.y)
        window_drag->anchor_root_y = window_drag->initial_window_pos.y + META_WINDOW_TITLEBAR_HEIGHT / 2;

      meta_window_unmaximize (window);
      return;
    }

  /* remaximize window on another monitor if window has been shaken
   * loose or it is still maximized (then move straight)
   */
  else if ((window_drag->shaken_loose || meta_window_is_maximized (window)) &&
           meta_window_config_get_tile_mode (window->config) != META_TILE_LEFT &&
           meta_window_config_get_tile_mode (window->config) != META_TILE_RIGHT)
    {
      MetaDisplay *display = meta_window_get_display (window);
      MetaContext *context = meta_display_get_context (display);
      MetaBackend *backend = meta_context_get_backend (context);
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);
      int n_logical_monitors;
      const MetaLogicalMonitor *wmonitor;
      MtkRectangle work_area;
      int monitor;

      meta_window_config_set_tile_mode (window->config, META_TILE_NONE);
      wmonitor = window->monitor;
      n_logical_monitors =
        meta_monitor_manager_get_num_logical_monitors (monitor_manager);

      for (monitor = 0; monitor < n_logical_monitors; monitor++)
        {
          meta_window_get_work_area_for_monitor (window, monitor, &work_area);

          /* check if cursor is near the top of a monitor work area */
          if (x >= work_area.x &&
              x < (work_area.x + work_area.width) &&
              y >= work_area.y &&
              y < (work_area.y + shake_threshold))
            {
              /* move the saved rect if window will become maximized on an
               * other monitor so user isn't surprised on a later unmaximize
               */
              if (wmonitor->number != monitor &&
                  monitor != window_drag->target_monitor_number)
                {
                  window->saved_rect.x = work_area.x;
                  window->saved_rect.y = work_area.y;

#ifdef HAVE_X11_CLIENT
                  if (META_IS_WINDOW_X11 (window))
                    {
                      MetaFrame *frame;

                      frame = meta_window_x11_get_frame (window);
                      if (frame)
                        {
                          window->saved_rect.x += frame->child_x;
                          window->saved_rect.y += frame->child_y;
                        }
                    }
#endif

                  window_drag->initial_window_pos = work_area;
                  window_drag->anchor_root_x = x;
                  window_drag->anchor_root_y = y;
                  window_drag->shaken_loose = FALSE;

                  if (meta_window_is_maximized (window))
                    {

                      meta_window_move_to_monitor (window, monitor);
                    }
                  else
                    {
                      meta_window_maximize_internal (window,
                                                     META_MAXIMIZE_BOTH,
                                                     &window->saved_rect);
                      meta_window_move_to_monitor (window, monitor);
                    }

                  window_drag->target_monitor_number = monitor;
                }

              return;
            }
        }
    }

  if (meta_window_config_is_floating (window->config))
    {
      /* Delay showing the tile preview slightly to make it more unlikely to
       * trigger it unwittingly, e.g. when shaking loose the window or moving
       * it to another monitor.
       */
      update_tile_preview (window_drag,
                           meta_window_config_get_tile_mode (window->config) !=
                           META_TILE_NONE);

      window_drag->last_edge_resistance_flags =
        flags & ~META_EDGE_RESISTANCE_KEYBOARD_OP;

      meta_window_move_frame (window, TRUE, new_x, new_y);
      window_drag->drag_rect.x = new_x;
      window_drag->drag_rect.y = new_y;
    }
}

static gboolean
update_move_cb (gpointer user_data)
{
  MetaWindowDrag *window_drag = user_data;

  window_drag->move_resize_later_id = 0;

  update_move (window_drag,
               window_drag->last_edge_resistance_flags,
               window_drag->latest_motion_x,
               window_drag->latest_motion_y);

  return G_SOURCE_REMOVE;
}

static void
queue_update_move (MetaWindowDrag          *window_drag,
                   MetaEdgeResistanceFlags  flags,
                   int                      x,
                   int                      y)
{
  MetaCompositor *compositor;
  MetaLaters *laters;
  MetaDisplay *display;

  window_drag->last_edge_resistance_flags = flags;
  window_drag->latest_motion_x = x;
  window_drag->latest_motion_y = y;

  if (window_drag->move_resize_later_id)
    return;
  if (!window_drag->effective_grab_window)
    return;

  display = meta_window_get_display (window_drag->effective_grab_window);
  compositor = meta_display_get_compositor (display);
  laters = meta_compositor_get_laters (compositor);
  window_drag->move_resize_later_id =
    meta_laters_add (laters,
                     META_LATER_BEFORE_REDRAW,
                     update_move_cb,
                     window_drag, NULL);
}

static void
calculate_window_size (MetaWindowDrag          *window_drag,
                       MetaEdgeResistanceFlags  flags,
                       int                     *out_width,
                       int                     *out_height)
{
  MetaWindow *window = window_drag->effective_grab_window;
  int dx, dy;
  MetaGravity gravity;
  MtkRectangle old_rect;
  MtkRectangle new_rect = {};

  meta_window_get_frame_rect (window, &old_rect);

  new_rect.width = window_drag->initial_window_pos.width;
  new_rect.height = window_drag->initial_window_pos.height;

  dx = window_drag->latest_motion_x - window_drag->anchor_root_x;
  dy = window_drag->latest_motion_y - window_drag->anchor_root_y;

  if (window_drag->grab_op & META_GRAB_OP_WINDOW_DIR_EAST)
    new_rect.width += dx;
  else if (window_drag->grab_op & META_GRAB_OP_WINDOW_DIR_WEST)
    new_rect.width -= dx;

  if (window_drag->grab_op & META_GRAB_OP_WINDOW_DIR_SOUTH)
    new_rect.height += dy;
  else if (window_drag->grab_op & META_GRAB_OP_WINDOW_DIR_NORTH)
    new_rect.height -= dy;

  meta_window_maybe_apply_size_hints (window, &new_rect);

  /* One sided resizing ought to actually be one-sided, despite the fact that
   * aspect ratio windows don't interact nicely with the above stuff.  So,
   * to avoid some nasty flicker, we enforce that.
   */

  if (!(window_drag->grab_op & (META_GRAB_OP_WINDOW_DIR_WEST |
                                META_GRAB_OP_WINDOW_DIR_EAST)))
    new_rect.width = old_rect.width;

  if (!(window_drag->grab_op & (META_GRAB_OP_WINDOW_DIR_NORTH |
                                META_GRAB_OP_WINDOW_DIR_SOUTH)))
    new_rect.height = old_rect.height;

  gravity = meta_resize_gravity_from_grab_op (window_drag->grab_op);
  g_assert (gravity >= 0);

  meta_window_drag_edge_resistance_for_resize (window_drag,
                                               &new_rect.width,
                                               &new_rect.height,
                                               gravity,
                                               flags);

  *out_width = new_rect.width;
  *out_height = new_rect.height;
}

static void
update_resize (MetaWindowDrag          *window_drag,
               MetaEdgeResistanceFlags  flags,
               int                      x,
               int                      y)
{
  int dx, dy;
  int new_width, new_height;
  MetaWindow *window;

  window = window_drag->effective_grab_window;
  if (!window)
    return;

  window_drag->latest_motion_x = x;
  window_drag->latest_motion_y = y;

  clear_move_resize_later (window_drag);

  dx = x - window_drag->anchor_root_x;
  dy = y - window_drag->anchor_root_y;

  /* Attached modal dialogs are special in that size
   * changes apply to both sides, so that the dialog
   * remains centered to the parent.
   */
  if (meta_window_is_attached_dialog (window))
    {
      dx *= 2;
      dy *= 2;
    }

  /* Don't bother doing anything if no move has been specified.  (This
   * happens often, even in keyboard resizing, due to the warping of the
   * pointer.
   */
  if (dx == 0 && dy == 0)
    return;

  calculate_window_size (window_drag, flags, &new_width, &new_height);

  if ((window_drag->grab_op & META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN) ==
      META_GRAB_OP_KEYBOARD_RESIZING_UNKNOWN)
    {
      MetaGrabOp op = META_GRAB_OP_WINDOW_BASE |
        META_GRAB_OP_WINDOW_FLAG_KEYBOARD |
        (window_drag->grab_op & META_GRAB_OP_WINDOW_FLAG_UNCONSTRAINED);

      if (dx > 0)
        op |= META_GRAB_OP_WINDOW_DIR_EAST;
      else if (dx < 0)
        op |= META_GRAB_OP_WINDOW_DIR_WEST;

      if (dy > 0)
        op |= META_GRAB_OP_WINDOW_DIR_SOUTH;
      else if (dy < 0)
        op |= META_GRAB_OP_WINDOW_DIR_NORTH;

      window_drag->grab_op = op;

      update_keyboard_resize (window_drag, TRUE);
    }

  /* If we're waiting for a request for _NET_WM_SYNC_REQUEST, we'll
   * resize the window when the window responds, or when we time
   * the response out.
   */
#ifdef HAVE_X11_CLIENT
  if (window->client_type == META_WINDOW_CLIENT_TYPE_X11 &&
      meta_window_x11_is_awaiting_sync_response (window))
    return;
#endif

  window_drag->last_edge_resistance_flags =
    flags & ~META_EDGE_RESISTANCE_KEYBOARD_OP;

  resize_window_frame (window_drag, window, new_width, new_height);
}

static gboolean
update_resize_cb (gpointer user_data)
{
  MetaWindowDrag *window_drag = user_data;

  window_drag->move_resize_later_id = 0;

  update_resize (window_drag,
                 window_drag->last_edge_resistance_flags,
                 window_drag->latest_motion_x,
                 window_drag->latest_motion_y);

  return G_SOURCE_REMOVE;
}

static void
queue_update_resize (MetaWindowDrag          *window_drag,
                     MetaEdgeResistanceFlags  flags,
                     int                      x,
                     int                      y)
{
  MetaCompositor *compositor;
  MetaLaters *laters;
  MetaDisplay *display;

  window_drag->last_edge_resistance_flags = flags;
  window_drag->latest_motion_x = x;
  window_drag->latest_motion_y = y;

  if (window_drag->move_resize_later_id)
    return;
  if (!window_drag->effective_grab_window)
    return;

  display = meta_window_get_display (window_drag->effective_grab_window);
  compositor = meta_display_get_compositor (display);
  laters = meta_compositor_get_laters (compositor);
  window_drag->move_resize_later_id =
    meta_laters_add (laters,
                     META_LATER_BEFORE_REDRAW,
                     update_resize_cb,
                     window_drag, NULL);
}

static void
maybe_maximize_tiled_window (MetaWindow *window)
{
  MtkRectangle work_area;
  gint shake_threshold;
  int tile_monitor_number;
  int width;

  if (!meta_window_is_tiled_side_by_side (window))
    return;

  shake_threshold = meta_prefs_get_drag_threshold ();

  tile_monitor_number =
    meta_window_config_get_tile_monitor_number (window->config);
  meta_window_get_work_area_for_monitor (window,
                                         tile_monitor_number,
                                         &work_area);
  meta_window_config_get_size (window->config, &width, NULL);
  if (width >= work_area.width - shake_threshold)
    meta_window_maximize (window);
}

static void
check_threshold_reached (MetaWindowDrag *window_drag,
                         int             x,
                         int             y)
{
  /* Don't bother doing the check again if we've already reached the threshold */
  if (meta_prefs_get_raise_on_click () ||
      window_drag->threshold_movement_reached)
    return;

  if (ABS (window_drag->initial_x - x) >= 8 ||
      ABS (window_drag->initial_y - y) >= 8)
    window_drag->threshold_movement_reached = TRUE;
}

static void
end_grab_op (MetaWindowDrag     *window_drag,
             const ClutterEvent *event)
{
  ClutterModifierType modifiers;
  MetaEdgeResistanceFlags last_flags;
  MetaWindow *window;
  gfloat x, y;

  window = window_drag->effective_grab_window;
  if (!window)
    return;

  clutter_event_get_coords (event, &x, &y);
  modifiers = clutter_event_get_state (event);
  check_threshold_reached (window_drag, (int) x, (int) y);

  /* If the user was snap moving then ignore the button
   * release because they may have let go of shift before
   * releasing the mouse button and they almost certainly do
   * not want a non-snapped movement to occur from the button
   * release.
   */
  last_flags = window_drag->last_edge_resistance_flags;
  if ((last_flags & META_EDGE_RESISTANCE_SNAP) == 0)
    {
      MetaEdgeResistanceFlags flags = META_EDGE_RESISTANCE_DEFAULT;

      if (modifiers & CLUTTER_SHIFT_MASK)
        flags |= META_EDGE_RESISTANCE_SNAP;

      if (modifiers & CLUTTER_CONTROL_MASK)
        flags |= META_EDGE_RESISTANCE_WINDOWS;

      if (meta_grab_op_is_moving (window_drag->grab_op))
        {
          if (window_drag->preview_tile_mode != META_TILE_NONE)
            {
              meta_window_tile_internal (window,
                                         window_drag->preview_tile_mode,
                                         &window_drag->initial_window_pos);
            }
          else
            {
              update_move (window_drag, flags, (int) x, (int) y);
            }
        }
      else if (meta_grab_op_is_resizing (window_drag->grab_op))
        {
          if (meta_window_config_get_tile_match (window->config))
            flags |= (META_EDGE_RESISTANCE_SNAP | META_EDGE_RESISTANCE_WINDOWS);

          update_resize (window_drag, flags, (int) x, (int) y);
          maybe_maximize_tiled_window (window);
        }
    }
  window_drag->preview_tile_mode = META_TILE_NONE;
  meta_window_drag_end (window_drag);
}

static void
process_pointer_event (MetaWindowDrag     *window_drag,
                       const ClutterEvent *event)
{
  MetaDisplay *display = meta_window_get_display (window_drag->window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSprite *sprite;
  ClutterModifierType modifier_state;
  MetaEdgeResistanceFlags flags;
  MetaWindow *window;
  gfloat x, y;
  int button;

  window = window_drag->effective_grab_window;
  if (!window)
    return;

  sprite = clutter_backend_get_sprite (clutter_backend, stage, event);
  if (window_drag->leading_sprite != sprite)
    return;

  switch (clutter_event_type (event))
    {
    case CLUTTER_BUTTON_PRESS:
      /* This is the keybinding or menu case where we've
       * been dragging around the window without the button
       * pressed, or the case of pressing extra mouse buttons
       * while a grab op is ongoing.
       */
      end_grab_op (window_drag, event);
      break;
    case CLUTTER_TOUCH_END:
      end_grab_op (window_drag, event);
      break;
    case CLUTTER_BUTTON_RELEASE:
      button = clutter_event_get_button (event);

      if (button == 1 ||
          button == (unsigned int) meta_prefs_get_mouse_button_resize ())
        end_grab_op (window_drag, event);

      break;
    case CLUTTER_TOUCH_UPDATE:
      G_GNUC_FALLTHROUGH;
    case CLUTTER_MOTION:
      modifier_state = clutter_event_get_state (event);
      clutter_event_get_coords (event, &x, &y);
      flags = META_EDGE_RESISTANCE_DEFAULT;

      if (modifier_state & CLUTTER_SHIFT_MASK)
        flags |= META_EDGE_RESISTANCE_SNAP;

      if (modifier_state & CLUTTER_CONTROL_MASK)
        flags |= META_EDGE_RESISTANCE_WINDOWS;

      check_threshold_reached (window_drag, (int) x, (int) y);
      if (meta_grab_op_is_moving (window_drag->grab_op))
        {
          queue_update_move (window_drag, flags, (int) x, (int) y);
        }
      else if (meta_grab_op_is_resizing (window_drag->grab_op))
        {
          if (meta_window_config_get_tile_match (window->config))
            flags |= (META_EDGE_RESISTANCE_SNAP | META_EDGE_RESISTANCE_WINDOWS);

          queue_update_resize (window_drag, flags, (int) x, (int) y);
        }
      break;
    case CLUTTER_TOUCH_CANCEL:
      end_grab_op (window_drag, event);
      break;
    default:
      break;
    }
}

gboolean
meta_window_drag_process_event (MetaWindowDrag     *window_drag,
                                const ClutterEvent *event)
{
  switch (clutter_event_type (event))
    {
    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      process_key_event (window_drag, (ClutterKeyEvent *) event);
      break;
    default:
      process_pointer_event (window_drag, event);
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
handle_drag_event (const ClutterEvent *event,
                   gpointer            user_data)
{
  MetaWindowDrag *window_drag = user_data;

  return meta_window_drag_process_event (window_drag, event);
}

gboolean
meta_window_drag_begin (MetaWindowDrag      *window_drag,
                        ClutterSprite       *sprite,
                        uint32_t             timestamp,
                        MetaDragWindowFlags  flags)
{
  MetaWindow *window = window_drag->window, *grab_window = NULL;
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaGrabOp grab_op = window_drag->grab_op;
  ClutterStage *stage;
  int root_x, root_y;

  grab_window = window;

  /* If we're trying to move a window, move the first
   * non-attached dialog instead.
   */
  if (meta_grab_op_is_moving (grab_op))
    grab_window = get_first_freefloating_window (window);

  meta_window_get_frame_rect (grab_window, &window_drag->drag_rect);

  if ((grab_op & META_GRAB_OP_KEYBOARD_MOVING) == META_GRAB_OP_KEYBOARD_MOVING)
    {
      warp_grab_pointer (window_drag, window, grab_op, &root_x, &root_y);
    }
  else if (window_drag->pos_hint_set)
    {
      root_x = (int) window_drag->pos_hint.x;
      root_y = (int) window_drag->pos_hint.y;
    }
  else
    {
      ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
      ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
      graphene_point_t pos;

      clutter_seat_query_state (seat, sprite, &pos, NULL);
      root_x = (int) pos.x;
      root_y = (int) pos.y;
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

  g_assert (grab_window != NULL);
  g_assert (grab_op != META_GRAB_OP_NONE);

  /* Make sure the window is focused, otherwise the keyboard grab
   * won't do a lot of good.
   */
  meta_topic (META_DEBUG_FOCUS,
              "Focusing %s because we're grabbing all its keys",
              window->desc);
  meta_window_focus (window, timestamp);

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  if ((flags & META_DRAG_WINDOW_FLAG_FOREIGN_GRAB) == 0)
    {
      meta_topic (META_DEBUG_WINDOW_OPS, "Creating a new grab.");
      window_drag->grab = clutter_stage_grab_input_only_inactive (stage,
                                                                  handle_drag_event,
                                                                  window_drag,
                                                                  NULL);
      clutter_grab_activate (window_drag->grab);
      if ((clutter_grab_get_seat_state (window_drag->grab) &
           CLUTTER_GRAB_STATE_POINTER) == 0 &&
          !meta_grab_op_is_keyboard (grab_op))
        {
          meta_topic (META_DEBUG_WINDOW_OPS,
                      "Pointer grab failed on a pointer grab op");
          return FALSE;
        }
    }

  g_set_object (&window_drag->effective_grab_window, grab_window);
  window_drag->unmanaged_id =
    g_signal_connect (grab_window, "unmanaged",
                      G_CALLBACK (on_grab_window_unmanaged), window_drag);

  window_drag->leading_sprite = sprite;
  window_drag->initial_window_config =
    meta_window_config_new_from (grab_window->config);
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

  window_drag->initial_window_pos = window_drag->drag_rect;

  window_drag->anchor_rel_x =
    CLAMP ((double) (root_x - window_drag->initial_window_pos.x) /
           window_drag->initial_window_pos.width,
           0, 1);
  window_drag->anchor_rel_y =
    CLAMP ((double) (root_y - window_drag->initial_window_pos.y) /
           window_drag->initial_window_pos.height,
           0, 1);

  g_signal_emit_by_name (display, "grab-op-begin", grab_window, grab_op);

  meta_window_grab_op_began (grab_window, grab_op);

  return TRUE;
}

void
meta_window_drag_update_resize (MetaWindowDrag *window_drag)
{
  update_resize (window_drag,
                 window_drag->last_edge_resistance_flags,
                 window_drag->latest_motion_x,
                 window_drag->latest_motion_y);
}

void
meta_window_drag_calculate_window_size (MetaWindowDrag *window_drag,
                                        int            *out_width,
                                        int            *out_height)
{
  *out_width = window_drag->drag_rect.width;
  *out_height = window_drag->drag_rect.height;
}

MetaWindow *
meta_window_drag_get_window (MetaWindowDrag *window_drag)
{
  return window_drag->effective_grab_window;
}

MetaGrabOp
meta_window_drag_get_grab_op (MetaWindowDrag *window_drag)
{
  return window_drag->grab_op;
}

void
meta_window_drag_update_edges (MetaWindowDrag *window_drag)
{
  meta_window_drag_edge_resistance_cleanup (window_drag);
}

void
meta_window_drag_set_position_hint (MetaWindowDrag   *window_drag,
                                    graphene_point_t *pos_hint)
{
  window_drag->pos_hint_set = pos_hint != NULL;
  if (pos_hint)
    window_drag->pos_hint = *pos_hint;
}

void
meta_window_drag_destroy (MetaWindowDrag *window_drag)
{
  g_object_run_dispose (G_OBJECT (window_drag));
  g_object_unref (window_drag);
}
