/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2; -*- */

/* 
 * Copyright 2012, 2013 Red Hat Inc.
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
 * Authors: Jaster St. Pierre <jstpierr@redhat.com>
 *          Giovanni Campagna <gcampagn@redhat.com>
 */

/**
 * SECTION:barrier
 * @Title: MetaBarrier
 * @Short_Description: Pointer barriers
 */

#include "config.h"

#include <glib-object.h>
#include <math.h>

#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <meta/util.h>
#include <meta/barrier.h>
#include "display-private.h"
#include "mutter-enum-types.h"
#include "barrier-private.h"
#include "core.h"

G_DEFINE_TYPE (MetaBarrier, meta_barrier, G_TYPE_OBJECT)

enum {
  PROP_0,

  PROP_DISPLAY,

  PROP_X1,
  PROP_Y1,
  PROP_X2,
  PROP_Y2,
  PROP_DIRECTIONS,

  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

enum {
  HIT,
  LEFT,

  LAST_SIGNAL,
};

static guint obj_signals[LAST_SIGNAL];

struct _MetaBarrierPrivate
{
  MetaDisplay *display;

  int x1;
  int y1;
  int x2;
  int y2;

  MetaBarrierDirection directions;

  /* x11 */
  PointerBarrier xbarrier;

  /* wayland */
  gboolean active;
  gboolean seen, hit;

