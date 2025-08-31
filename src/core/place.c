/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter window placement */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2003 Rob Adams
 * Copyright (C) 2005 Elijah Newren
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

#include "core/place.h"

#include <math.h>
#include <stdlib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "core/boxes-private.h"
#include "core/window-private.h"
#include "meta/meta-backend.h"
#include "meta/prefs.h"
#include "meta/workspace.h"

#ifdef HAVE_XWAYLAND
#include "x11/window-x11-private.h"
#endif

/* arbitrary-ish threshold, honors user attempts to manually cascade. */
#define CASCADE_FUZZ 15
/* space between top-left corners of cascades */
#define CASCADE_INTERVAL 50

typedef enum
{
  META_LEFT,
  META_RIGHT,
  META_TOP,
  META_BOTTOM
} MetaWindowDirection;

typedef struct
{
  MtkRectangle area;
  MtkRectangle window;
  gboolean ltr;
} WindowDistanceComparisonData;

static gint
window_distance_cmp (gconstpointer a,
                     gconstpointer b,
                     gpointer      user_data)
{
  MetaWindow *aw = (gpointer) a;
  MetaWindow *bw = (gpointer) b;
  WindowDistanceComparisonData *data = user_data;
  MtkRectangle *area = &data->area;
  MtkRectangle *window = &data->window;
  MtkRectangle a_frame;
  MtkRectangle b_frame;
  int from_origin_a;
  int from_origin_b;
  int ax, ay, bx, by;
  int corner_x, corner_y;

  meta_window_get_frame_rect (aw, &a_frame);
  meta_window_get_frame_rect (bw, &b_frame);
  ax = a_frame.x - area->x;
  ay = a_frame.y - area->y;
  bx = b_frame.x - area->x;
  by = b_frame.y - area->y;
  corner_x = area->width / 2 + ((data->ltr ? -1 : 1) * window->width / 2);
  corner_y = area->height / 2 - window->height / 2;

  from_origin_a = (corner_x - ax) * (corner_x - ax) +
                  (corner_y - ay) * (corner_y - ay);
  from_origin_b = (corner_x - bx) * (corner_x - bx) +
                  (corner_y - by) * (corner_y - by);

  if (from_origin_a < from_origin_b)
    return -1;
  else if (from_origin_a > from_origin_b)
    return 1;
  else
    return 0;
}

static gint
northwest_cmp (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  MetaWindow *aw = (gpointer) a;
  MetaWindow *bw = (gpointer) b;
  MtkRectangle *area = user_data;
  MtkRectangle a_frame;
  MtkRectangle b_frame;
  int from_origin_a;
  int from_origin_b;
  int ax, ay, bx, by;

  meta_window_get_frame_rect (aw, &a_frame);
  meta_window_get_frame_rect (bw, &b_frame);
  ax = a_frame.x - area->x;
  ay = a_frame.y - area->y;
  bx = b_frame.x - area->x;
  by = b_frame.y - area->y;

  from_origin_a = ax * ax + ay * ay;
  from_origin_b = bx * bx + by * by;

  if (from_origin_a < from_origin_b)
    return -1;
  else if (from_origin_a > from_origin_b)
    return 1;
  else
    return 0;
}

static gint
northeast_cmp (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  MetaWindow *aw = (gpointer) a;
  MetaWindow *bw = (gpointer) b;
  MtkRectangle *area = user_data;
  MtkRectangle a_frame;
  MtkRectangle b_frame;
  int from_origin_a;
  int from_origin_b;
  int ax, ay, bx, by;

  meta_window_get_frame_rect (aw, &a_frame);
  meta_window_get_frame_rect (bw, &b_frame);
  ax = (area->x + area->width) - (a_frame.x + a_frame.width);
  ay = a_frame.y - area->y;
  bx = (area->x + area->width) - (b_frame.x + b_frame.width);
  by = b_frame.y - area->y;

  from_origin_a = ax * ax + ay * ay;
  from_origin_b = bx * bx + by * by;

  if (from_origin_a < from_origin_b)
    return -1;
  else if (from_origin_a > from_origin_b)
    return 1;
  else
    return 0;
}

