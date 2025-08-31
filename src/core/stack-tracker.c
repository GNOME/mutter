/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * stack-tracker:
 *
 * Track stacking order for compositor
 *
 * #MetaStackTracker maintains the most accurate view we have at a
 * given point of time of the ordering of the children of the root
 * window (including override-redirect windows.) This is used to order
 * the windows when the compositor draws them.
 *
 * By contrast, #MetaStack is responsible for keeping track of how we
 * think that windows *should* be ordered.  For windows we manage
 * (non-override-redirect windows), the two stacking orders will be
 * the same.
 */

/*
 * Copyright (C) 2009 Red Hat, Inc.
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

#include "core/stack-tracker.h"

#include <string.h>

#include "compositor/compositor-private.h"
#include "core/display-private.h"
#include "core/stack.h"
#include "core/window-private.h"
#include "meta/util.h"

#ifdef HAVE_XWAYLAND
#include "mtk/mtk-x11.h"
#include "x11/meta-x11-display-private.h"
#include "x11/meta-x11-frame.h"
#include "x11/window-x11.h"
#endif

/* The complexity here comes from resolving two competing factors:
 *
 *  - We need to have a view of the stacking order that takes into
 *    account everything we have done without waiting for events
 *    back from the X server; we don't want to draw intermediate
 *    partially-stacked stack states just because we haven't received
 *    some notification yet.
 *
 *  - Only the X server has an accurate view of the complete stacking;
 *    when we make a request to restack windows, we don't know how
 *    it will affect override-redirect windows, because at any point
 *    applications may restack these windows without our involvement.
 *
 * The technique we use is that we keep three sets of information:
 *
 *  - The stacking order on the server as known from the last
 *    event we received.
 *  - A queue of stacking requests that *we* made subsequent to
 *    that last event.
 *  - A predicted stacking order, derived from applying the queued
 *    requests to the last state from the server.
 *
 * When we receive a new event: a) we compare the serial in the event to
 * the serial of the queued requests and remove any that are now
 * no longer pending b) if necessary, drop the predicted stacking
 * order to recompute it at the next opportunity.
 *
 * Possible optimizations:
 *  Keep the stacks as an array + reverse-mapping hash table to avoid
 *    linear lookups.
 *  Keep the stacks as a GList + reverse-mapping hash table to avoid
 *    linear lookups and to make restacking constant-time.
 */

typedef union _MetaStackOp MetaStackOp;

typedef enum
{
  STACK_OP_ADD,
  STACK_OP_REMOVE,
  STACK_OP_RAISE_ABOVE,
  STACK_OP_LOWER_BELOW
} MetaStackOpType;

typedef enum
{
  APPLY_DEFAULT = 0,
  /* Only do restacking that we can do locally without changing
   * the order of X windows. After we've received any stack
   * events from the X server, we apply the locally cached
   * ops in this mode to handle the non-X parts */
  NO_RESTACK_X_WINDOWS =   1 << 0,
  /* If the stacking operation wouldn't change the order of X
   * windows, ignore it. We use this when applying events received
   * from X so that a spontaneous ConfigureNotify (for a move, say)
   * doesn't change the stacking of X windows with respect to
   * Wayland windows. */
  IGNORE_NOOP_X_RESTACK = 1 << 1
} ApplyFlags;

/* MetaStackOp represents a "stacking operation" - a change to
 * apply to a window stack. Depending on the context, it could
 * either reflect a request we have sent to the server, or a
 * notification event we received from the X server.
 */
union _MetaStackOp
{
  struct {
    MetaStackOpType type;
    gulong serial;
    guint64 window;
  } any;
  struct {
    MetaStackOpType type;
    gulong serial;
    guint64 window;
  } add;
  struct {
    MetaStackOpType type;
    gulong serial;
    guint64 window;
  } remove;
  struct {
    MetaStackOpType type;
    gulong serial;
    guint64 window;
    guint64 sibling;
  } raise_above;
  struct {
    MetaStackOpType type;
    gulong serial;
    guint64 window;
    guint64 sibling;
  } lower_below;
};

struct _MetaStackTracker
{
  MetaDisplay *display;
  MetaStack *stack;

  /* This is the serial of the last request we made that was reflected
   * in xserver_stack
   */
  gulong xserver_serial;

  /* A combined stack containing X and Wayland windows but without
   * any unverified operations applied. */
  GArray *verified_stack;

  /* This is a queue of requests we've made to change the stacking order,
   * where we haven't yet gotten a reply back from the server.
   */
  GQueue *unverified_predictions;

  /* This is how we think the stack is, based on verified_stack, and
   * on the unverified_predictions we've made subsequent to
   * verified_stack.
   */
  GArray *predicted_stack;

  /* Idle function used to sync the compositor's view of the window
   * stack up with our best guess before a frame is drawn.
   */
  guint sync_stack_later;
};

static void
meta_stack_tracker_keep_override_redirect_on_top (MetaStackTracker *tracker);

static inline const char *
get_window_desc (MetaStackTracker *tracker,
                 guint64           window)
{
  return meta_display_describe_stack_id (tracker->display, window);
}