  int barrier_event_id;
  int release_event_id;
  guint32 last_timestamp;
};

struct _MetaBarrierManager
{
  GList *barriers;
} *global_barrier_manager;

static void meta_barrier_event_unref (MetaBarrierEvent *event);

static void
meta_barrier_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MetaBarrier *barrier = META_BARRIER (object);
  MetaBarrierPrivate *priv = barrier->priv;
  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    case PROP_X1:
      g_value_set_int (value, priv->x1);
      break;
    case PROP_Y1:
      g_value_set_int (value, priv->y1);
      break;
    case PROP_X2:
      g_value_set_int (value, priv->x2);
      break;
    case PROP_Y2:
      g_value_set_int (value, priv->y2);
      break;
    case PROP_DIRECTIONS:
      g_value_set_flags (value, priv->directions);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_barrier_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  MetaBarrier *barrier = META_BARRIER (object);
  MetaBarrierPrivate *priv = barrier->priv;
  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_X1:
      priv->x1 = g_value_get_int (value);
      break;
    case PROP_Y1:
      priv->y1 = g_value_get_int (value);
      break;
    case PROP_X2:
      priv->x2 = g_value_get_int (value);
      break;
    case PROP_Y2:
      priv->y2 = g_value_get_int (value);
      break;
    case PROP_DIRECTIONS:
      priv->directions = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_barrier_dispose (GObject *object)
{
  MetaBarrier *barrier = META_BARRIER (object);
  MetaBarrierPrivate *priv = barrier->priv;

  if (meta_barrier_is_active (barrier))
    {
      meta_bug ("MetaBarrier wrapper %p for X barrier %ld was destroyed"
                " while the X barrier is still active.",
                barrier, priv->xbarrier);
    }

  G_OBJECT_CLASS (meta_barrier_parent_class)->dispose (object);
}

gboolean
meta_barrier_is_active (MetaBarrier *barrier)
{
  if (meta_is_wayland_compositor ())
    return barrier->priv->active;
  else
    return barrier->priv->xbarrier != 0;
}

/**
 * meta_barrier_release:
 * @barrier: The barrier to release
 * @event: The event to release the pointer for
 *
 * In XI2.3, pointer barriers provide a feature where they can
 * be temporarily released so that the pointer goes through
 * them. Pass a #MetaBarrierEvent to release the barrier for
 * this event sequence.
 */
void
meta_barrier_release (MetaBarrier      *barrier,
                      MetaBarrierEvent *event)
{
  MetaBarrierPrivate *priv;

  priv = barrier->priv;

  if (meta_is_wayland_compositor ())
    {
      priv->release_event_id = event->event_id;
    }
  else
    {
#ifdef HAVE_XI23
      if (META_DISPLAY_HAS_XINPUT_23 (priv->display))
        {
          XIBarrierReleasePointer (priv->display->xdisplay,
                                   META_VIRTUAL_CORE_POINTER_ID,
                                   priv->xbarrier, event->event_id);
        }
#endif /* HAVE_XI23 */
    }
}

static void
meta_barrier_constructed (GObject *object)
{
  MetaBarrier *barrier = META_BARRIER (object);
  MetaBarrierPrivate *priv = barrier->priv;
  Display *dpy;
  Window root;

  g_return_if_fail (priv->x1 == priv->x2 || priv->y1 == priv->y2);

  if (priv->display == NULL)
    {
      g_warning ("A display must be provided when constructing a barrier.");
      return;
    }

  if (meta_is_wayland_compositor ())
    {
      MetaBarrierManager *manager = meta_barrier_manager_get ();

      manager->barriers = g_list_prepend (manager->barriers, g_object_ref (barrier));
      priv->active = TRUE;
    }
  else
    {
      dpy = priv->display->xdisplay;
      root = DefaultRootWindow (dpy);

      priv->xbarrier = XFixesCreatePointerBarrier (dpy, root,
                                                   priv->x1, priv->y1,
                                                   priv->x2, priv->y2,
                                                   priv->directions, 0, NULL);

      /* Take a ref that we'll release when the XID dies inside destroy(),
       * so that the object stays alive and doesn't get GC'd. */
      g_object_ref (barrier);

      g_hash_table_insert (priv->display->xids, &priv->xbarrier, barrier);
    }

  G_OBJECT_CLASS (meta_barrier_parent_class)->constructed (object);
}

static void
meta_barrier_class_init (MetaBarrierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_barrier_get_property;
  object_class->set_property = meta_barrier_set_property;
  object_class->dispose = meta_barrier_dispose;
  object_class->constructed = meta_barrier_constructed;

  obj_props[PROP_DISPLAY] =
    g_param_spec_object ("display",
                         "Display",
                         "The display to construct the pointer barrier on",
                         META_TYPE_DISPLAY,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_X1] =
    g_param_spec_int ("x1",
                      "X1",
                      "The first X coordinate of the barrier",
                      0, G_MAXSHORT, 0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_Y1] =
    g_param_spec_int ("y1",
                      "Y1",
                      "The first Y coordinate of the barrier",
                      0, G_MAXSHORT, 0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_X2] =
    g_param_spec_int ("x2",
                      "X2",
                      "The second X coordinate of the barrier",
                      0, G_MAXSHORT, G_MAXSHORT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_Y2] =
    g_param_spec_int ("y2",
                      "Y2",
                      "The second Y coordinate of the barrier",
                      0, G_MAXSHORT, G_MAXSHORT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_DIRECTIONS] =
    g_param_spec_flags ("directions",
                        "Directions",
                        "A set of directions to let the pointer through",
                        META_TYPE_BARRIER_DIRECTION,
                        0,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, obj_props);

  /**
   * MetaBarrier::hit:
   * @barrier: The #MetaBarrier that was hit
   * @event: A #MetaBarrierEvent that has the details of how
   * the barrier was hit.
   *
   * When a pointer barrier is hit, this will trigger. This
   * requires an XI2-enabled server.
   */
  obj_signals[HIT] =
    g_signal_new ("hit",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_BARRIER_EVENT);

  /**
   * MetaBarrier::left:
   * @barrier: The #MetaBarrier that was left
   * @event: A #MetaBarrierEvent that has the details of how
   * the barrier was left.
   *
   * When a pointer barrier hitbox was left, this will trigger.
   * This requires an XI2-enabled server.
   */
  obj_signals[LEFT] =
    g_signal_new ("left",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_BARRIER_EVENT);

  g_type_class_add_private (object_class, sizeof(MetaBarrierPrivate));
}

void
meta_barrier_destroy (MetaBarrier *barrier)
{
  MetaBarrierPrivate *priv = barrier->priv;
  Display *dpy;

  if (priv->display == NULL)
    return;

  if (meta_is_wayland_compositor ())
    {
      MetaBarrierManager *manager = meta_barrier_manager_get ();

      manager->barriers = g_list_remove (manager->barriers, barrier);
      g_object_unref (barrier);
    }
  else
    {
      dpy = priv->display->xdisplay;

      if (!meta_barrier_is_active (barrier))
        return;

      XFixesDestroyPointerBarrier (dpy, priv->xbarrier);
      g_hash_table_remove (priv->display->xids, &priv->xbarrier);
      priv->xbarrier = 0;

      g_object_unref (barrier);
    }
}

static void
meta_barrier_init (MetaBarrier *barrier)
{
  barrier->priv = G_TYPE_INSTANCE_GET_PRIVATE (barrier, META_TYPE_BARRIER, MetaBarrierPrivate);
}

#ifdef HAVE_XI23
static void
meta_barrier_fire_event (MetaBarrier    *barrier,
                         XIBarrierEvent *xevent)
{
  MetaBarrierEvent *event = g_slice_new0 (MetaBarrierEvent);

  event->ref_count = 1;
  event->event_id = xevent->eventid;
  event->time = xevent->time;
  event->dt = xevent->dtime;

  event->x = xevent->root_x;
  event->y = xevent->root_y;
  event->dx = xevent->dx;
  event->dy = xevent->dy;

  event->released = (xevent->flags & XIBarrierPointerReleased) != 0;
  event->grabbed = (xevent->flags & XIBarrierDeviceIsGrabbed) != 0;

  switch (xevent->evtype)
    {
    case XI_BarrierHit:
      g_signal_emit (barrier, obj_signals[HIT], 0, event);
      break;
    case XI_BarrierLeave:
      g_signal_emit (barrier, obj_signals[LEFT], 0, event);
      break;
    default:
      g_assert_not_reached ();
    }

  meta_barrier_event_unref (event);
}

gboolean
meta_display_process_barrier_event (MetaDisplay    *display,
                                    XIBarrierEvent *xev)
{
  MetaBarrier *barrier;

  if (meta_is_wayland_compositor ())
    return FALSE;

  barrier = g_hash_table_lookup (display->xids, &xev->barrier);
  if (barrier != NULL)
    {
      meta_barrier_fire_event (barrier, xev);
      return TRUE;
    }

  return FALSE;
}
#endif /* HAVE_XI23 */

/*
 * The following code was copied and adapted from the X server (Xi/xibarriers.c)
 *
 * Copyright 2012 Red Hat, Inc.
 * Copyright © 2002 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

static gboolean
barrier_is_horizontal(MetaBarrier *barrier)
{
  return barrier->priv->y1 == barrier->priv->y2;
}

static gboolean
barrier_is_vertical(MetaBarrier *barrier)
{
  return barrier->priv->x1 == barrier->priv->x2;
}

/*
 * @return The set of barrier movement directions the movement vector
 * x1/y1 → x2/y2 represents.
 */
static int
barrier_get_direction(int x1, int y1, int x2, int y2)
{
  int direction = 0;

  /* which way are we trying to go */
  if (x2 > x1)
    direction |= META_BARRIER_DIRECTION_POSITIVE_X;
  if (x2 < x1)
    direction |= META_BARRIER_DIRECTION_NEGATIVE_X;
  if (y2 > y1)
    direction |= META_BARRIER_DIRECTION_POSITIVE_Y;
  if (y2 < y1)
    direction |= META_BARRIER_DIRECTION_NEGATIVE_Y;

  return direction;
}

/*
 * Test if the barrier may block movement in the direction defined by
 * x1/y1 → x2/y2. This function only tests whether the directions could be
 * blocked, it does not test if the barrier actually blocks the movement.
 *
 * @return TRUE if the barrier blocks the direction of movement or FALSE
 * otherwise.
 */
static gboolean
barrier_is_blocking_direction(MetaBarrier          *barrier,
                              MetaBarrierDirection  direction)
{
  /* Barriers define which way is ok, not which way is blocking */
  return (barrier->priv->directions & direction) != direction;
}

static gboolean
inside_segment(int v, int v1, int v2)
{
  if (v1 < 0 && v2 < 0) /* line */
    return TRUE;
  else if (v1 < 0)      /* ray */
    return v <= v2;
  else if (v2 < 0)      /* ray */
    return v >= v1;
  else                  /* line segment */
    return v >= v1 && v <= v2;
}

#define T(v, a, b) (((float)v) - (a)) / ((b) - (a))
#define F(t, a, b) ((t) * ((a) - (b)) + (a))

/*
 * Test if the movement vector x1/y1 → x2/y2 is intersecting with the
 * barrier. A movement vector with the startpoint or endpoint adjacent to
 * the barrier itself counts as intersecting.
 *
 * @param x1 X start coordinate of movement vector
 * @param y1 Y start coordinate of movement vector
 * @param x2 X end coordinate of movement vector
 * @param y2 Y end coordinate of movement vector
 * @param[out] distance The distance between the start point and the
 * intersection with the barrier (if applicable).
 * @return TRUE if the barrier intersects with the given vector
 */
static gboolean
barrier_is_blocking(MetaBarrier *barrier,
                    int x1, int y1, int x2, int y2, double *distance)
{
  if (barrier_is_vertical (barrier))
    {
      float t, y;
      t = T (barrier->priv->x1, x1, x2);
      if (t < 0 || t > 1)
        return FALSE;

      /* Edge case: moving away from barrier. */
      if (x2 > x1 && t == 0)
        return FALSE;

      y = F (t, y1, y2);
      if (!inside_segment (y, barrier->priv->y1, barrier->priv->y2))
        return FALSE;

      *distance = sqrt ((y - y1) * (y - y1) + (barrier->priv->x1 - x1) * (barrier->priv->x1 - x1));
      return TRUE;
    }
  else
    {
      float t, x;
      t = T (barrier->priv->y1, y1, y2);
      if (t < 0 || t > 1)
        return FALSE;

      /* Edge case: moving away from barrier. */
      if (y2 > y1 && t == 0)
        return FALSE;

      x = F(t, x1, x2);
      if (!inside_segment (x, barrier->priv->x1, barrier->priv->x2))
        return FALSE;

      *distance = sqrt ((x - x1) * (x - x1) + (barrier->priv->y1 - y1) * (barrier->priv->y1 - y1));
      return TRUE;
    }
}

#define HIT_EDGE_EXTENTS 2
static gboolean
barrier_inside_hit_box(MetaBarrier *barrier, int x, int y)
{
  int x1, x2, y1, y2;
  int dir;

  x1 = barrier->priv->x1;
  x2 = barrier->priv->x2;
  y1 = barrier->priv->y1;
  y2 = barrier->priv->y2;
  dir = ~(barrier->priv->directions);

  if (barrier_is_vertical (barrier))
    {
      if (dir & META_BARRIER_DIRECTION_POSITIVE_X)
        x1 -= HIT_EDGE_EXTENTS;
      if (dir & META_BARRIER_DIRECTION_NEGATIVE_X)
        x2 += HIT_EDGE_EXTENTS;
    }
  if (barrier_is_horizontal (barrier))
    {
      if (dir & META_BARRIER_DIRECTION_POSITIVE_Y)
        y1 -= HIT_EDGE_EXTENTS;
      if (dir & META_BARRIER_DIRECTION_NEGATIVE_Y)
        y2 += HIT_EDGE_EXTENTS;
    }

  return x >= x1 && x <= x2 && y >= y1 && y <= y2;
}

/*
 * Find the nearest barrier client that is blocking movement from x1/y1 to x2/y2.
 *
 * @param dir Only barriers blocking movement in direction dir are checked
 * @param x1 X start coordinate of movement vector
 * @param y1 Y start coordinate of movement vector
 * @param x2 X end coordinate of movement vector
 * @param y2 Y end coordinate of movement vector
 * @return The barrier nearest to the movement origin that blocks this movement.
 */
static MetaBarrier *
barrier_find_nearest(MetaBarrierManager *manager,
                     int                 dir,
                     int                 x1,
                     int                 y1,
                     int                 x2,
                     int                 y2)
{
  GList *iter;
  MetaBarrier *nearest = NULL;
  double min_distance = INT_MAX;      /* can't get higher than that in X anyway */

  for (iter = manager->barriers; iter; iter = iter->next)
    {
      MetaBarrier *b = iter->data;
      double distance;

      if (b->priv->seen || !b->priv->active)
        continue;

      if (!barrier_is_blocking_direction (b, dir))
        continue;

      if (barrier_is_blocking (b, x1, y1, x2, y2, &distance))
        {
          if (min_distance > distance)
            {
              min_distance = distance;
              nearest = b;
            }
        }
    }

  return nearest;
}

/*
 * Clamp to the given barrier given the movement direction specified in dir.
 *
 * @param barrier The barrier to clamp to
 * @param dir The movement direction
 * @param[out] x The clamped x coordinate.
 * @param[out] y The clamped x coordinate.
 */
static void
barrier_clamp_to_barrier(MetaBarrier *barrier,
                         int          dir,
                         float       *x,
                         float       *y)
{
  if (barrier_is_vertical (barrier))
    {
      if ((dir & META_BARRIER_DIRECTION_NEGATIVE_X) & ~barrier->priv->directions)
        *x = barrier->priv->x1;
      if ((dir & META_BARRIER_DIRECTION_POSITIVE_X) & ~barrier->priv->directions)
        *x = barrier->priv->x1 - 1;
    }
  if (barrier_is_horizontal (barrier))
    {
      if ((dir & META_BARRIER_DIRECTION_NEGATIVE_Y) & ~barrier->priv->directions)
        *y = barrier->priv->y1;
      if ((dir & META_BARRIER_DIRECTION_POSITIVE_Y) & ~barrier->priv->directions)
        *y = barrier->priv->y1 - 1;
    }
}

static gboolean
emit_hit_event (gpointer data)
{
  MetaBarrierEvent *event = data;

  g_signal_emit (event->barrier, obj_signals[HIT], 0, event);

  meta_barrier_event_unref (event);
  return FALSE;
}

static gboolean
emit_left_event (gpointer data)
{
  MetaBarrierEvent *event = data;

  g_signal_emit (event->barrier, obj_signals[LEFT], 0, event);

  meta_barrier_event_unref (event);
  return FALSE;
}

void
meta_barrier_manager_constrain_cursor (MetaBarrierManager *manager,
                                       guint32             time,
                                       float               current_x,
                                       float               current_y,
                                       float              *new_x,
                                       float              *new_y)
{
  float x = *new_x;
  float y = *new_y;
  int dir;
  MetaBarrier *nearest = NULL;
  GList *iter;
  float dx = x - current_x;
  float dy = y - current_y;

  /* How this works:
   * Given the origin and the movement vector, get the nearest barrier
   * to the origin that is blocking the movement.
   * Clamp to that barrier.
   * Then, check from the clamped intersection to the original
   * destination, again finding the nearest barrier and clamping.
   */
  dir = barrier_get_direction (current_x, current_y, x, y);

  while (dir != 0)
    {
      MetaBarrierEvent *event;
      gboolean new_sequence;

      nearest = barrier_find_nearest (manager, dir, current_x, current_y, x, y);
      if (!nearest)
        break;

      new_sequence = !nearest->priv->hit;

      nearest->priv->seen = TRUE;
      nearest->priv->hit = TRUE;

      if (nearest->priv->barrier_event_id == nearest->priv->release_event_id)
        continue;

      barrier_clamp_to_barrier (nearest, dir, &x, &y);

      if (barrier_is_vertical (nearest))
        {
          dir &= ~(META_BARRIER_DIRECTION_NEGATIVE_X | META_BARRIER_DIRECTION_POSITIVE_X);
          current_x = x;
        }
      else if (barrier_is_horizontal (nearest))
        {
          dir &= ~(META_BARRIER_DIRECTION_NEGATIVE_Y | META_BARRIER_DIRECTION_POSITIVE_Y);
          current_y = y;
        }

      event = g_slice_new0 (MetaBarrierEvent);

      event->ref_count = 1;
      event->barrier = g_object_ref (nearest);
      event->event_id = nearest->priv->barrier_event_id;
      event->time = time;
      event->dt = new_sequence ? 0 : time - nearest->priv->last_timestamp;
      event->x = current_x;
      event->y = current_y;
      event->dx = dx;
      event->dy = dy;
      event->released = FALSE;
      event->grabbed = FALSE;

      g_idle_add (emit_hit_event, event);
    }

  for (iter = manager->barriers; iter; iter = iter->next)
    {
      MetaBarrierEvent *event;
      MetaBarrier *barrier = iter->data;

      if (!barrier->priv->active)
        continue;

      barrier->priv->seen = FALSE;
      if (!barrier->priv->hit)
        continue;

      if (barrier_inside_hit_box (barrier, x, y))
        continue;

      barrier->priv->hit = FALSE;

      event = g_slice_new0 (MetaBarrierEvent);

      event->ref_count = 1;
      event->barrier = g_object_ref (barrier);
      event->event_id = barrier->priv->barrier_event_id;
      event->time = time;
      event->dt = time - barrier->priv->last_timestamp;
      event->x = current_x;
      event->y = current_y;
      event->dx = dx;
      event->dy = dy;
      event->released = barrier->priv->barrier_event_id == barrier->priv->release_event_id;
      event->grabbed = FALSE;

      g_idle_add (emit_left_event, event);

      /* If we've left the hit box, this is the
       * start of a new event ID. */
      barrier->priv->barrier_event_id++;
    }

  *new_x = x;
  *new_y = y;
}

MetaBarrierManager *
meta_barrier_manager_get (void)
{
  if (!global_barrier_manager)
    global_barrier_manager = g_new0 (MetaBarrierManager, 1);

  return global_barrier_manager;
}

static MetaBarrierEvent *
meta_barrier_event_ref (MetaBarrierEvent *event)
{
  g_return_val_if_fail (event != NULL, NULL);
  g_return_val_if_fail (event->ref_count > 0, NULL);

  g_atomic_int_inc ((volatile int *)&event->ref_count);
  return event;
}

static void
meta_barrier_event_unref (MetaBarrierEvent *event)
{
  g_return_if_fail (event != NULL);
  g_return_if_fail (event->ref_count > 0);

  if (g_atomic_int_dec_and_test ((volatile int *)&event->ref_count))
    {
      if (event->barrier)
        g_object_unref (event->barrier);

      g_slice_free (MetaBarrierEvent, event);
    }
}

G_DEFINE_BOXED_TYPE (MetaBarrierEvent,
                     meta_barrier_event,
                     meta_barrier_event_ref,
                     meta_barrier_event_unref)