static void
find_next_cascade (MetaWindow   *window,
                   MtkRectangle  work_area,
                   /* visible windows on relevant workspaces */
                   GList        *windows,
                   int           width,
                   int           height,
                   int          *new_x,
                   int          *new_y,
                   gboolean      place_centered)
{
  GList *tmp;
  GList *sorted;
  int adjusted_center_x, adjusted_center_y;
  int cascade_origin_x, cascade_x, cascade_y;
  int x_threshold, y_threshold;
  MtkRectangle frame_rect;
  int window_width, window_height;
  int cascade_stage;
  gboolean ltr = clutter_get_text_direction () == CLUTTER_TEXT_DIRECTION_LTR;

  /* This is a "fuzzy" cascade algorithm.
   * For each window in the list, we find where we'd cascade a
   * new window after it. If a window is already nearly at that
   * position, we move on.
   */

  x_threshold = CASCADE_FUZZ;
  y_threshold = CASCADE_FUZZ;

  /* Find furthest-SE origin of all workspaces.
   * cascade_x, cascade_y are the target position
   * of NW corner of window frame.
   */

  meta_window_get_frame_rect (window, &frame_rect);
  window_width = width;
  window_height = height;

  sorted = g_list_copy (windows);
  if (place_centered)
    {
      WindowDistanceComparisonData window_distance_data = {
        .area = work_area,
        .window = MTK_RECTANGLE_INIT (frame_rect.x, frame_rect.y,
                                      width, height),
        .ltr = ltr,
      };

      sorted = g_list_sort_with_data (sorted, window_distance_cmp,
                                      &window_distance_data);
    }
  else if (ltr)
    {
      sorted = g_list_sort_with_data (sorted, northwest_cmp, &work_area);
    }
  else
    {
      sorted = g_list_sort_with_data (sorted, northeast_cmp, &work_area);
    }

  adjusted_center_x = work_area.x + work_area.width / 2 - window_width / 2;
  adjusted_center_y = work_area.y + work_area.height / 2 - window_height / 2;

  if (place_centered)
    {
      cascade_origin_x = adjusted_center_x;
    }
  else if (ltr)
    {
      cascade_origin_x = MAX (0, work_area.x);
    }
  else
    {
      cascade_origin_x = work_area.x + work_area.width - window_width;
    }
  cascade_x = cascade_origin_x;
  cascade_y = MAX (0, place_centered ? adjusted_center_y : work_area.y);

  /* Find first cascade position that's not used. */

  cascade_stage = 0;
  tmp = sorted;
  while (tmp != NULL)
    {
      MetaWindow *w;
      MtkRectangle w_frame_rect;
      int wx, ww, wy;
      gboolean nearby;

      w = tmp->data;

      /* we want frame position, not window position */
      meta_window_get_frame_rect (w, &w_frame_rect);
      wx = w_frame_rect.x;
      ww = w_frame_rect.width;
      wy = w_frame_rect.y;

      if (ltr)
        nearby = ABS (wx - cascade_x) < x_threshold &&
                 ABS (wy - cascade_y) < y_threshold;
      else
        nearby = ABS ((wx + ww) - (cascade_x + window_width)) < x_threshold &&
                 ABS (wy - cascade_y) < y_threshold;

      if (nearby)
        {
          /* Cascade the window evenly by the titlebar height; this isn't a typo. */
          cascade_x = ltr
            ? wx + META_WINDOW_TITLEBAR_HEIGHT
            : wx + ww - META_WINDOW_TITLEBAR_HEIGHT - window_width;
          cascade_y = wy + META_WINDOW_TITLEBAR_HEIGHT;

          /* If we go off the screen, start over with a new cascade */
          if (((cascade_x + window_width) >
               (work_area.x + work_area.width)) ||
              (cascade_x < work_area.x) ||
              ((cascade_y + window_height) >
               (work_area.y + work_area.height)))
            {
              cascade_x = cascade_origin_x;
              cascade_y = MAX (0, place_centered ? adjusted_center_y : work_area.y);

              cascade_stage += 1;
              if (ltr)
                cascade_x += CASCADE_INTERVAL * cascade_stage;
              else
                cascade_x -= CASCADE_INTERVAL * cascade_stage;

              /* start over with a new cascade translated to the right
               * (or to the left in RTL environment), unless we are out of space
               */
              if (((cascade_x + window_width) <
                   (work_area.x + work_area.width)) &&
                  (cascade_x >= work_area.x))
                {
                  tmp = sorted;
                  continue;
                }
              else
                {
                  /* All out of space, this cascade_x won't work */
                  cascade_x = cascade_origin_x;
                  break;
                }
            }
        }
      else
        {
          /* Keep searching for a further-down-the-diagonal window. */
        }

      tmp = tmp->next;
    }

  /* cascade_x and cascade_y will match the last window in the list
   * that was "in the way" (in the approximate cascade diagonal)
   */

  g_list_free (sorted);

  *new_x = cascade_x;
  *new_y = cascade_y;
}

