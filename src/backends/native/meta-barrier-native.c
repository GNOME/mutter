/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * MetaBarrierImplNative:
 *
 * Pointer barriers implementation for the native backend
 */

#include "config.h"

#include "backends/native/meta-barrier-native.h"

#include <stdlib.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-barrier-private.h"
#include "backends/native/meta-seat-native.h"
#include "meta/barrier.h"
#include "meta/util.h"

struct _MetaBarrierManagerNative
{
  GHashTable *barriers;
  GMutex mutex;
  MetaBarrierImplNative *pointer_trap;
};

typedef enum
{
  /* The barrier is active and responsive to pointer motion. */
  META_BARRIER_STATE_ACTIVE,

  /* An intermediate state after a pointer hit the pointer barrier. */
  META_BARRIER_STATE_HIT,

  /* The barrier was hit by a pointer and is still within the hit box and
   * has not been released.*/
  META_BARRIER_STATE_HELD,

  /* The pointer was released by the user. If the following motion hits
   * the barrier, it will pass through. */
  META_BARRIER_STATE_RELEASE,

  /* An intermediate state when the pointer has left the barrier. */
  META_BARRIER_STATE_LEFT,
} MetaBarrierState;

struct _MetaBarrierImplNative
{
  MetaBarrierImpl parent;

  MetaBarrier              *barrier;
  MetaBarrierManagerNative *manager;

  gboolean                  is_active;
  MetaBarrierState          state;
  int                       trigger_serial;
  guint32                   last_event_time;
  MetaBarrierDirection      blocked_dir;
  GMainContext             *main_context;
};

G_DEFINE_TYPE (MetaBarrierImplNative,
               meta_barrier_impl_native,
               META_TYPE_BARRIER_IMPL)

static int
next_serial (void)
{
  static int barrier_serial = 1;

  barrier_serial++;

  /* If it wraps, avoid 0 as it's not a valid serial. */
  if (barrier_serial == 0)
    barrier_serial++;

  return barrier_serial;
}

static gboolean
is_barrier_horizontal (MetaBarrier *barrier)
{
  MetaBorder *border = meta_barrier_get_border (barrier);

  return meta_border_is_horizontal (border);
}

static gboolean
is_barrier_blocking_directions (MetaBarrier         *barrier,
                                MetaBarrierDirection directions)
{
  MetaBorder *border = meta_barrier_get_border (barrier);
  MetaBorderMotionDirection border_motion_directions =
    (MetaBorderMotionDirection) directions;

  return meta_border_is_blocking_directions (border, border_motion_directions);
}

static void
dismiss_pointer (MetaBarrierImplNative *self)
{
  self->state = META_BARRIER_STATE_LEFT;
}

/*
 * Calculate the hit box for a held motion. The hit box is a 2 px wide region
 * in the opposite direction of every direction the barrier blocks. The purpose
 * of this is to allow small movements without receiving a "left" signal. This
 * heuristic comes from the X.org pointer barrier implementation.
 */
static MetaLine2
calculate_barrier_hit_box (MetaBarrier *barrier)
{
  MetaBorder *border = meta_barrier_get_border (barrier);
  MetaLine2 hit_box = border->line;

  if (is_barrier_horizontal (barrier))
    {
      if (is_barrier_blocking_directions (barrier,
                                          META_BARRIER_DIRECTION_POSITIVE_Y))
        hit_box.a.y -= 2.0f;
      if (is_barrier_blocking_directions (barrier,
                                          META_BARRIER_DIRECTION_NEGATIVE_Y))
        hit_box.b.y += 2.0f;
    }
  else
    {
      if (is_barrier_blocking_directions (barrier,
                                          META_BARRIER_DIRECTION_POSITIVE_X))
        hit_box.a.x -= 2.0f;
      if (is_barrier_blocking_directions (barrier,
                                          META_BARRIER_DIRECTION_NEGATIVE_X))
        hit_box.b.x += 2.0f;
    }

  return hit_box;
}