static void
meta_stack_op_dump (MetaStackTracker *tracker,
                    MetaStackOp      *op,
		    const char       *prefix,
		    const char       *suffix)
{
#ifdef WITH_VERBOSE_MODE
  const char *window_desc = get_window_desc (tracker, op->any.window);
#endif

  switch (op->any.type)
    {
    case STACK_OP_ADD:
      meta_topic (META_DEBUG_STACK, "%sADD(%s; %ld)%s",
		  prefix, window_desc, op->any.serial, suffix);
      break;
    case STACK_OP_REMOVE:
      meta_topic (META_DEBUG_STACK, "%sREMOVE(%s; %ld)%s",
		  prefix, window_desc, op->any.serial, suffix);
      break;
    case STACK_OP_RAISE_ABOVE:
      {
        meta_topic (META_DEBUG_STACK, "%sRAISE_ABOVE(%s, %s; %ld)%s",
                    prefix,
                    window_desc,
                    get_window_desc (tracker, op->raise_above.sibling),
                    op->any.serial,
                    suffix);
        break;
      }
    case STACK_OP_LOWER_BELOW:
      {
        meta_topic (META_DEBUG_STACK, "%sLOWER_BELOW(%s, %s; %ld)%s",
                    prefix,
                    window_desc,
                    get_window_desc (tracker, op->lower_below.sibling),
                    op->any.serial,
                    suffix);
        break;
      }
    }
}

#ifdef WITH_VERBOSE_MODE
static void
stack_dump (MetaStackTracker *tracker,
            GArray           *stack)
{
  guint i;

  for (i = 0; i < stack->len; i++)
    {
      guint64 window = g_array_index (stack, guint64, i);
      meta_topic (META_DEBUG_STACK, "    %s", get_window_desc (tracker, window));
    }
}
#endif /* WITH_VERBOSE_MODE */

static void
meta_stack_tracker_dump (MetaStackTracker *tracker)
{
#ifdef WITH_VERBOSE_MODE
  GList *l;

  meta_topic (META_DEBUG_STACK, "MetaStackTracker state");
  meta_topic (META_DEBUG_STACK, "  xserver_serial: %ld", tracker->xserver_serial);
  meta_topic (META_DEBUG_STACK, "  verified_stack: ");
  stack_dump (tracker, tracker->verified_stack);
  meta_topic (META_DEBUG_STACK, "  unverified_predictions: [");
  for (l = tracker->unverified_predictions->head; l; l = l->next)
    {
      MetaStackOp *op = l->data;
      meta_stack_op_dump (tracker, op, "", l->next ? ", " : "");
    }
  meta_topic (META_DEBUG_STACK, "]");
  if (tracker->predicted_stack)
    {
      meta_topic (META_DEBUG_STACK, "  predicted_stack: ");
      stack_dump (tracker, tracker->predicted_stack);
    }
#endif /* WITH_VERBOSE_MODE */
}

static void
meta_stack_op_free (MetaStackOp *op)
{
  g_free (op);
}

static int
find_window (GArray  *window_stack,
             guint64  window)
{
  guint i;

  for (i = 0; i < window_stack->len; i++)
    {
      guint64 current = g_array_index (window_stack, guint64, i);
      if (current == window)
        return i;
    }

  return -1;
}

/* Returns TRUE if stack was changed */
static gboolean
move_window_above (GArray    *stack,
                   guint64    window,
                   int        old_pos,
                   int        above_pos,
                   ApplyFlags apply_flags)
{
  int i;
  gboolean can_restack_this_window =
    (apply_flags & NO_RESTACK_X_WINDOWS) == 0  || !META_STACK_ID_IS_X11 (window);

  if (old_pos < above_pos)
    {
      if ((apply_flags & IGNORE_NOOP_X_RESTACK) != 0)
        {
          gboolean found_x_window = FALSE;
          for (i = old_pos + 1; i <= above_pos; i++)
            if (META_STACK_ID_IS_X11 (g_array_index (stack, guint64, i)))
              found_x_window = TRUE;

          if (!found_x_window)
            return FALSE;
        }

      for (i = old_pos; i < above_pos; i++)
        {
          if (!can_restack_this_window &&
              META_STACK_ID_IS_X11 (g_array_index (stack, guint64, i + 1)))
            break;

          g_array_index (stack, guint64, i) =
            g_array_index (stack, guint64, i + 1);
        }

      g_array_index (stack, guint64, i) = window;

      return i != old_pos;
    }
  else if (old_pos > above_pos + 1)
    {
      if ((apply_flags & IGNORE_NOOP_X_RESTACK) != 0)
        {
          gboolean found_x_window = FALSE;
          for (i = above_pos + 1; i < old_pos; i++)
            if (META_STACK_ID_IS_X11 (g_array_index (stack, guint64, i)))
              found_x_window = TRUE;

          if (!found_x_window)
            return FALSE;
        }

      for (i = old_pos; i > above_pos + 1; i--)
        {
          if (!can_restack_this_window &&
              META_STACK_ID_IS_X11 (g_array_index (stack, guint64, i - 1)))
            break;

          g_array_index (stack, guint64, i) =
            g_array_index (stack, guint64, i - 1);
        }

      g_array_index (stack, guint64, i) = window;

      return i != old_pos;
    }
  else
    return FALSE;
}