static void
find_most_freespace (MetaWindow *window,
                     /* visible windows on relevant workspaces */
                     MetaWindow *focus_window,
                     int         x,
                     int         y,
                     int        *new_x,
                     int        *new_y)
{
  MetaWindowDirection side;
  int max_area;
  int max_width, max_height, left, right, top, bottom;
  int left_space, right_space, top_space, bottom_space;
  MtkRectangle work_area;
  MtkRectangle avoid;
  MtkRectangle frame_rect;

  meta_window_get_work_area_current_monitor (focus_window, &work_area);
  meta_window_get_frame_rect (focus_window, &avoid);
  meta_window_get_frame_rect (window, &frame_rect);

  /* Find the areas of choosing the various sides of the focus window */
  max_width  = MIN (avoid.width, frame_rect.width);
  max_height = MIN (avoid.height, frame_rect.height);
  left_space   = avoid.x - work_area.x;
  right_space  = work_area.width - (avoid.x + avoid.width - work_area.x);
  top_space    = avoid.y - work_area.y;
  bottom_space = work_area.height - (avoid.y + avoid.height - work_area.y);
  left   = MIN (left_space,   frame_rect.width);
  right  = MIN (right_space,  frame_rect.width);
  top    = MIN (top_space,    frame_rect.height);
  bottom = MIN (bottom_space, frame_rect.height);

  /* Find out which side of the focus_window can show the most of the window */
  side = META_LEFT;
  max_area = left * max_height;
  if (right * max_height > max_area)
    {
      side = META_RIGHT;
      max_area = right * max_height;
    }
  if (top * max_width > max_area)
    {
      side = META_TOP;
      max_area = top * max_width;
    }
  if (bottom * max_width > max_area)
    {
      side = META_BOTTOM;
      max_area = bottom * max_width;
    }

  /* Give up if there's no where to put it (i.e. focus window is maximized) */
  if (max_area == 0)
    return;

  /* Place the window on the relevant side; if the whole window fits,
   * make it adjacent to the focus window; if not, make sure the
   * window doesn't go off the edge of the screen.
   */
  switch (side)
    {
    case META_LEFT:
      *new_y = avoid.y;
      if (left_space > frame_rect.width)
        *new_x = avoid.x - frame_rect.width;
      else
        *new_x = work_area.x;
      break;
    case META_RIGHT:
      *new_y = avoid.y;
      if (right_space > frame_rect.width)
        *new_x = avoid.x + avoid.width;
      else
        *new_x = work_area.x + work_area.width - frame_rect.width;
      break;
    case META_TOP:
      *new_x = avoid.x;
      if (top_space > frame_rect.height)
        *new_y = avoid.y - frame_rect.height;
      else
        *new_y = work_area.y;
      break;
    case META_BOTTOM:
      *new_x = avoid.x;
      if (bottom_space > frame_rect.height)
        *new_y = avoid.y + avoid.height;
      else
        *new_y = work_area.y + work_area.height - frame_rect.height;
      break;
    }
}