static gboolean
is_within_box (MetaLine2   box,
               MetaVector2 point)
{
  return (point.x >= box.a.x && point.x < box.b.x &&
          point.y >= box.a.y && point.y < box.b.y);
}

static void
maybe_release_barrier (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  MetaBarrierImplNative *self = key;
  MetaBarrier *barrier = self->barrier;
  MetaBorder *border = meta_barrier_get_border (barrier);
  MetaLine2 *motion = user_data;
  MetaLine2 hit_box;

  if (self->state != META_BARRIER_STATE_HELD)
    return;

  /* Release if we end up outside barrier end points. */
  if (is_barrier_horizontal (barrier))
    {
      if (motion->b.x > MAX (border->line.a.x, border->line.b.x) ||
          motion->b.x < MIN (border->line.a.x, border->line.b.x))
        {
          dismiss_pointer (self);
          return;
        }
    }
  else
    {
      if (motion->b.y > MAX (border->line.a.y, border->line.b.y) ||
          motion->b.y < MIN (border->line.a.y, border->line.b.y))
        {
          dismiss_pointer (self);
          return;
        }
    }

  /* Release if we don't intersect and end up outside of hit box. */
  hit_box = calculate_barrier_hit_box (barrier);
  if (!is_within_box (hit_box, motion->b))
    {
      dismiss_pointer (self);
      return;
    }
}

static void
maybe_release_barriers (MetaBarrierManagerNative *manager,
                        graphene_point_t          prev,
                        graphene_point_t          cur)
{
  MetaLine2 motion = {
    .a = {
      .x = prev.x,
      .y = prev.y,
    },
    .b = {
      .x = cur.x,
      .y = cur.y,
    },
  };

  g_hash_table_foreach (manager->barriers,
                        maybe_release_barrier,
                        &motion);
}

typedef struct _MetaClosestBarrierData
{
  struct
  {
    MetaLine2                   motion;
    MetaBarrierDirection        directions;
  } in;

  struct
  {
    float                       closest_distance_2;
    MetaBarrierImplNative      *barrier_impl;
  } out;
} MetaClosestBarrierData;

static void
update_closest_barrier (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
  MetaBarrierImplNative *self = key;
  MetaBarrier *barrier = self->barrier;
  MetaBorder *border = meta_barrier_get_border (barrier);
  MetaClosestBarrierData *data = user_data;
  MetaVector2 intersection;
  float dx, dy;
  float distance_2;

  /* Ignore if the barrier is not blocking in any of the motions directions. */
  if (!is_barrier_blocking_directions (barrier, data->in.directions))
    return;

  /* Ignore if the barrier released the pointer. */
  if (self->state == META_BARRIER_STATE_RELEASE)
    return;

  /* Ignore if we are moving away from barrier. */
  if (self->state == META_BARRIER_STATE_HELD &&
      (data->in.directions & self->blocked_dir) == 0)
    return;

  /* Check if the motion intersects with the barrier, and retrieve the
   * intersection point if any. */
  if (!meta_line2_intersects_with (&border->line,
                                   &data->in.motion,
                                   &intersection))
    return;

  /* Calculate the distance to the barrier and keep track of the closest
   * barrier. */
  dx = intersection.x - data->in.motion.a.x;
  dy = intersection.y - data->in.motion.a.y;
  distance_2 = dx*dx + dy*dy;
  if (data->out.barrier_impl == NULL ||
      distance_2 < data->out.closest_distance_2)
    {
      data->out.barrier_impl = self;
      data->out.closest_distance_2 = distance_2;
    }
}