/* Returns TRUE if stack was changed */
static gboolean
meta_stack_op_apply (MetaStackTracker *tracker,
                     MetaStackOp      *op,
                     GArray           *stack,
                     ApplyFlags        apply_flags)
{
  switch (op->any.type)
    {
    case STACK_OP_ADD:
      {
        int old_pos;

        if (META_STACK_ID_IS_X11 (op->add.window) &&
            (apply_flags & NO_RESTACK_X_WINDOWS) != 0)
          {
            meta_topic (META_DEBUG_STACK, "STACK_OP_ADD: Ignoring addition "
                        "of %s as per NO_RESTACK_X_WINDOWS",
                        get_window_desc (tracker, op->remove.window));
            return FALSE;
          }

        old_pos = find_window (stack, op->add.window);
        if (old_pos >= 0)
          {
            meta_topic (META_DEBUG_STACK,
                        "STACK_OP_ADD: window %s already in stack",
                        get_window_desc (tracker, op->add.window));
            return FALSE;
          }

        g_array_append_val (stack, op->add.window);
        return TRUE;
      }
    case STACK_OP_REMOVE:
      {
        int old_pos;

        if (META_STACK_ID_IS_X11 (op->remove.window) &&
            (apply_flags & NO_RESTACK_X_WINDOWS) != 0)
          {
            meta_topic (META_DEBUG_STACK, "STACK_OP_REMOVE: Ignoring removal "
                        "of %s as per NO_RESTACK_X_WINDOWS",
                        get_window_desc (tracker, op->remove.window));
            return FALSE;
          }

        old_pos = find_window (stack, op->remove.window);
        if (old_pos < 0)
          {
            meta_topic (META_DEBUG_STACK,
                        "STACK_OP_REMOVE: window %s not in stack",
                        get_window_desc (tracker, op->remove.window));
            return FALSE;
          }

        g_array_remove_index (stack, old_pos);
        return TRUE;
      }
    case STACK_OP_RAISE_ABOVE:
      {
        int old_pos;
        int above_pos;

        old_pos = find_window (stack, op->raise_above.window);
        if (old_pos < 0)
          {
            meta_topic (META_DEBUG_STACK,
                        "STACK_OP_RAISE_ABOVE: window %s not in stack",
                        get_window_desc (tracker, op->raise_above.window));
            return FALSE;
          }

        if (op->raise_above.sibling)
          {
            above_pos = find_window (stack, op->raise_above.sibling);
            if (above_pos < 0)
              {
                meta_topic (META_DEBUG_STACK,
                            "STACK_OP_RAISE_ABOVE: sibling window %s not in stack",
                            get_window_desc (tracker, op->raise_above.sibling));
                return FALSE;
              }
          }
        else
          {
            above_pos = -1;
          }

        return move_window_above (stack, op->raise_above.window, old_pos, above_pos,
                                  apply_flags);
      }
    case STACK_OP_LOWER_BELOW:
      {
        int old_pos;
        int above_pos;

        old_pos = find_window (stack, op->raise_above.window);
        if (old_pos < 0)
          {
            meta_topic (META_DEBUG_STACK,
                        "STACK_OP_LOWER_BELOW: window %s not in stack",
                        get_window_desc (tracker, op->lower_below.window));
            return FALSE;
          }

        if (op->lower_below.sibling)
          {
            int below_pos;

            below_pos = find_window (stack, op->lower_below.sibling);
            if (below_pos < 0)
              {
                meta_topic (META_DEBUG_STACK,
                            "STACK_OP_LOWER_BELOW: sibling window %s not in stack",
                            get_window_desc (tracker, op->lower_below.sibling));
                return FALSE;
              }

            above_pos = below_pos - 1;
          }
        else
          {
            above_pos = stack->len - 1;
          }

        return move_window_above (stack, op->lower_below.window, old_pos, above_pos,
                                  apply_flags);
      }
    }

  g_assert_not_reached ();
  return FALSE;
}

static GArray *
copy_stack (GArray *stack)
{
  GArray *copy = g_array_sized_new (FALSE, FALSE, sizeof (guint64), stack->len);

  g_array_set_size (copy, stack->len);

  memcpy (copy->data, stack->data, sizeof (guint64) * stack->len);

  return copy;
}

#ifdef HAVE_XWAYLAND
static void
query_xserver_stack (MetaDisplay      *display,
                     MetaStackTracker *tracker)
{
  MetaX11Display *x11_display = display->x11_display;
  Window ignored1, ignored2;
  Window *children;
  guint n_children;
  guint i, old_len;

  tracker->xserver_serial = XNextRequest (x11_display->xdisplay);

  XQueryTree (x11_display->xdisplay,
              x11_display->xroot,
              &ignored1, &ignored2, &children, &n_children);

  old_len = tracker->verified_stack->len;

  g_array_set_size (tracker->verified_stack, old_len + n_children);

  for (i = 0; i < n_children; i++)
    g_array_index (tracker->verified_stack, guint64, old_len + i) = children[i];

  XFree (children);
}

static void
drop_x11_windows (MetaDisplay      *display,
                  MetaStackTracker *tracker)
{
  GArray *new_stack;
  GList *l;
  int i;

  tracker->xserver_serial = 0;

  new_stack = g_array_new (FALSE, FALSE, sizeof (guint64));

  for (i = 0; i < tracker->verified_stack->len; i++)
    {
      guint64 window = g_array_index (tracker->verified_stack, guint64, i);

      if (!META_STACK_ID_IS_X11 (window))
        g_array_append_val (new_stack, window);
    }

  g_array_unref (tracker->verified_stack);
  tracker->verified_stack = new_stack;
  l = tracker->unverified_predictions->head;

  while (l)
    {
      MetaStackOp *op = l->data;
      GList *next = l->next;

      if (META_STACK_ID_IS_X11 (op->any.window))
        g_queue_remove (tracker->unverified_predictions, op);

      l = next;
    }
}
#endif /* HAVE_XWAYLAND */

