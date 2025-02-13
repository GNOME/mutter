/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Edge resistance for move/resize operations */

/*
 * Copyright (C) 2005, 2006 Elijah Newren
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

#include "compositor/edge-resistance.h"

#include "compositor/compositor-private.h"
#include "core/boxes-private.h"
#include "core/display-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/workspace-private.h"

typedef struct _MetaEdgeResistanceData MetaEdgeResistanceData;

struct _MetaEdgeResistanceData
{
  GArray *left_edges;
  GArray *right_edges;
  GArray *top_edges;
  GArray *bottom_edges;
};

static GQuark edge_resistance_data_quark = 0;

static gboolean
is_window_relevant_for_edges (MetaWindow *window)
{
  MetaWindowDrag *drag;

  if (!meta_window_should_be_showing (window))
    return FALSE;

  drag = meta_compositor_get_current_window_drag (window->display->compositor);
  if (drag && window == meta_window_drag_get_window (drag))
    return FALSE;

  switch (window->type)
    {
    case META_WINDOW_DESKTOP:
    case META_WINDOW_MENU:
    case META_WINDOW_SPLASHSCREEN:
      return FALSE;
    default:
      return TRUE;
    }
}

/* !WARNING!: this function can return invalid indices (namely, either -1 or
 * edges->len); this is by design, but you need to remember this.
 */