static gboolean
window_overlaps_focus_window (MetaWindow *window,
                              int         new_x,
                              int         new_y)
{
  MetaWindow *focus_window;
  MtkRectangle window_frame, focus_frame, overlap;

  focus_window = window->display->focus_window;
  if (focus_window == NULL)
    return FALSE;

  meta_window_get_frame_rect (window, &window_frame);
  window_frame.x = new_x;
  window_frame.y = new_y;

  meta_window_get_frame_rect (focus_window, &focus_frame);

  return mtk_rectangle_intersect (&window_frame,
                                  &focus_frame,
                                  &overlap);
}

static gboolean
window_place_centered (MetaWindow *window)
{
  MetaWindowType type;

  type = window->type;

  return (type == META_WINDOW_DIALOG ||
          type == META_WINDOW_MODAL_DIALOG ||
          type == META_WINDOW_SPLASHSCREEN ||
         (type == META_WINDOW_NORMAL && meta_prefs_get_center_new_windows ()));
}

static void
avoid_being_obscured_as_second_modal_dialog (MetaWindow    *window,
                                             MetaPlaceFlag  flags,
                                             int           *x,
                                             int           *y)
{
  /* We can't center this dialog if it was denied focus and it
   * overlaps with the focus window and this dialog is modal and this
   * dialog is in the same app as the focus window (*phew*...please
   * don't make me say that ten times fast). See bug 307875 comment 11
   * and 12 for details, but basically it means this is probably a
   * second modal dialog for some app while the focus window is the
   * first modal dialog.  We should probably make them simultaneously
   * visible in general, but it becomes mandatory to do so due to
   * buggy apps (e.g. those using gtk+ *sigh*) because in those cases
   * this second modal dialog also happens to be modal to the first
   * dialog in addition to the main window, while it has only let us
   * know about the modal-to-the-main-window part.
   */

  MetaWindow *focus_window;

  focus_window = window->display->focus_window;

  /* denied_focus_and_not_transient is only set when focus_window != NULL */

  if (flags & META_PLACE_FLAG_DENIED_FOCUS_AND_NOT_TRANSIENT &&
      window->type == META_WINDOW_MODAL_DIALOG &&
#ifdef HAVE_XWAYLAND
      meta_window_x11_same_application (window, focus_window) &&
#endif
      window_overlaps_focus_window (window, *x, *y))
    {
      find_most_freespace (window, focus_window, *x, *y, x, y);
      meta_topic (META_DEBUG_PLACEMENT,
                  "Dialog window %s was denied focus but may be modal "
                  "to the focus window; had to move it to avoid the "
                  "focus window",
                  window->desc);
    }
}

static gboolean
rectangle_overlaps_some_window (MtkRectangle *rect,
                                GList        *windows)
{
  GList *tmp;
  MtkRectangle dest;

  tmp = windows;
  while (tmp != NULL)
    {
      MetaWindow *other = tmp->data;
      MtkRectangle other_rect;

      switch (other->type)
        {
        case META_WINDOW_DOCK:
        case META_WINDOW_SPLASHSCREEN:
        case META_WINDOW_DESKTOP:
        case META_WINDOW_DIALOG:
        case META_WINDOW_MODAL_DIALOG:
        /* override redirect window types: */
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_POPUP_MENU:
        case META_WINDOW_TOOLTIP:
        case META_WINDOW_NOTIFICATION:
        case META_WINDOW_COMBO:
        case META_WINDOW_DND:
        case META_WINDOW_OVERRIDE_OTHER:
          break;

        case META_WINDOW_NORMAL:
        case META_WINDOW_UTILITY:
        case META_WINDOW_TOOLBAR:
        case META_WINDOW_MENU:
          meta_window_get_frame_rect (other, &other_rect);

          if (mtk_rectangle_intersect (rect, &other_rect, &dest))
            return TRUE;
          break;
        }

      tmp = tmp->next;
    }

  return FALSE;
}