static void
on_stack_changed (MetaStack        *stack,
                  MetaStackTracker *tracker)
{
  GArray *all_root_children_stacked;
  GList *l;
  GArray *hidden_stack_ids;
  GList *sorted;
#ifdef HAVE_XWAYLAND
  MetaFrame *frame;
#endif

  COGL_TRACE_BEGIN_SCOPED (StackChanged, "Meta::StackTracker::on_stack_changed()");

  meta_topic (META_DEBUG_STACK, "Syncing window stack to server");

  all_root_children_stacked = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  hidden_stack_ids = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  meta_topic (META_DEBUG_STACK, "Bottom to top: ");

  sorted = meta_stack_list_windows (stack, NULL);

  for (l = sorted; l; l = l->next)
    {
      MetaWindow *w = l->data;
      uint64_t stack_id = w->stamp;

      if (w->unmanaging)
        continue;

      meta_topic (META_DEBUG_STACK, "  %u:%d - %s ",
                  w->layer, w->stack_position, w->desc);

#ifdef HAVE_XWAYLAND
      if (w->client_type == META_WINDOW_CLIENT_TYPE_X11)
        {
          frame = meta_window_x11_get_frame (w);
          if (frame)
            stack_id = frame->xwindow;
          else
            stack_id = meta_window_x11_get_xwindow (w);
        }
#endif

      /* We don't restack hidden windows along with the rest, though they are
       * reflected in the _NET hints. Hidden windows all get pushed below
       * the screens fullscreen guard_window. */
      if (w->hidden)
        {
          g_array_append_val (hidden_stack_ids, stack_id);
          continue;
        }

      g_array_append_val (all_root_children_stacked, stack_id);
    }

#ifdef HAVE_XWAYLAND
  if (tracker->display->x11_display)
    {
      uint64_t guard_window_id;

      /* The screen guard window sits above all hidden windows and acts as
       * a barrier to input reaching these windows. */
      guard_window_id = tracker->display->x11_display->guard_window;
      g_array_append_val (hidden_stack_ids, guard_window_id);
    }
#endif

  /* Sync to server */

  meta_topic (META_DEBUG_STACK, "Restacking %u windows",
              all_root_children_stacked->len);

  meta_stack_tracker_restack_managed (tracker,
                                      (uint64_t *)all_root_children_stacked->data,
                                      all_root_children_stacked->len);
  meta_stack_tracker_restack_at_bottom (tracker,
                                        (uint64_t *)hidden_stack_ids->data,
                                        hidden_stack_ids->len);

  g_array_free (hidden_stack_ids, TRUE);
  g_array_free (all_root_children_stacked, TRUE);
  g_list_free (sorted);
}

MetaStackTracker *
meta_stack_tracker_new (MetaStack *stack)
{
  MetaStackTracker *tracker;

  tracker = g_new0 (MetaStackTracker, 1);
  tracker->display = stack->display;
  tracker->stack = stack;

  tracker->verified_stack = g_array_new (FALSE, FALSE, sizeof (guint64));
  tracker->unverified_predictions = g_queue_new ();

#ifdef HAVE_XWAYLAND
  g_signal_connect (tracker->display,
                    "x11-display-setup",
                    G_CALLBACK (query_xserver_stack),
                    tracker);
  g_signal_connect (tracker->display,
                    "x11-display-closing",
                    G_CALLBACK (drop_x11_windows),
                    tracker);
#endif
  g_signal_connect (tracker->stack, "changed",
                    G_CALLBACK (on_stack_changed),
                    tracker);

  meta_stack_tracker_dump (tracker);

  return tracker;
}

void
meta_stack_tracker_free (MetaStackTracker *tracker)
{
  if (tracker->sync_stack_later)
    {
      MetaCompositor *compositor =
        meta_display_get_compositor (tracker->display);
      MetaLaters *laters = meta_compositor_get_laters (compositor);

      meta_laters_remove (laters, tracker->sync_stack_later);
    }

  g_array_free (tracker->verified_stack, TRUE);
  if (tracker->predicted_stack)
    g_array_free (tracker->predicted_stack, TRUE);

  g_queue_foreach (tracker->unverified_predictions, (GFunc)meta_stack_op_free, NULL);
  g_queue_free (tracker->unverified_predictions);
  tracker->unverified_predictions = NULL;

#ifdef HAVE_XWAYLAND
  g_signal_handlers_disconnect_by_func (tracker->display,
                                        (gpointer)query_xserver_stack,
                                        tracker);
  g_signal_handlers_disconnect_by_func (tracker->display,
                                        drop_x11_windows,
                                        tracker);
#endif
  g_signal_handlers_disconnect_by_func (tracker->stack,
                                        on_stack_changed,
                                        tracker);

  g_free (tracker);
}