static gboolean
get_closest_barrier (MetaBarrierManagerNative *manager,
                     float                     prev_x,
                     float                     prev_y,
                     float                     x,
                     float                     y,
                     MetaBarrierDirection      motion_dir,
                     MetaBarrierImplNative   **barrier_impl)
{
  MetaClosestBarrierData closest_barrier_data;

  closest_barrier_data = (MetaClosestBarrierData) {
    .in = {
      .motion = {
        .a = {
          .x = prev_x,
          .y = prev_y,
        },
        .b = {
          .x = x,
          .y = y,
        },
      },
      .directions = motion_dir,
    },
  };

  g_hash_table_foreach (manager->barriers,
                        update_closest_barrier,
                        &closest_barrier_data);

  if (closest_barrier_data.out.barrier_impl != NULL)
    {
      *barrier_impl = closest_barrier_data.out.barrier_impl;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

typedef struct _MetaBarrierEventData
{
  guint32             time;
  float               prev_x;
  float               prev_y;
  float               x;
  float               y;
  float               dx;
  float               dy;
} MetaBarrierEventData;

typedef struct
{
  MetaBarrierEvent *event;
  MetaBarrier *barrier;
  MetaBarrierState state;
} MetaBarrierIdleData;

static gboolean
emit_event_idle (MetaBarrierIdleData *idle_data)
{
  if (idle_data->state == META_BARRIER_STATE_HELD)
    meta_barrier_emit_hit_signal (idle_data->barrier, idle_data->event);
  else
    meta_barrier_emit_left_signal (idle_data->barrier, idle_data->event);

  meta_barrier_event_unref (idle_data->event);

  return G_SOURCE_REMOVE;
}

static void
queue_event (MetaBarrierImplNative *self,
             MetaBarrierEvent      *event)
{
  MetaBarrierIdleData *idle_data;
  GSource *source;

  idle_data = g_new0 (MetaBarrierIdleData, 1);
  idle_data->state = self->state;
  idle_data->barrier = self->barrier;
  idle_data->event = event;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source,
                         (GSourceFunc) emit_event_idle,
                         idle_data,
                         g_free);

  g_source_attach (source, self->main_context);
  g_source_unref (source);
}

static void
emit_barrier_event (MetaBarrierImplNative *self,
                    guint32                time,
                    float                  prev_x,
                    float                  prev_y,
                    float                  x,
                    float                  y,
                    float                  dx,
                    float                  dy)
{
  MetaBarrierEvent *event = g_new0 (MetaBarrierEvent, 1);
  MetaBarrierState old_state = self->state;

  switch (self->state)
    {
    case META_BARRIER_STATE_HIT:
      self->state = META_BARRIER_STATE_HELD;
      self->trigger_serial = next_serial ();
      event->dt = 0;

      break;
    case META_BARRIER_STATE_RELEASE:
    case META_BARRIER_STATE_LEFT:
      self->state = META_BARRIER_STATE_ACTIVE;

      G_GNUC_FALLTHROUGH;
    case META_BARRIER_STATE_HELD:
      event->dt = time - self->last_event_time;

      break;
    case META_BARRIER_STATE_ACTIVE:
      g_assert_not_reached (); /* Invalid state. */
    }

  event->ref_count = 1;
  event->event_id = self->trigger_serial;
  event->time = time;

  event->x = x;
  event->y = y;
  event->dx = dx;
  event->dy = dy;

  event->grabbed = self->state == META_BARRIER_STATE_HELD;
  event->released = old_state == META_BARRIER_STATE_RELEASE;

  self->last_event_time = time;

  queue_event (self, event);
}

static void
maybe_emit_barrier_event (gpointer key, gpointer value, gpointer user_data)
{
  MetaBarrierImplNative *self = key;
  MetaBarrierEventData *data = user_data;

  switch (self->state)
    {
    case META_BARRIER_STATE_ACTIVE:
      break;
    case META_BARRIER_STATE_HIT:
    case META_BARRIER_STATE_HELD:
    case META_BARRIER_STATE_RELEASE:
    case META_BARRIER_STATE_LEFT:
      emit_barrier_event (self,
                          data->time,
                          data->prev_x,
                          data->prev_y,
                          data->x,
                          data->y,
                          data->dx,
                          data->dy);
      break;
    }
}

/* Clamp (x, y) to the barrier and remove clamped direction from motion_dir. */
static void
clamp_to_barrier (MetaBarrierImplNative *self,
                  MetaBarrierDirection  *motion_dir,
                  graphene_point_t      *pos)
{
  MetaBarrier *barrier = self->barrier;
  MetaBorder *border = meta_barrier_get_border (barrier);

  if (is_barrier_horizontal (barrier))
    {
      if (*motion_dir & META_BARRIER_DIRECTION_POSITIVE_Y)
        pos->y = border->line.a.y;
      else if (*motion_dir & META_BARRIER_DIRECTION_NEGATIVE_Y)
        pos->y = border->line.a.y;

      self->blocked_dir = *motion_dir & (META_BARRIER_DIRECTION_POSITIVE_Y |
                                         META_BARRIER_DIRECTION_NEGATIVE_Y);
      *motion_dir &= ~(META_BARRIER_DIRECTION_POSITIVE_Y |
                       META_BARRIER_DIRECTION_NEGATIVE_Y);
    }
  else
    {
      if (*motion_dir & META_BARRIER_DIRECTION_POSITIVE_X)
        pos->x = border->line.a.x;
      else if (*motion_dir & META_BARRIER_DIRECTION_NEGATIVE_X)
        pos->x = border->line.a.x;

      self->blocked_dir = *motion_dir & (META_BARRIER_DIRECTION_POSITIVE_X |
                                         META_BARRIER_DIRECTION_NEGATIVE_X);
      *motion_dir &= ~(META_BARRIER_DIRECTION_POSITIVE_X |
                       META_BARRIER_DIRECTION_NEGATIVE_X);
    }

  self->state = META_BARRIER_STATE_HIT;
}

static gboolean
stick_to_barrier (MetaBarrierImplNative *self,
                  MetaBarrierDirection   motion_dir,
                  graphene_point_t       prev,
                  graphene_point_t      *cur_inout)
{
  MetaLine2 motion = {
    .a = { .x = prev.x, .y = prev.y },
    .b = { .x = cur_inout->x, .y = cur_inout->y },
  };
  MetaBorder *border = meta_barrier_get_border (self->barrier);
  MetaVector2 intersection;

  if (meta_line2_intersects_with (&motion, &border->line,
                                  &intersection))
    {
      cur_inout->x = intersection.x;
      cur_inout->y = intersection.y;

      self->blocked_dir = motion_dir;
      self->state = META_BARRIER_STATE_HIT;
      self->manager->pointer_trap = self;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

void
meta_barrier_manager_native_process_in_impl (MetaBarrierManagerNative *manager,
                                             uint32_t                  time,
                                             graphene_point_t          prev,
                                             graphene_point_t         *new_inout)
{
  graphene_point_t orig = *new_inout;
  MetaBarrierDirection motion_dir = 0;
  MetaBarrierEventData barrier_event_data;
  MetaBarrierImplNative *barrier_impl;

  if (manager->pointer_trap)
    {
      *new_inout = prev;
      return;
    }

  g_mutex_lock (&manager->mutex);

  /* Get the direction of the motion vector. */
  if (prev.x < new_inout->x)
    motion_dir |= META_BARRIER_DIRECTION_POSITIVE_X;
  else if (prev.x > new_inout->x)
    motion_dir |= META_BARRIER_DIRECTION_NEGATIVE_X;
  if (prev.y < new_inout->y)
    motion_dir |= META_BARRIER_DIRECTION_POSITIVE_Y;
  else if (prev.y > new_inout->y)
    motion_dir |= META_BARRIER_DIRECTION_NEGATIVE_Y;

  /* Clamp to the closest barrier in any direction until either there are no
   * more barriers to clamp to or all directions have been clamped. */
  while (motion_dir != 0)
    {
      if (get_closest_barrier (manager,
                               prev.x, prev.y,
                               new_inout->x, new_inout->y,
                               motion_dir,
                               &barrier_impl))
        {
          MetaBarrier *barrier = barrier_impl->barrier;

          if (meta_barrier_get_flags (barrier) & META_BARRIER_FLAG_STICKY)
            {
              if (stick_to_barrier (barrier_impl, motion_dir,
                                    prev, new_inout))
                break;
            }

          clamp_to_barrier (barrier_impl, &motion_dir, new_inout);
        }
      else
        break;
    }

  /* Potentially release active barrier movements. */
  maybe_release_barriers (manager, prev, *new_inout);

  /* Initiate or continue barrier interaction. */
  barrier_event_data = (MetaBarrierEventData) {
    .time = time,
    .prev_x = prev.x,
    .prev_y = prev.y,
    .x = new_inout->x,
    .y = new_inout->y,
    .dx = orig.x - prev.x,
    .dy = orig.y - prev.y,
  };

  g_hash_table_foreach (manager->barriers,
                        maybe_emit_barrier_event,
                        &barrier_event_data);

  g_mutex_unlock (&manager->mutex);
}

static gboolean
meta_barrier_impl_native_is_active (MetaBarrierImpl *impl)
{
  MetaBarrierImplNative *self = META_BARRIER_IMPL_NATIVE (impl);

  return self->is_active;
}

static void
meta_barrier_impl_native_release (MetaBarrierImpl  *impl,
                                  MetaBarrierEvent *event)
{
  MetaBarrierImplNative *self = META_BARRIER_IMPL_NATIVE (impl);

  if (self->state == META_BARRIER_STATE_HELD &&
      (!event || event->event_id == self->trigger_serial))
    {
      self->state = META_BARRIER_STATE_RELEASE;
      self->manager->pointer_trap = NULL;
    }
}

static void
meta_barrier_impl_native_destroy (MetaBarrierImpl *impl)
{
  MetaBarrierImplNative *self = META_BARRIER_IMPL_NATIVE (impl);

  g_mutex_lock (&self->manager->mutex);
  if (self->manager->pointer_trap == self)
    self->manager->pointer_trap = NULL;
  g_hash_table_remove (self->manager->barriers, self);
  g_mutex_unlock (&self->manager->mutex);
  g_main_context_unref (self->main_context);
  self->is_active = FALSE;
}

MetaBarrierImpl *
meta_barrier_impl_native_new (MetaBarrier *barrier)
{
  MetaBackend *backend = meta_barrier_get_backend (barrier);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaBarrierImplNative *self;
  MetaBarrierManagerNative *manager;

  self = g_object_new (META_TYPE_BARRIER_IMPL_NATIVE, NULL);

  self->barrier = barrier;
  self->is_active = TRUE;
  self->main_context = g_main_context_ref_thread_default ();

  manager = meta_seat_native_get_barrier_manager (META_SEAT_NATIVE (seat));
  self->manager = manager;
  g_mutex_lock (&manager->mutex);
  g_hash_table_add (manager->barriers, self);
  g_mutex_unlock (&manager->mutex);

  return META_BARRIER_IMPL (self);
}

static void
meta_barrier_impl_native_class_init (MetaBarrierImplNativeClass *klass)
{
  MetaBarrierImplClass *impl_class = META_BARRIER_IMPL_CLASS (klass);

  impl_class->is_active = meta_barrier_impl_native_is_active;
  impl_class->release = meta_barrier_impl_native_release;
  impl_class->destroy = meta_barrier_impl_native_destroy;
}

static void
meta_barrier_impl_native_init (MetaBarrierImplNative *self)
{
}

MetaBarrierManagerNative *
meta_barrier_manager_native_new (void)
{
  MetaBarrierManagerNative *manager;

  manager = g_new0 (MetaBarrierManagerNative, 1);

  manager->barriers = g_hash_table_new (NULL, NULL);
  g_mutex_init (&manager->mutex);

  return manager;
}