static gint
leftmost_cmp (gconstpointer a,
              gconstpointer b)
{
  MetaWindow *aw = (gpointer) a;
  MetaWindow *bw = (gpointer) b;
  MtkRectangle a_frame;
  MtkRectangle b_frame;
  int ax, bx;

  meta_window_get_frame_rect (aw, &a_frame);
  meta_window_get_frame_rect (bw, &b_frame);
  ax = a_frame.x;
  bx = b_frame.x;

  if (ax < bx)
    return -1;
  else if (ax > bx)
    return 1;
  else
    return 0;
}

static gint
rightmost_cmp (gconstpointer a,
               gconstpointer b)
{
  return -leftmost_cmp (a, b);
}

static gint
topmost_cmp (gconstpointer a,
             gconstpointer b)
{
  MetaWindow *aw = (gpointer) a;
  MetaWindow *bw = (gpointer) b;
  MtkRectangle a_frame;
  MtkRectangle b_frame;
  int ay, by;

  meta_window_get_frame_rect (aw, &a_frame);
  meta_window_get_frame_rect (bw, &b_frame);
  ay = a_frame.y;
  by = b_frame.y;

  if (ay < by)
    return -1;
  else if (ay > by)
    return 1;
  else
    return 0;
}

static void
center_tile_rect_in_area (MtkRectangle *rect,
                          MtkRectangle *work_area)
{
  int fluff;

  /* The point here is to tile a window such that "extra"
   * space is equal on either side (i.e. so a full screen
   * of windows tiled this way would center the windows
   * as a group)
   */

  fluff = (work_area->width % (rect->width + 1)) / 2;
  if (clutter_get_text_direction () == CLUTTER_TEXT_DIRECTION_LTR)
    rect->x = work_area->x + fluff;
  else
    rect->x = work_area->x + work_area->width - rect->width - fluff;
  fluff = (work_area->height % (rect->height + 1)) / 3;
  rect->y = work_area->y + fluff;
}

/* Find the leftmost, then topmost, empty area on the workspace
 * that can contain the new window.
 *
 * Cool feature to have: if we can't fit the current window size,
 * try shrinking the window (within geometry constraints). But
 * beware windows such as Emacs with no sane minimum size, we
 * don't want to create a 1x1 Emacs.
 */
static gboolean
find_first_fit (MetaWindow         *window,
                /* visible windows on relevant workspaces */
                GList              *windows,
                MetaLogicalMonitor *logical_monitor,
                int                 width,
                int                 height,
                int                *new_x,
                int                *new_y)
{
  /* This algorithm is limited - it just brute-force tries
   * to fit the window in a small number of locations that are aligned
   * with existing windows. It tries to place the window on
   * the bottom of each existing window, and then to the right
   * of each existing window, aligned with the left/top of the
   * existing window in each of those cases.
   */
  int retval;
  GList *below_sorted;
  GList *end_sorted;
  GList *tmp;
  MtkRectangle rect = MTK_RECTANGLE_INIT (0, 0, width, height);
  MtkRectangle work_area;
  gboolean ltr = clutter_get_text_direction () == CLUTTER_TEXT_DIRECTION_LTR;

  retval = FALSE;

  /* Below each window */
  below_sorted = g_list_copy (windows);
  below_sorted = g_list_sort (below_sorted, ltr ? leftmost_cmp : rightmost_cmp);
  below_sorted = g_list_sort (below_sorted, topmost_cmp);

  /* To the right of each window */
  end_sorted = g_list_copy (windows);
  end_sorted = g_list_sort (end_sorted, topmost_cmp);
  end_sorted = g_list_sort (end_sorted, ltr ? leftmost_cmp : rightmost_cmp);

#ifdef WITH_VERBOSE_MODE
  {
    char monitor_location_string[RECT_LENGTH];

    meta_rectangle_to_string (&logical_monitor->rect,
                              monitor_location_string);
    meta_topic (META_DEBUG_PLACEMENT,
                "Natural monitor is %s",
                monitor_location_string);
  }
#endif

  meta_window_get_work_area_for_logical_monitor (window,
                                                 logical_monitor,
                                                 &work_area);

  center_tile_rect_in_area (&rect, &work_area);

  if (mtk_rectangle_contains_rect (&work_area, &rect) &&
      !rectangle_overlaps_some_window (&rect, windows))
    {
      *new_x = rect.x;
      *new_y = rect.y;

      retval = TRUE;

      goto out;
    }

  /* try below each window */
  tmp = below_sorted;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;
      MtkRectangle frame_rect;

      meta_window_get_frame_rect (w, &frame_rect);

      rect.x = frame_rect.x;
      rect.y = frame_rect.y + frame_rect.height;

      if (mtk_rectangle_contains_rect (&work_area, &rect) &&
          !rectangle_overlaps_some_window (&rect, below_sorted))
        {
          *new_x = rect.x;
          *new_y = rect.y;

          retval = TRUE;

          goto out;
        }

      tmp = tmp->next;
    }

  /* try to the right (or left in RTL environment) of each window */
  tmp = end_sorted;
  while (tmp != NULL)
    {
      MetaWindow *w = tmp->data;
      MtkRectangle frame_rect;

      meta_window_get_frame_rect (w, &frame_rect);

      if (ltr)
        rect.x = frame_rect.x + frame_rect.width;
      else
        rect.x = frame_rect.x - rect.width;
      rect.y = frame_rect.y;

      if (mtk_rectangle_contains_rect (&work_area, &rect) &&
          !rectangle_overlaps_some_window (&rect, end_sorted))
        {
          *new_x = rect.x;
          *new_y = rect.y;

          retval = TRUE;

          goto out;
        }

      tmp = tmp->next;
    }