static void
stack_tracker_apply_prediction (MetaStackTracker *tracker,
			        MetaStackOp      *op)
{
  gboolean free_at_end = FALSE;

  /* If this operation doesn't involve restacking X windows then it's
   * implicitly verified. We can apply it immediately unless there
   * are outstanding X restacks that haven't yet been confirmed.
   */
  if (op->any.serial == 0 &&
      tracker->unverified_predictions->length == 0)
    {
      if (meta_stack_op_apply (tracker, op, tracker->verified_stack, APPLY_DEFAULT))
        meta_stack_tracker_queue_sync_stack (tracker);

      free_at_end = TRUE;
    }
  else
    {
      meta_stack_op_dump (tracker, op, "Predicting: ", "");
      g_queue_push_tail (tracker->unverified_predictions, op);
    }

  if (!tracker->predicted_stack ||
      meta_stack_op_apply (tracker, op, tracker->predicted_stack, APPLY_DEFAULT))
    meta_stack_tracker_queue_sync_stack (tracker);

  if (free_at_end)
    meta_stack_op_free (op);

  meta_stack_tracker_dump (tracker);
}

void
meta_stack_tracker_record_add (MetaStackTracker *tracker,
                               guint64           window,
			       gulong            serial)
{
  MetaStackOp *op = g_new0 (MetaStackOp, 1);

  op->any.type = STACK_OP_ADD;
  op->any.serial = serial;
  op->any.window = window;

  stack_tracker_apply_prediction (tracker, op);
}

void
meta_stack_tracker_record_remove (MetaStackTracker *tracker,
                                  guint64           window,
				  gulong            serial)
{
  MetaStackOp *op = g_new0 (MetaStackOp, 1);

  op->any.type = STACK_OP_REMOVE;
  op->any.serial = serial;
  op->any.window = window;

  stack_tracker_apply_prediction (tracker, op);
}

static void
meta_stack_tracker_record_raise_above (MetaStackTracker *tracker,
                                       guint64           window,
                                       guint64           sibling,
				       gulong            serial)
{
  MetaStackOp *op = g_new0 (MetaStackOp, 1);

  op->any.type = STACK_OP_RAISE_ABOVE;
  op->any.serial = serial;
  op->any.window = window;
  op->raise_above.sibling = sibling;

  stack_tracker_apply_prediction (tracker, op);
}

static void
meta_stack_tracker_record_lower_below (MetaStackTracker *tracker,
                                       guint64           window,
                                       guint64           sibling,
				       gulong            serial)
{
  MetaStackOp *op = g_new0 (MetaStackOp, 1);

  op->any.type = STACK_OP_LOWER_BELOW;
  op->any.serial = serial;
  op->any.window = window;
  op->lower_below.sibling = sibling;

  stack_tracker_apply_prediction (tracker, op);
}

#ifdef HAVE_XWAYLAND
static void
stack_tracker_event_received (MetaStackTracker *tracker,
			      MetaStackOp      *op)
{
  gboolean need_sync = FALSE;

  /* If the event is older than our initial query, then it's
   * already included in our tree. Just ignore it. */
  if (op->any.serial < tracker->xserver_serial)
    return;

  meta_stack_op_dump (tracker, op, "Stack op event received: ", "");

  /* First we apply any operations that we have queued up that depended
   * on X operations *older* than what we received .. those operations
   * must have been ignored by the X server, so we just apply the
   * operations we have as best as possible while not moving windows.
   */
  while (tracker->unverified_predictions->head)
    {
      MetaStackOp *queued_op  = tracker->unverified_predictions->head->data;

      if (queued_op->any.serial >= op->any.serial)
	break;

      meta_stack_op_apply (tracker, queued_op, tracker->verified_stack,
                           NO_RESTACK_X_WINDOWS);

      g_queue_pop_head (tracker->unverified_predictions);
      meta_stack_op_free (queued_op);
      need_sync = TRUE;
    }

  /* Then we apply the received event. If it's a spontaneous event
   * based on stacking we didn't trigger, this is the only handling. If we
   * triggered it, we do the X restacking here, and then any residual
   * local-only Wayland stacking below.
   */
  if (meta_stack_op_apply (tracker, op, tracker->verified_stack,
                           IGNORE_NOOP_X_RESTACK))
    need_sync = TRUE;

  /* What is left to process is the prediction corresponding to the event
   * (if any), and then any subsequent Wayland-only events we can just
   * go ahead and do now.
   */
  while (tracker->unverified_predictions->head)
    {
      MetaStackOp *queued_op  = tracker->unverified_predictions->head->data;

      if (queued_op->any.serial > op->any.serial)
	break;

      meta_stack_op_apply (tracker, queued_op, tracker->verified_stack,
                           NO_RESTACK_X_WINDOWS);

      g_queue_pop_head (tracker->unverified_predictions);
      meta_stack_op_free (queued_op);
      need_sync = TRUE;
    }

  if (need_sync)
    {
      if (tracker->predicted_stack)
        {
          g_array_free (tracker->predicted_stack, TRUE);
          tracker->predicted_stack = NULL;
        }

      meta_stack_tracker_queue_sync_stack (tracker);
    }

  meta_stack_tracker_dump (tracker);
}

void
meta_stack_tracker_create_event (MetaStackTracker    *tracker,
				 XCreateWindowEvent  *event)
{
  MetaStackOp op;

  op.any.type = STACK_OP_ADD;
  op.any.serial = event->serial;
  op.add.window = event->window;

  stack_tracker_event_received (tracker, &op);
}

void
meta_stack_tracker_destroy_event (MetaStackTracker    *tracker,
				  XDestroyWindowEvent *event)
{
  MetaStackOp op;

  op.any.type = STACK_OP_REMOVE;
  op.any.serial = event->serial;
  op.remove.window = event->window;

  stack_tracker_event_received (tracker, &op);
}