static int
find_index_of_edge_near_position (const GArray *edges,
                                  int           position,
                                  gboolean      want_interval_min,
                                  gboolean      horizontal)
{
  /* This is basically like a binary search, except that we're trying to
   * find a range instead of an exact value.  So, if we have in our array
   *   Value: 3  27 316 316 316 505 522 800 1213
   *   Index: 0   1   2   3   4   5   6   7    8
   * and we call this function with position=500 & want_interval_min=TRUE
   * then we should get 5 (because 505 is the first value bigger than 500).
   * If we call this function with position=805 and want_interval_min=FALSE
   * then we should get 7 (because 800 is the last value smaller than 800).
   * A couple more, to make things clear:
   *    position  want_interval_min  correct_answer
   *         316               TRUE               2
   *         316              FALSE               4
   *           2              FALSE              -1
   *        2000               TRUE               9
   */
  int low, high, mid;
  int compare;
  MetaEdge *edge;

  /* Initialize mid, edge, & compare in the off change that the array only
   * has one element.
   */
  mid  = 0;
  edge = g_array_index (edges, MetaEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;

  /* Begin the search... */
  low  = 0;
  high = edges->len - 1;
  while (low < high)
    {
      mid = low + (high - low)/2;
      edge = g_array_index (edges, MetaEdge*, mid);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      if (compare == position)
        break;

      if (compare > position)
        high = mid - 1;
      else
        low = mid + 1;
    }

  /* mid should now be _really_ close to the index we want, so we start
   * linearly searching.  However, note that we don't know if mid is less
   * than or greater than what we need and it's possible that there are
   * several equal values equal to what we were searching for and we ended
   * up in the middle of them instead of at the end.  So we may need to
   * move mid multiple locations over.
   */
  if (want_interval_min)
    {
      while (compare >= position && mid > 0)
        {
          mid--;
          edge = g_array_index (edges, MetaEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }
      while (compare < position && mid < (int)edges->len - 1)
        {
          mid++;
          edge = g_array_index (edges, MetaEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }

      /* Special case for no values in array big enough */
      if (compare < position)
        return edges->len;

      /* Return the found value */
      return mid;
    }
  else
    {
      while (compare <= position && mid < (int)edges->len - 1)
        {
          mid++;
          edge = g_array_index (edges, MetaEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }
      while (compare > position && mid > 0)
        {
          mid--;
          edge = g_array_index (edges, MetaEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }

      /* Special case for no values in array small enough */
      if (compare > position)
        return -1;

      /* Return the found value */
      return mid;
    }
}

static gboolean
points_on_same_side (int ref, int pt1, int pt2)
{
  return (pt1 - ref) * (pt2 - ref) > 0;
}

static int
find_nearest_position (const GArray       *edges,
                       int                 position,
                       int                 old_position,
                       const MtkRectangle *new_rect,
                       gboolean            horizontal,
                       gboolean            only_forward)
{
  /* This is basically just a binary search except that we're looking
   * for the value closest to position, rather than finding that
   * actual value.  Also, we ignore any edges that aren't relevant
   * given the horizontal/vertical position of new_rect.
   */
  int low, high, mid;
  int compare;
  MetaEdge *edge;
  int best, best_dist, i;
  gboolean edges_align;

  /* Initialize mid, edge, & compare in the off change that the array only
   * has one element.
   */
  mid  = 0;
  edge = g_array_index (edges, MetaEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;

  /* Begin the search... */
  low  = 0;
  high = edges->len - 1;
  while (low < high)
    {
      mid = low + (high - low)/2;
      edge = g_array_index (edges, MetaEdge*, mid);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      if (compare == position)
        break;

      if (compare > position)
        high = mid - 1;
      else
        low = mid + 1;
    }

  /* mid should now be _really_ close to the index we want, so we
   * start searching nearby for something that overlaps and is closer
   * than the original position.
   */
  best = old_position;
  best_dist = INT_MAX;

  /* Start the search at mid */
  edge = g_array_index (edges, MetaEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;
  edges_align = meta_rectangle_edge_aligns (new_rect, edge);
  if (edges_align &&
      (!only_forward || !points_on_same_side (position, compare, old_position)))
    {
      int dist = ABS (compare - position);
      if (dist < best_dist)
        {
          best = compare;
          best_dist = dist;
        }
    }

  /* Now start searching higher than mid */
  for (i = mid + 1; i < (int)edges->len; i++)
    {
      edge = g_array_index (edges, MetaEdge*, i);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      edges_align = horizontal ?
                    mtk_rectangle_vert_overlap (&edge->rect, new_rect) :
                    mtk_rectangle_horiz_overlap (&edge->rect, new_rect);

      if (edges_align &&
          (!only_forward ||
           !points_on_same_side (position, compare, old_position)))
        {
          int dist = ABS (compare - position);
          if (dist < best_dist)
            {
              best = compare;
              best_dist = dist;
            }
          break;
        }
    }

  /* Now start searching lower than mid */
  for (i = mid-1; i >= 0; i--)
    {
      edge = g_array_index (edges, MetaEdge*, i);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      edges_align = horizontal ?
                    mtk_rectangle_vert_overlap (&edge->rect, new_rect) :
                    mtk_rectangle_horiz_overlap (&edge->rect, new_rect);

      if (edges_align &&
          (!only_forward ||
           !points_on_same_side (position, compare, old_position)))
        {
          int dist = ABS (compare - position);
          if (dist < best_dist)
            {
              best = compare;
              best_dist = dist;
            }
          break;
        }
    }

  /* Return the best one found */
  return best;
}

static gboolean
movement_towards_edge (MetaSide side, int increment)
{
  switch (side)
    {
    case META_SIDE_LEFT:
    case META_SIDE_TOP:
      return increment < 0;
    case META_SIDE_RIGHT:
    case META_SIDE_BOTTOM:
      return increment > 0;
    default:
      g_assert_not_reached ();
      return FALSE;
    }
}

static int
apply_edge_resistance (MetaWindow         *window,
                       int                 old_pos,
                       int                 new_pos,
                       const MtkRectangle *old_rect,
                       const MtkRectangle *new_rect,
                       GArray             *edges,
                       gboolean            xdir,
                       gboolean            include_windows,
                       gboolean            keyboard_op)
{
  int i, begin, end;
  int last_edge;
  gboolean increasing = new_pos > old_pos;
  int      increment = increasing ? 1 : -1;

  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_WINDOW    = 16;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_WINDOW   =  0;
  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_MONITOR   = 32;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_MONITOR  =  0;
  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_SCREEN    = 32;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_SCREEN   =  0;

  /* Quit if no movement was specified */
  if (old_pos == new_pos)
    return new_pos;

  /* Get the range of indices in the edge array that we move past/to. */
  begin = find_index_of_edge_near_position (edges, old_pos,  increasing, xdir);
  end   = find_index_of_edge_near_position (edges, new_pos, !increasing, xdir);

  /* begin and end can be outside the array index, if the window is partially
   * off the screen
   */
  last_edge = edges->len - 1;
  begin = CLAMP (begin, 0, last_edge);
  end   = CLAMP (end,   0, last_edge);

  /* Loop over all these edges we're moving past/to. */
  i = begin;
  while ((increasing  && i <= end) ||
         (!increasing && i >= end))
    {
      gboolean  edges_align;
      MetaEdge *edge = g_array_index (edges, MetaEdge*, i);
      int       compare = xdir ? edge->rect.x : edge->rect.y;

      /* Find out if this edge is relevant */
      edges_align = meta_rectangle_edge_aligns (new_rect, edge)  ||
                    meta_rectangle_edge_aligns (old_rect, edge);

      /* Nothing to do unless the edges align */
      if (!edges_align)
        {
          /* Go to the next edge in the range */
          i += increment;
          continue;
        }

      /* Rest is easier to read if we split on keyboard vs. mouse op */
      if (keyboard_op)
        {
          if ((old_pos < compare && compare < new_pos) ||
              (old_pos > compare && compare > new_pos))
            return compare;
        }
      else /* mouse op */
        {
          int threshold;

          /* PIXEL DISTANCE MOUSE RESISTANCE: If the edge matters and the
           * user hasn't moved at least threshold pixels past this edge,
           * stop movement at this edge.  (Note that this is different from
           * keyboard resistance precisely because keyboard move ops are
           * relative to previous positions, whereas mouse move ops are
           * relative to differences in mouse position and mouse position
           * is an absolute quantity rather than a relative quantity)
           */

          /* First, determine the threshold */
          threshold = 0;
          switch (edge->edge_type)
            {
            case META_EDGE_WINDOW:
              if (!include_windows)
                break;
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_WINDOW;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_WINDOW;
              break;
            case META_EDGE_MONITOR:
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_MONITOR;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_MONITOR;
              break;
            case META_EDGE_SCREEN:
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_SCREEN;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_SCREEN;
              break;
            }

          if (ABS (compare - new_pos) < threshold)
            return compare;
        }

      /* Go to the next edge in the range */
      i += increment;
    }

  return new_pos;
}

static int
apply_edge_snapping (int                  old_pos,
                     int                  new_pos,
                     const MtkRectangle *new_rect,
                     GArray              *edges,
                     gboolean             xdir,
                     gboolean             keyboard_op)
{
  int snap_to;

  if (old_pos == new_pos)
    return new_pos;

  snap_to = find_nearest_position (edges,
                                   new_pos,
                                   old_pos,
                                   new_rect,
                                   xdir,
                                   keyboard_op);

  /* If mouse snap-moving, the user could easily accidentally move just a
   * couple pixels in a direction they didn't mean to move; so ignore snap
   * movement in those cases unless it's only a small number of pixels
   * anyway.
   */
  if (!keyboard_op &&
      ABS (snap_to - old_pos) >= 8 &&
      ABS (new_pos - old_pos) < 8)
    return old_pos;
  else
    /* Otherwise, return the snapping position found */
    return snap_to;
}

/* This function takes the position (including any frame) of the window and
 * a proposed new position (ignoring edge resistance/snapping), and then
 * applies edge resistance to EACH edge (separately) updating new_outer.
 * It returns true if new_outer is modified, false otherwise.
 *
 * display->grab_edge_resistance_data MUST already be setup or calling this
 * function will cause a crash.
 */
static gboolean
apply_edge_resistance_to_each_side (MetaEdgeResistanceData  *edge_data,
                                    MetaWindow              *window,
                                    const MtkRectangle      *old_outer,
                                    MtkRectangle            *new_outer,
                                    MetaEdgeResistanceFlags  flags,
                                    gboolean                 is_resize)
{
  MtkRectangle modified_rect;
  gboolean modified;
  int new_left, new_right, new_top, new_bottom;
  gboolean auto_snap, keyboard_op;

  auto_snap = flags & META_EDGE_RESISTANCE_SNAP;
  keyboard_op = flags & META_EDGE_RESISTANCE_KEYBOARD_OP;

  if (auto_snap && !meta_window_is_tiled_side_by_side (window))
    {
      /* Do the auto snapping instead of normal edge resistance; in all
       * cases, we allow snapping to opposite kinds of edges (e.g. left
       * sides of windows to both left and right edges.
       */

      new_left   = apply_edge_snapping (BOX_LEFT (*old_outer),
                                        BOX_LEFT (*new_outer),
                                        new_outer,
                                        edge_data->left_edges,
                                        TRUE,
                                        keyboard_op);

      new_right  = apply_edge_snapping (BOX_RIGHT (*old_outer),
                                        BOX_RIGHT (*new_outer),
                                        new_outer,
                                        edge_data->right_edges,
                                        TRUE,
                                        keyboard_op);

      new_top    = apply_edge_snapping (BOX_TOP (*old_outer),
                                        BOX_TOP (*new_outer),
                                        new_outer,
                                        edge_data->top_edges,
                                        FALSE,
                                        keyboard_op);

      new_bottom = apply_edge_snapping (BOX_BOTTOM (*old_outer),
                                        BOX_BOTTOM (*new_outer),
                                        new_outer,
                                        edge_data->bottom_edges,
                                        FALSE,
                                        keyboard_op);
    }
  else if (auto_snap && meta_window_is_tiled_side_by_side (window))
    {
      MtkRectangle workarea;
      guint i;

      const gfloat tile_edges[] =
        {
          1.0f / 4.0f,
          1.0f / 3.0f,
          1.0f / 2.0f,
          2.0f / 3.0f,
          3.0f / 4.0f,
        };

      meta_window_get_work_area_current_monitor (window, &workarea);

      new_left = new_outer->x;
      new_top = new_outer->y;
      new_right = new_outer->x + new_outer->width;
      new_bottom = new_outer->y + new_outer->height;

      /* When snapping tiled windows, we don't really care about the
       * x and y position, only about the width and height. Also, it
       * is special-cased (instead of relying on edge_data) because
       * we don't really care for other windows when calculating the
       * snapping points of tiled windows - we only care about the
       * work area and the target position.
       */
      for (i = 0; i < G_N_ELEMENTS (tile_edges); i++)
        {
          guint horizontal_point = workarea.x + (int) floorf (workarea.width * tile_edges[i]);

          if (ABS (horizontal_point - new_left) < 16)
            {
              new_left = horizontal_point;
              new_right = workarea.x + workarea.width;
            }
          else if (ABS (horizontal_point - new_right) < 16)
            {
              new_left = workarea.x;
              new_right = horizontal_point;
            }
        }
    }
  else
    {
      gboolean include_windows = flags & META_EDGE_RESISTANCE_WINDOWS;

      /* Disable edge resistance for resizes when windows have size
       * increment hints; see #346782.  For all other cases, apply
       * them.
       */
      if (!is_resize || window->size_hints.width_inc == 1)
        {
          /* Now, apply the normal horizontal edge resistance */
          new_left   = apply_edge_resistance (window,
                                              BOX_LEFT (*old_outer),
                                              BOX_LEFT (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->left_edges,
                                              TRUE,
                                              include_windows,
                                              keyboard_op);
          new_right  = apply_edge_resistance (window,
                                              BOX_RIGHT (*old_outer),
                                              BOX_RIGHT (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->right_edges,
                                              TRUE,
                                              include_windows,
                                              keyboard_op);
        }
      else
        {
          new_left  = new_outer->x;
          new_right = new_outer->x + new_outer->width;
        }
      /* Same for vertical resizes... */
      if (!is_resize || window->size_hints.height_inc == 1)
        {
          new_top    = apply_edge_resistance (window,
                                              BOX_TOP (*old_outer),
                                              BOX_TOP (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->top_edges,
                                              FALSE,
                                              include_windows,
                                              keyboard_op);
          new_bottom = apply_edge_resistance (window,
                                              BOX_BOTTOM (*old_outer),
                                              BOX_BOTTOM (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->bottom_edges,
                                              FALSE,
                                              include_windows,
                                              keyboard_op);
        }
      else
        {
          new_top    = new_outer->y;
          new_bottom = new_outer->y + new_outer->height;
        }
    }

  /* Determine whether anything changed, and save the changes */
  modified_rect = MTK_RECTANGLE_INIT (new_left,
                                      new_top,
                                      new_right - new_left,
                                      new_bottom - new_top);
  modified = !mtk_rectangle_equal (new_outer, &modified_rect);
  *new_outer = modified_rect;
  return modified;
}

static void
meta_edge_resistance_data_free (MetaEdgeResistanceData *edge_data)
{
  guint i,j;
  GHashTable *edges_to_be_freed;

  /* We first need to clean out any window edges */
  edges_to_be_freed = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                             g_free, NULL);
  for (i = 0; i < 4; i++)
    {
      GArray *tmp = NULL;
      MetaSide side;
      switch (i)
        {
        case 0:
          tmp = edge_data->left_edges;
          side = META_SIDE_LEFT;
          break;
        case 1:
          tmp = edge_data->right_edges;
          side = META_SIDE_RIGHT;
          break;
        case 2:
          tmp = edge_data->top_edges;
          side = META_SIDE_TOP;
          break;
        case 3:
          tmp = edge_data->bottom_edges;
          side = META_SIDE_BOTTOM;
          break;
        default:
          g_assert_not_reached ();
        }

      for (j = 0; j < tmp->len; j++)
        {
          MetaEdge *edge = g_array_index (tmp, MetaEdge*, j);
          if (edge->edge_type == META_EDGE_WINDOW &&
              edge->side_type == side)
            {
              /* The same edge will appear in two arrays, and we can't free
               * it yet we still need to compare edge->side_type for the other
               * array that it is in.  So store it in a hash table for later
               * freeing.  Could also do this in a simple linked list.
               */
              g_hash_table_insert (edges_to_be_freed, edge, edge);
            }
        }
    }

  /* Now free all the window edges (the key destroy function is g_free) */
  g_hash_table_destroy (edges_to_be_freed);

  /* Now free the arrays and data */
  g_array_free (edge_data->left_edges, TRUE);
  g_array_free (edge_data->right_edges, TRUE);
  g_array_free (edge_data->top_edges, TRUE);
  g_array_free (edge_data->bottom_edges, TRUE);
  edge_data->left_edges = NULL;
  edge_data->right_edges = NULL;
  edge_data->top_edges = NULL;
  edge_data->bottom_edges = NULL;

  g_free (edge_data);
}

void
meta_window_drag_edge_resistance_cleanup (MetaWindowDrag *window_drag)
{
  if (edge_resistance_data_quark == 0)
    return;

  g_object_set_qdata (G_OBJECT (window_drag), edge_resistance_data_quark, NULL);
}

static int
stupid_sort_requiring_extra_pointer_dereference (gconstpointer a,
                                                 gconstpointer b)
{
  const MetaEdge * const *a_edge = a;
  const MetaEdge * const *b_edge = b;
  return meta_rectangle_edge_cmp_ignore_type (*a_edge, *b_edge);
}

static MetaEdgeResistanceData *
cache_edges (MetaDisplay *display,
             GList *window_edges,
             GList *monitor_edges,
             GList *screen_edges)
{
  MetaEdgeResistanceData *edge_data;
  GList *tmp;
  int num_left, num_right, num_top, num_bottom;
  int i;

  /*
   * 0th: Print debugging information to the log about the edges
   */
#ifdef WITH_VERBOSE_MODE
  if (meta_is_verbose())
    {
      int max_edges = MAX (MAX( g_list_length (window_edges),
                                g_list_length (monitor_edges)),
                           g_list_length (screen_edges));
      char big_buffer[(EDGE_LENGTH+2)*max_edges];

      meta_rectangle_edge_list_to_string (window_edges, ", ", big_buffer);
      meta_topic (META_DEBUG_EDGE_RESISTANCE,
                  "Window edges for resistance  : %s", big_buffer);

      meta_rectangle_edge_list_to_string (monitor_edges, ", ", big_buffer);
      meta_topic (META_DEBUG_EDGE_RESISTANCE,
                  "Monitor edges for resistance: %s", big_buffer);

      meta_rectangle_edge_list_to_string (screen_edges, ", ", big_buffer);
      meta_topic (META_DEBUG_EDGE_RESISTANCE,
                  "Screen edges for resistance  : %s", big_buffer);
    }
#endif

  /*
   * 1st: Get the total number of each kind of edge
   */
  num_left = num_right = num_top = num_bottom = 0;
  for (i = 0; i < 3; i++)
    {
      tmp = NULL;
      switch (i)
        {
        case 0:
          tmp = window_edges;
          break;
        case 1:
          tmp = monitor_edges;
          break;
        case 2:
          tmp = screen_edges;
          break;
        default:
          g_assert_not_reached ();
        }

      while (tmp)
        {
          MetaEdge *edge = tmp->data;
          switch (edge->side_type)
            {
            case META_SIDE_LEFT:
              num_left++;
              break;
            case META_SIDE_RIGHT:
              num_right++;
              break;
            case META_SIDE_TOP:
              num_top++;
              break;
            case META_SIDE_BOTTOM:
              num_bottom++;
              break;
            default:
              g_assert_not_reached ();
            }
          tmp = tmp->next;
        }
    }

  /*
   * 2nd: Allocate the edges
   */
  edge_data = g_new0 (MetaEdgeResistanceData, 1);
  edge_data->left_edges   = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(MetaEdge*),
                                               num_left + num_right);
  edge_data->right_edges  = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(MetaEdge*),
                                               num_left + num_right);
  edge_data->top_edges    = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(MetaEdge*),
                                               num_top + num_bottom);
  edge_data->bottom_edges = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(MetaEdge*),
                                               num_top + num_bottom);

  /*
   * 3rd: Add the edges to the arrays
   */
  for (i = 0; i < 3; i++)
    {
      tmp = NULL;
      switch (i)
        {
        case 0:
          tmp = window_edges;
          break;
        case 1:
          tmp = monitor_edges;
          break;
        case 2:
          tmp = screen_edges;
          break;
        default:
          g_assert_not_reached ();
        }

      while (tmp)
        {
          MetaEdge *edge = tmp->data;
          switch (edge->side_type)
            {
            case META_SIDE_LEFT:
            case META_SIDE_RIGHT:
              g_array_append_val (edge_data->left_edges, edge);
              g_array_append_val (edge_data->right_edges, edge);
              break;
            case META_SIDE_TOP:
            case META_SIDE_BOTTOM:
              g_array_append_val (edge_data->top_edges, edge);
              g_array_append_val (edge_data->bottom_edges, edge);
              break;
            default:
              g_assert_not_reached ();
            }
          tmp = tmp->next;
        }
    }

  /*
   * 4th: Sort the arrays (FIXME: This is kinda dumb since the arrays were
   * individually sorted earlier and we could have done this faster and
   * avoided this sort by sticking them into the array with some simple
   * merging of the lists).
   */
  g_array_sort (edge_data->left_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->right_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->top_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->bottom_edges,
                stupid_sort_requiring_extra_pointer_dereference);

  return edge_data;
}

static MetaEdgeResistanceData *
compute_resistance_and_snapping_edges (MetaWindowDrag *window_drag)
{
  MetaEdgeResistanceData *edge_data;
  GList *l;
  /* Lists of window positions (rects) and their relative stacking positions */
  int stack_position;
  g_autoslist (MtkRectangle) obscuring_windows = NULL;
  g_autoptr (GList) stacked_windows = NULL;
  g_autoptr (GSList) window_stacking = NULL;
  g_autoptr (GList) edges = NULL;
  /* The portions of the above lists that still remain at the stacking position
   * in the layer that we are working on
   */
  GSList *rem_windows, *rem_win_stacking;
  MetaWindow *window = meta_window_drag_get_window (window_drag);
  MetaDisplay *display = window->display;
  MetaWorkspaceManager *workspace_manager = display->workspace_manager;

  meta_topic (META_DEBUG_WINDOW_OPS,
              "Computing edges to resist-movement or snap-to for %s.",
              meta_window_drag_get_window (window_drag)->desc);

  /*
   * 1st: Get the list of relevant windows, from bottom to top
   */
  stacked_windows =
    meta_stack_list_windows (display->stack,
                             workspace_manager->active_workspace);

  /*
   * 2nd: we need to separate that stacked list into a list of windows that
   * can obscure other edges.  To make sure we only have windows obscuring
   * those below it instead of going both ways, we also need to keep a
   * counter list.  Messy, I know.
   */
  stack_position = 0;
  for (l = stacked_windows; l; l = l->next)
    {
      MetaWindow *cur_window = l->data;

      if (is_window_relevant_for_edges (cur_window))
        {
          MtkRectangle *new_rect;

          new_rect = mtk_rectangle_new_empty ();
          meta_window_get_frame_rect (cur_window, new_rect);
          obscuring_windows = g_slist_prepend (obscuring_windows, new_rect);
          window_stacking =
            g_slist_prepend (window_stacking, GINT_TO_POINTER (stack_position));
        }

      stack_position++;
    }
  /* Put 'em in bottom to top order */
  rem_windows = obscuring_windows = g_slist_reverse (obscuring_windows);
  rem_win_stacking = window_stacking = g_slist_reverse (window_stacking);

  /*
   * 3rd: loop over the windows again, this time getting the edges from
   * them and removing intersections with the relevant obscuring_windows &
   * obscuring_docks.
   */
  edges = NULL;
  stack_position = 0;
  for (l = stacked_windows; l; l = l->next)
    {
      MetaWindow *cur_window = l->data;
      MtkRectangle  cur_rect;
      meta_window_get_frame_rect (cur_window, &cur_rect);

      /* Check if we want to use this window's edges for edge
       * resistance (note that dock edges are considered screen edges
       * which are handled separately
       */
      if (is_window_relevant_for_edges (cur_window) &&
          cur_window->type != META_WINDOW_DOCK)
        {
          GList *new_edges;
          MetaEdge *new_edge;
          MtkRectangle display_rect = { 0 };
          MtkRectangle reduced;

          meta_display_get_size (display,
                                 &display_rect.width, &display_rect.height);

          /* We don't care about snapping to any portion of the window that
           * is offscreen (we also don't care about parts of edges covered
           * by other windows or DOCKS, but that's handled below).
           */
          mtk_rectangle_intersect (&cur_rect,
                                   &display_rect,
                                   &reduced);

          new_edges = NULL;

          /* Left side of this window is resistance for the right edge of
           * the window being moved.
           */
          new_edge = g_new (MetaEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.width = 0;
          new_edge->side_type = META_SIDE_RIGHT;
          new_edge->edge_type = META_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Right side of this window is resistance for the left edge of
           * the window being moved.
           */
          new_edge = g_new (MetaEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.x += new_edge->rect.width;
          new_edge->rect.width = 0;
          new_edge->side_type = META_SIDE_LEFT;
          new_edge->edge_type = META_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Top side of this window is resistance for the bottom edge of
           * the window being moved.
           */
          new_edge = g_new (MetaEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.height = 0;
          new_edge->side_type = META_SIDE_BOTTOM;
          new_edge->edge_type = META_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Top side of this window is resistance for the bottom edge of
           * the window being moved.
           */
          new_edge = g_new (MetaEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.y += new_edge->rect.height;
          new_edge->rect.height = 0;
          new_edge->side_type = META_SIDE_TOP;
          new_edge->edge_type = META_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Update the remaining windows to only those at a higher
           * stacking position than this one.
           */
          while (rem_win_stacking &&
                 stack_position >= GPOINTER_TO_INT (rem_win_stacking->data))
            {
              rem_windows      = rem_windows->next;
              rem_win_stacking = rem_win_stacking->next;
            }

          /* Remove edge portions overlapped by rem_windows and rem_docks */
          new_edges =
            meta_rectangle_remove_intersections_with_boxes_from_edges (
              new_edges,
              rem_windows);

          /* Save the new edges */
          edges = g_list_concat (new_edges, edges);
        }

      stack_position++;
    }

  /* Sort the list.  FIXME: Should I bother with this sorting?  I just
   * sort again later in cache_edges() anyway...
   */
  edges = g_list_sort (edges, meta_rectangle_edge_cmp);

  /*
   * 5th: Cache the combination of these edges with the onscreen and
   * monitor edges in an array for quick access.  Free the edges since
   * they've been cached elsewhere.
   */
  edge_data = cache_edges (display,
                           edges,
                           workspace_manager->active_workspace->monitor_edges,
                           workspace_manager->active_workspace->screen_edges);

  return edge_data;
}

static MetaEdgeResistanceData *
meta_window_drag_ensure_edge_resistance_data (MetaWindowDrag *window_drag)
{
  MetaEdgeResistanceData *edge_data;

  if (G_UNLIKELY (edge_resistance_data_quark == 0))
    {
      edge_resistance_data_quark =
        g_quark_from_static_string ("meta-window-drag-edge-data");
    }

  edge_data = g_object_get_qdata (G_OBJECT (window_drag),
                                  edge_resistance_data_quark);

  if (!edge_data)
    {
      edge_data = compute_resistance_and_snapping_edges (window_drag);
      g_object_set_qdata_full (G_OBJECT (window_drag),
                               edge_resistance_data_quark,
                               edge_data,
                               (GDestroyNotify) meta_edge_resistance_data_free);
    }

  return edge_data;
}

void
meta_window_drag_edge_resistance_for_move (MetaWindowDrag          *window_drag,
                                           int                     *new_x,
                                           int                     *new_y,
                                           MetaEdgeResistanceFlags  flags)
{
  MetaEdgeResistanceData *edge_data;
  MtkRectangle old_outer, proposed_outer, new_outer;
  gboolean is_resize, is_keyboard_op, snap;
  MetaWindow *window;

  window = meta_window_drag_get_window (window_drag);

  meta_window_get_frame_rect (window, &old_outer);

  proposed_outer = old_outer;
  proposed_outer.x = *new_x;
  proposed_outer.y = *new_y;
  new_outer = proposed_outer;

  snap = flags & META_EDGE_RESISTANCE_SNAP;
  is_keyboard_op = flags & META_EDGE_RESISTANCE_KEYBOARD_OP;

  edge_data = meta_window_drag_ensure_edge_resistance_data (window_drag);

  is_resize = FALSE;
  if (apply_edge_resistance_to_each_side (edge_data,
                                          window,
                                          &old_outer,
                                          &new_outer,
                                          flags,
                                          is_resize))
    {
      /* apply_edge_resistance_to_each_side independently applies
       * resistance to both the right and left edges of new_outer as both
       * could meet areas of resistance.  But we don't want a resize, so we
       * just have both edges move according to the stricter of the
       * resistances.  Same thing goes for top & bottom edges.
       */
      MtkRectangle *reference;
      int left_change, right_change, smaller_x_change;
      int top_change, bottom_change, smaller_y_change;

      if (snap && !is_keyboard_op)
        reference = &proposed_outer;
      else
        reference = &old_outer;

      left_change  = BOX_LEFT (new_outer)  - BOX_LEFT (*reference);
      right_change = BOX_RIGHT (new_outer) - BOX_RIGHT (*reference);
      if (     snap && is_keyboard_op && left_change == 0)
        smaller_x_change = right_change;
      else if (snap && is_keyboard_op && right_change == 0)
        smaller_x_change = left_change;
      else if (ABS (left_change) < ABS (right_change))
        smaller_x_change = left_change;
      else
        smaller_x_change = right_change;

      top_change    = BOX_TOP (new_outer)    - BOX_TOP (*reference);
      bottom_change = BOX_BOTTOM (new_outer) - BOX_BOTTOM (*reference);
      if (     snap && is_keyboard_op && top_change == 0)
        smaller_y_change = bottom_change;
      else if (snap && is_keyboard_op && bottom_change == 0)
        smaller_y_change = top_change;
      else if (ABS (top_change) < ABS (bottom_change))
        smaller_y_change = top_change;
      else
        smaller_y_change = bottom_change;

      *new_x = old_outer.x + smaller_x_change +
              (BOX_LEFT (*reference) - BOX_LEFT (old_outer));
      *new_y = old_outer.y + smaller_y_change +
              (BOX_TOP (*reference) - BOX_TOP (old_outer));

      meta_topic (META_DEBUG_EDGE_RESISTANCE,
                  "outer x & y move-to coordinate changed from %d,%d to %d,%d",
                  proposed_outer.x, proposed_outer.y,
                  *new_x, *new_y);
    }
}

void
meta_window_drag_edge_resistance_for_resize (MetaWindowDrag          *window_drag,
                                             int                     *new_width,
                                             int                     *new_height,
                                             MetaGravity              gravity,
                                             MetaEdgeResistanceFlags  flags)
{
  MetaEdgeResistanceData *edge_data;
  MtkRectangle old_outer, new_outer;
  int proposed_outer_width, proposed_outer_height;
  MetaWindow *window;

  window = meta_window_drag_get_window (window_drag);

  meta_window_get_frame_rect (window, &old_outer);
  proposed_outer_width  = *new_width;
  proposed_outer_height = *new_height;
  meta_rectangle_resize_with_gravity (&old_outer,
                                      &new_outer,
                                      gravity,
                                      proposed_outer_width,
                                      proposed_outer_height);

  edge_data = meta_window_drag_ensure_edge_resistance_data (window_drag);

  if (apply_edge_resistance_to_each_side (edge_data,
                                          window,
                                          &old_outer,
                                          &new_outer,
                                          flags,
                                          TRUE))
    {
      *new_width = new_outer.width;
      *new_height = new_outer.height;

      meta_topic (META_DEBUG_EDGE_RESISTANCE,
                  "outer width & height got changed from %d,%d to %d,%d",
                  proposed_outer_width, proposed_outer_height,
                  new_outer.width, new_outer.height);
    }
}