out:
  g_list_free (below_sorted);
  g_list_free (end_sorted);
  return retval;
}

void
meta_window_process_placement (MetaWindow        *window,
                               MetaPlacementRule *placement_rule,
                               int               *rel_x,
                               int               *rel_y)
{
  MtkRectangle anchor_rect;
  int window_width, window_height;
  int x, y;

  window_width = placement_rule->width;
  window_height = placement_rule->height;

  anchor_rect = placement_rule->anchor_rect;

  /* Place at anchor point. */
  if (placement_rule->anchor & META_PLACEMENT_ANCHOR_LEFT)
    x = anchor_rect.x;
  else if (placement_rule->anchor & META_PLACEMENT_ANCHOR_RIGHT)
    x = anchor_rect.x + anchor_rect.width;
  else
    x = anchor_rect.x + (anchor_rect.width / 2);
  if (placement_rule->anchor & META_PLACEMENT_ANCHOR_TOP)
    y = anchor_rect.y;
  else if (placement_rule->anchor & META_PLACEMENT_ANCHOR_BOTTOM)
    y = anchor_rect.y + anchor_rect.height;
  else
    y = anchor_rect.y + (anchor_rect.height / 2);

  /* Shift according to gravity. */
  if (placement_rule->gravity & META_PLACEMENT_GRAVITY_LEFT)
    x -= window_width;
  else if (placement_rule->gravity & META_PLACEMENT_GRAVITY_RIGHT)
    x = x;
  else
    x -= window_width / 2;
  if (placement_rule->gravity & META_PLACEMENT_GRAVITY_TOP)
    y -= window_height;
  else if (placement_rule->gravity & META_PLACEMENT_GRAVITY_BOTTOM)
    y = y;
  else
    y -= window_height / 2;

  /* Offset according to offset. */
  x += placement_rule->offset_x;
  y += placement_rule->offset_y;

  *rel_x = x;
  *rel_y = y;
}

static GList *
find_windows_relevant_for_placement (MetaWindow *window)
{
  GList *windows = NULL;
  g_autoptr (GSList) all_windows = NULL;
  GSList *l;

  all_windows = meta_display_list_windows (window->display, META_LIST_DEFAULT);

  for (l = all_windows; l; l = l->next)
    {
      MetaWindow *other_window = l->data;

      if (other_window == window)
        continue;

      if (!meta_window_showing_on_its_workspace (other_window))
        continue;

      if (!window->on_all_workspaces &&
          !meta_window_located_on_workspace (other_window, window->workspace))
        continue;

      windows = g_list_prepend (windows, other_window);
    }

  return windows;
}