void
meta_stack_tracker_reparent_event (MetaStackTracker    *tracker,
				   XReparentEvent      *event)
{
  if (event->parent == event->event)
    {
      MetaStackOp op;

      op.any.type = STACK_OP_ADD;
      op.any.serial = event->serial;
      op.add.window = event->window;

      stack_tracker_event_received (tracker, &op);
    }
  else
    {
      MetaStackOp op;

      op.any.type = STACK_OP_REMOVE;
      op.any.serial = event->serial;
      op.remove.window = event->window;

      stack_tracker_event_received (tracker, &op);
    }
}

void
meta_stack_tracker_configure_event (MetaStackTracker    *tracker,
				    XConfigureEvent     *event)
{
  MetaStackOp op;

  op.any.type = STACK_OP_RAISE_ABOVE;
  op.any.serial = event->serial;
  op.raise_above.window = event->window;
  op.raise_above.sibling = event->above;

  stack_tracker_event_received (tracker, &op);
}
#endif /* HAVE_XWAYLAND */

static gboolean
meta_stack_tracker_is_guard_window (MetaStackTracker *tracker,
                                    uint64_t          stack_id)
{
#ifdef HAVE_XWAYLAND
  MetaX11Display *x11_display = tracker->display->x11_display;

  if (!x11_display)
    return FALSE;

  return stack_id == x11_display->guard_window;
#else
  return FALSE;
#endif
}

/**
 * meta_stack_tracker_get_stack:
 * @tracker: a #MetaStackTracker
 * @windows: location to store list of windows, or %NULL
 * @n_windows: location to store count of windows, or %NULL
 *
 * @windows will contain the most current view we have of the stacking order
 * of the children of the root window. The returned array contains
 * everything: InputOnly windows, override-redirect windows,
 * hidden windows, etc. Some of these will correspond to MetaWindow
 * objects, others won't.
 *
 * Assuming that no other clients have made requests that change
 * the stacking order since we last received a notification, the
 * returned list of windows is exactly that you'd get as the
 * children when calling XQueryTree() on the root window.
 */
void
meta_stack_tracker_get_stack (MetaStackTracker *tracker,
                              guint64         **windows,
			      int              *n_windows)
{
  GArray *stack;

  if (tracker->unverified_predictions->length == 0)
    {
      stack = tracker->verified_stack;
    }
  else
    {
      if (tracker->predicted_stack == NULL)
        {
          GList *l;

          tracker->predicted_stack = copy_stack (tracker->verified_stack);
          for (l = tracker->unverified_predictions->head; l; l = l->next)
            {
              MetaStackOp *op = l->data;
              meta_stack_op_apply (tracker, op, tracker->predicted_stack, APPLY_DEFAULT);
            }
        }

      stack = tracker->predicted_stack;
    }

  if (windows)
    *windows = (guint64 *)stack->data;
  if (n_windows)
    *n_windows = stack->len;
}

/**
 * meta_stack_tracker_sync_stack:
 * @tracker: a #MetaStackTracker
 *
 * Informs the compositor of the current stacking order of windows,
 * based on the predicted view maintained by the #MetaStackTracker.
 */
void
meta_stack_tracker_sync_stack (MetaStackTracker *tracker)
{
  guint64 *windows;
  GList *meta_windows;
  int n_windows;
  int i;
#ifdef HAVE_XWAYLAND
  MetaFrame *frame;
#endif

  if (tracker->sync_stack_later)
    {
      MetaLaters *laters;

      laters = meta_compositor_get_laters (tracker->display->compositor);
      meta_laters_remove (laters, tracker->sync_stack_later);
      tracker->sync_stack_later = 0;
    }

  meta_stack_tracker_keep_override_redirect_on_top (tracker);

  meta_stack_tracker_get_stack (tracker, &windows, &n_windows);

  meta_windows = NULL;
  for (i = 0; i < n_windows; i++)
    {
      guint64 window = windows[i];

#ifdef HAVE_XWAYLAND
      if (META_STACK_ID_IS_X11 (window))
        {
          MetaX11Display *x11_display = tracker->display->x11_display;
          MetaWindow *meta_window = NULL;
          Window xwindow = (Window) window;

          if (x11_display)
            meta_window = meta_x11_display_lookup_x_window (x11_display, xwindow);

          /* When mapping back from xwindow to MetaWindow we have to be a bit careful;
           * children of the root could include unmapped windows created by toolkits
           * for internal purposes, including ones that we have registered in our
           * XID => window table (such as user time or frame windows) and we
           * don't want to duplicate them not to break the compositor assumption
           * that the list we pass contains unique IDs (leading to crashes due
           * to wrong removals on destruction).
           * Only include the unframed windows whose XID matches the one we're
           * handling or we'd end up including all the internal windows we've
           * associated to the same meta window, such as the user time windows.
           * (Wine uses a toplevel for _NET_WM_USER_TIME_WINDOW;
           * see window-prop.c:reload_net_wm_user_time_window() for registration.)
           */
          frame = meta_window ? meta_window_x11_get_frame (meta_window) : NULL;
          if (meta_window &&
              (xwindow == meta_window_x11_get_xwindow (meta_window) ||
               (frame && xwindow == frame->xwindow)))
            meta_windows = g_list_prepend (meta_windows, meta_window);
        }
      else
#endif
        meta_windows = g_list_prepend (meta_windows,
                                       meta_display_lookup_stamp (tracker->display, window));
    }

  meta_compositor_sync_stack (tracker->display->compositor,
                              meta_windows);
  g_list_free (meta_windows);

  meta_display_restacked (tracker->display);
}

static gboolean
stack_tracker_sync_stack_later (gpointer data)
{
  meta_stack_tracker_sync_stack (data);

  return FALSE;
}

/**
 * meta_stack_tracker_queue_sync_stack:
 * @tracker: a #MetaStackTracker
 *
 * Queue informing the compositor of the new stacking order before the
 * next redraw. (See meta_stack_tracker_sync_stack()). This is called
 * internally when the stack of X windows changes, but also needs be
 * called directly when we an undecorated window is first shown or
 * withdrawn since the compositor's stacking order (which contains only
 * the windows that have a corresponding MetaWindow) will change without
 * any change to the stacking order of the X windows, if we are creating
 * or destroying MetaWindows.
 */
void
meta_stack_tracker_queue_sync_stack (MetaStackTracker *tracker)
{
  MetaLaters *laters;

  if (tracker->sync_stack_later != 0)
    return;

  laters = meta_compositor_get_laters (tracker->display->compositor);
  tracker->sync_stack_later = meta_laters_add (laters, META_LATER_SYNC_STACK,
                                               stack_tracker_sync_stack_later,
                                               tracker, NULL);
}

/* When moving an X window we sometimes need an X based sibling.
 *
 * If the given sibling is X based this function returns it back
 * otherwise it searches downwards looking for the nearest X window.
 *
 * If no X based sibling could be found return NULL. */
#ifdef HAVE_XWAYLAND
static Window
find_x11_sibling_downwards (MetaStackTracker *tracker,
                            guint64           sibling)
{
  guint64 *windows;
  int n_windows;
  int i;

  if (META_STACK_ID_IS_X11 (sibling))
    return (Window)sibling;

  meta_stack_tracker_get_stack (tracker,
                                &windows, &n_windows);

  /* NB: Children are in order from bottom to top and we
   * want to search downwards for the nearest X window.
   */

  for (i = n_windows - 1; i >= 0; i--)
    if (windows[i] == sibling)
      break;

  for (; i >= 0; i--)
    {
      if (META_STACK_ID_IS_X11 (windows[i]))
        return (Window)windows[i];
    }

  return None;
}

static Window
find_x11_sibling_upwards (MetaStackTracker *tracker,
                          guint64           sibling)
{
  guint64 *windows;
  int n_windows;
  int i;

  if (META_STACK_ID_IS_X11 (sibling))
    return (Window)sibling;

  meta_stack_tracker_get_stack (tracker,
                                &windows, &n_windows);

  for (i = 0; i < n_windows; i++)
    if (windows[i] == sibling)
      break;

  for (; i < n_windows; i++)
    {
      if (META_STACK_ID_IS_X11 (windows[i]))
        return (Window)windows[i];
    }

  return None;
}
#endif

static void
meta_stack_tracker_lower_below (MetaStackTracker *tracker,
                                guint64           window,
                                guint64           sibling)
{
  gulong serial = 0;
#ifdef HAVE_XWAYLAND
  MetaX11Display *x11_display = tracker->display->x11_display;

  if (META_STACK_ID_IS_X11 (window))
    {
      XWindowChanges changes;
      changes.sibling = sibling ? find_x11_sibling_upwards (tracker, sibling) : None;

      if (changes.sibling != find_x11_sibling_upwards (tracker, window))
        {
          serial = XNextRequest (x11_display->xdisplay);

          mtk_x11_error_trap_push (x11_display->xdisplay);

          changes.stack_mode = changes.sibling ? Below : Above;

          XConfigureWindow (x11_display->xdisplay,
                            window,
                            (changes.sibling ? CWSibling : 0) | CWStackMode,
                            &changes);

          mtk_x11_error_trap_pop (x11_display->xdisplay);
        }
    }
#endif

  meta_stack_tracker_record_lower_below (tracker,
                                         window, sibling,
                                         serial);
}

static void
meta_stack_tracker_raise_above (MetaStackTracker *tracker,
                                guint64           window,
                                guint64           sibling)
{
  gulong serial = 0;
#ifdef HAVE_XWAYLAND
  MetaX11Display *x11_display = tracker->display->x11_display;

  if (META_STACK_ID_IS_X11 (window))
    {
      XWindowChanges changes;
      changes.sibling = sibling ? find_x11_sibling_downwards (tracker, sibling) : None;

      if (changes.sibling != find_x11_sibling_downwards (tracker, window))
        {
          serial = XNextRequest (x11_display->xdisplay);

          mtk_x11_error_trap_push (x11_display->xdisplay);

          changes.stack_mode = changes.sibling ? Above : Below;

          XConfigureWindow (x11_display->xdisplay,
                            (Window)window,
                            (changes.sibling ? CWSibling : 0) | CWStackMode,
                            &changes);

          mtk_x11_error_trap_pop (x11_display->xdisplay);
        }
    }
#endif

  meta_stack_tracker_record_raise_above (tracker, window,
                                         sibling, serial);
}