void
meta_window_place (MetaWindow        *window,
                   MetaPlaceFlag      flags,
                   int                x,
                   int                y,
                   int                new_width,
                   int                new_height,
                   int               *new_x,
                   int               *new_y)
{
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);
  g_autoptr (GList) windows = NULL;
  MetaLogicalMonitor *logical_monitor = NULL;
  gboolean place_centered = FALSE;
  MtkRectangle work_area;

  meta_topic (META_DEBUG_PLACEMENT, "Placing window %s", window->desc);

  g_return_if_fail (!window->placement.rule);

  switch (window->type)
    {
    /* Run placement algorithm on these. */
    case META_WINDOW_NORMAL:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
    case META_WINDOW_SPLASHSCREEN:
      break;

    /* Assume the app knows best how to place these, no placement
     * algorithm ever (other than "leave them as-is")
     */
    case META_WINDOW_DESKTOP:
    case META_WINDOW_DOCK:
    case META_WINDOW_TOOLBAR:
    case META_WINDOW_MENU:
    case META_WINDOW_UTILITY:
    /* override redirect window types: */
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_POPUP_MENU:
    case META_WINDOW_TOOLTIP:
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_COMBO:
    case META_WINDOW_DND:
    case META_WINDOW_OVERRIDE_OTHER:
      goto done;
    }

  if (meta_prefs_get_disable_workarounds ())
    {
      switch (window->type)
        {
        /* Only accept USER_POSITION on normal windows because the app is full
         * of shit claiming the user set -geometry for a dialog or dock
         */
        case META_WINDOW_NORMAL:
          if (window->size_hints.flags & META_SIZE_HINTS_USER_POSITION)
            {
              /* don't constrain with placement algorithm */
              meta_topic (META_DEBUG_PLACEMENT,
                          "Honoring USER_POSITION for %s instead of using placement algorithm",
                          window->desc);

              goto done;
            }
          break;

        /* Ignore even USER_POSITION on dialogs, splashscreen */
        case META_WINDOW_DIALOG:
        case META_WINDOW_MODAL_DIALOG:
        case META_WINDOW_SPLASHSCREEN:
          break;

        /* Assume the app knows best how to place these. */
        case META_WINDOW_DESKTOP:
        case META_WINDOW_DOCK:
        case META_WINDOW_TOOLBAR:
        case META_WINDOW_MENU:
        case META_WINDOW_UTILITY:
        /* override redirect window types: */
        case META_WINDOW_DROPDOWN_MENU:
        case META_WINDOW_POPUP_MENU:
        case META_WINDOW_TOOLTIP:
        case META_WINDOW_NOTIFICATION:
        case META_WINDOW_COMBO:
        case META_WINDOW_DND:
        case META_WINDOW_OVERRIDE_OTHER:
          if (window->size_hints.flags & META_SIZE_HINTS_PROGRAM_POSITION)
            {
              meta_topic (META_DEBUG_PLACEMENT,
                          "Not placing non-normal non-dialog window with PROGRAM_POSITION set");
              goto done;
            }
          break;
        }
    }
  else
    {
      /* workarounds enabled */

      if ((window->size_hints.flags & META_SIZE_HINTS_PROGRAM_POSITION) ||
          (window->size_hints.flags & META_SIZE_HINTS_USER_POSITION))
        {
          meta_topic (META_DEBUG_PLACEMENT,
                      "Not placing window with PROGRAM_POSITION or USER_POSITION set");
          avoid_being_obscured_as_second_modal_dialog (window, flags, &x, &y);
          goto done;
        }
    }

  if (!window->showing_for_first_time)
    logical_monitor = meta_window_get_main_logical_monitor (window);
  else
    logical_monitor = meta_backend_get_current_logical_monitor (backend);

  g_warn_if_fail (logical_monitor);

  /* Avoid crashing on the above warning, but we would like to fix the root
   * causes too some day.
   */
  if (!logical_monitor)
    {
      MetaMonitorManager *monitor_manager =
        meta_backend_get_monitor_manager (backend);

      logical_monitor =
        meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

      if (!logical_monitor)
        goto done;
    }

  meta_window_get_work_area_for_logical_monitor (window,
                                                 logical_monitor,
                                                 &work_area);

  if (window->type == META_WINDOW_DIALOG ||
      window->type == META_WINDOW_MODAL_DIALOG ||
      (window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND &&
       window->type == META_WINDOW_NORMAL))
    {
      MetaWindow *parent = meta_window_get_transient_for (window);

      if (parent)
        {
          MtkRectangle parent_frame_rect;

          meta_window_get_frame_rect (parent, &parent_frame_rect);

          y = parent_frame_rect.y;

          /* center of parent */
          x = parent_frame_rect.x + parent_frame_rect.width / 2;
          /* center of child over center of parent */
          x -= new_width / 2;

          /* "visually" center window over parent, leaving twice as
           * much space below as on top.
           */
          y += (parent_frame_rect.height - new_height) / 3;

          meta_topic (META_DEBUG_PLACEMENT,
                      "Centered window %s over transient parent",
                      window->desc);

          avoid_being_obscured_as_second_modal_dialog (window, flags, &x, &y);

          goto maybe_automaximize;
        }
    }

  /* FIXME UTILITY with transient set should be stacked up
   * on the sides of the parent window or something.
   */

  windows = find_windows_relevant_for_placement (window);

  place_centered = window_place_centered (window);

  if (place_centered)
    {
      x = work_area.x + (work_area.width - new_width) / 2;
      y = work_area.y + (work_area.height - new_height) / 2;

      meta_topic (META_DEBUG_PLACEMENT, "Centered window %s on monitor %d",
                  window->desc, logical_monitor->number);

      find_next_cascade (window, work_area, windows, new_width, new_height,
                         &x, &y, place_centered);
    }
  else
    {
      /* "Origin" placement algorithm */
      x = logical_monitor->rect.x;
      y = logical_monitor->rect.y;

      /* No good fit? Fall back to cascading... */
      if (!find_first_fit (window, windows,
                           logical_monitor, new_width, new_height,
                           &x, &y))
        {
          find_next_cascade (window, work_area, windows, new_width, new_height,
                             &x, &y, place_centered);
        }
    }

  /* If the window is being denied focus and isn't a transient of the
   * focus window, we do NOT want it to overlap with the focus window
   * if at all possible.  This is guaranteed to only be called if the
   * focus_window is non-NULL, and we try to avoid that window.
   */
  if (flags & META_PLACE_FLAG_DENIED_FOCUS_AND_NOT_TRANSIENT)
    {
      MetaWindow    *focus_window;
      gboolean       found_fit;

      focus_window = window->display->focus_window;
      g_assert (focus_window != NULL);

      /* No need to do anything if the window doesn't overlap at all */
      found_fit = !window_overlaps_focus_window (window, x, y);

      /* Try to do a first fit again, this time only taking into account the
       * focus window.
       */
      if (!found_fit)
        {
          GList *focus_window_list;
          focus_window_list = g_list_prepend (NULL, focus_window);

          /* Reset x and y ("origin" placement algorithm) */
          x = logical_monitor->rect.x;
          y = logical_monitor->rect.y;

          found_fit = find_first_fit (window, focus_window_list,
                                      logical_monitor, new_width, new_height,
                                      &x, &y);
          g_list_free (focus_window_list);
        }

      /* If that still didn't work, just place it where we can see as much
       * as possible.
       */
      if (!found_fit)
        find_most_freespace (window, focus_window, x, y, &x, &y);
    }

maybe_automaximize:
  if (meta_prefs_get_auto_maximize () &&
      window->showing_for_first_time &&
      window->has_maximize_func)
    {
      int window_area;
      int work_area_area;

      window_area = new_width * new_height;
      work_area_area = work_area.width * work_area.height;

      if (window_area > work_area_area * MAX_UNMAXIMIZED_WINDOW_AREA)
        meta_window_queue_auto_maximize (window);
    }

done:
  *new_x = x;
  *new_y = y;
}