void
meta_stack_tracker_lower (MetaStackTracker *tracker,
                          guint64           window)
{
  meta_stack_tracker_raise_above (tracker, window, 0);
}

static void
meta_stack_tracker_keep_override_redirect_on_top (MetaStackTracker *tracker)
{
  guint64 *stack;
  int n_windows, i;
  int topmost_non_or;

  meta_stack_tracker_get_stack (tracker, &stack, &n_windows);

  for (i = n_windows - 1; i >= 0; i--)
    {
      MetaWindow *window;

      window = meta_display_lookup_stack_id (tracker->display, stack[i]);
      if (window && window->layer != META_LAYER_OVERRIDE_REDIRECT)
        break;
    }

  topmost_non_or = i;

  for (i -= 1; i >= 0; i--)
    {
      MetaWindow *window;

      if (meta_stack_tracker_is_guard_window (tracker, stack[i]))
        break;

      window = meta_display_lookup_stack_id (tracker->display, stack[i]);
      if (window && window->layer == META_LAYER_OVERRIDE_REDIRECT)
        {
          meta_stack_tracker_raise_above (tracker, stack[i], stack[topmost_non_or]);
          meta_stack_tracker_get_stack (tracker, &stack, &n_windows);
          topmost_non_or -= 1;
        }
    }
}

void
meta_stack_tracker_restack_managed (MetaStackTracker *tracker,
                                    const guint64    *managed,
                                    int               n_managed)
{
  guint64 *windows;
  int n_windows;
  int old_pos, new_pos;

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackManaged,
                           "Meta::StackTracker::restack_managed()");
  if (n_managed == 0)
    return;

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackManagedGet,
                           "Meta::StackTracker::restack_managed#get()");
  meta_stack_tracker_get_stack (tracker, &windows, &n_windows);

  /* If the top window has to be restacked, we don't want to move it to the very
   * top of the stack, since apps expect override-redirect windows to stay near
   * the top of the X stack; we instead move it above all managed windows (or
   * above the guard window if there are no non-hidden managed windows.)
   */
  old_pos = n_windows - 1;
  for (old_pos = n_windows - 1; old_pos >= 0; old_pos--)
    {
      MetaWindow *old_window = meta_display_lookup_stack_id (tracker->display, windows[old_pos]);
      if ((old_window && !old_window->override_redirect && !old_window->unmanaging) ||
          meta_stack_tracker_is_guard_window (tracker, windows[old_pos]))
        break;
    }
  g_assert (old_pos >= 0);
  COGL_TRACE_END (StackTrackerRestackManagedGet);

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackManagedRaise,
                           "Meta::StackTracker::restack_managed#raise()");
  new_pos = n_managed - 1;
  if (managed[new_pos] != windows[old_pos])
    {
      /* Move the first managed window in the new stack above all managed windows */
      meta_stack_tracker_raise_above (tracker, managed[new_pos], windows[old_pos]);
      meta_stack_tracker_get_stack (tracker, &windows, &n_windows);
      /* Moving managed[new_pos] above windows[old_pos], moves the window at old_pos down by one */
    }
  COGL_TRACE_END (StackTrackerRestackManagedRaise);

  old_pos--;
  new_pos--;

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackManagedRestack,
                           "Meta::StackTracker::restack_managed#restack()");
  while (old_pos >= 0 && new_pos >= 0)
    {
      if (meta_stack_tracker_is_guard_window (tracker, windows[old_pos]))
        break;

      if (windows[old_pos] == managed[new_pos])
        {
          old_pos--;
          new_pos--;
          continue;
        }

      MetaWindow *old_window = meta_display_lookup_stack_id (tracker->display, windows[old_pos]);
      if (!old_window || old_window->override_redirect || old_window->unmanaging)
        {
          old_pos--;
          continue;
        }

      meta_stack_tracker_lower_below (tracker, managed[new_pos], managed[new_pos + 1]);
      meta_stack_tracker_get_stack (tracker, &windows, &n_windows);
      /* Moving managed[new_pos] above windows[old_pos] moves the window at old_pos down by one,
       * we'll examine it again to see if it matches the next new window */
      old_pos--;
      new_pos--;
    }
  COGL_TRACE_END (StackTrackerRestackManagedRestack);

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackManagedLower,
                           "Meta::StackTracker::restack_managed#lower()");
  while (new_pos > 0)
    {
      meta_stack_tracker_lower_below (tracker, managed[new_pos], managed[new_pos - 1]);
      new_pos--;
    }
  COGL_TRACE_END (StackTrackerRestackManagedLower);
}

void
meta_stack_tracker_restack_at_bottom (MetaStackTracker *tracker,
                                      const guint64    *new_order,
                                      int               n_new_order)
{
  guint64 *windows;
  int n_windows;
  int pos;

  COGL_TRACE_BEGIN_SCOPED (StackTrackerRestackAtBottom,
                           "Meta::StackTracker::restack_at_bottom()");
  meta_stack_tracker_get_stack (tracker, &windows, &n_windows);

  for (pos = 0; pos < n_new_order; pos++)
    {
      if (pos >= n_windows || windows[pos] != new_order[pos])
        {
          if (pos == 0)
            meta_stack_tracker_lower (tracker, new_order[pos]);
          else
            meta_stack_tracker_raise_above (tracker, new_order[pos], new_order[pos - 1]);

          meta_stack_tracker_get_stack (tracker, &windows, &n_windows);
        }
    }
}
