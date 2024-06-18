/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014-2015 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * MetaBarrierImplX11:
 *
 * Pointer barriers implementation for X11
 */

#include "config.h"

#include <glib-object.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>

#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-barrier-x11.h"
#include "core/display-private.h"
#include "meta/barrier.h"
#include "x11/meta-x11-display-private.h"

struct _MetaX11Barriers
{
  GHashTable *barriers;
};

struct _MetaBarrierImplX11
{
  MetaBarrierImpl parent;

  MetaBarrier *barrier;
  PointerBarrier xbarrier;
};

G_DEFINE_TYPE (MetaBarrierImplX11,
               meta_barrier_impl_x11,
               META_TYPE_BARRIER_IMPL)

static gboolean
meta_barrier_impl_x11_is_active (MetaBarrierImpl *impl)
{
  MetaBarrierImplX11 *self = META_BARRIER_IMPL_X11 (impl);

  return self->xbarrier != 0;
}

static void
meta_barrier_impl_x11_release (MetaBarrierImpl  *impl,
                               MetaBarrierEvent *event)
{
  MetaBarrierImplX11 *self = META_BARRIER_IMPL_X11 (impl);
  MetaBackend *backend = meta_barrier_get_backend (self->barrier);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  if (!event)
    {
      g_warning ("X11 barriers always need barrier events to release");
      return;
    }

  XIBarrierReleasePointer (xdisplay,
                           META_VIRTUAL_CORE_POINTER_ID,
                           self->xbarrier, event->event_id);
}

static void
meta_barrier_impl_x11_destroy (MetaBarrierImpl *impl)
{
  MetaBarrierImplX11 *self = META_BARRIER_IMPL_X11 (impl);
  MetaBackend *backend = meta_barrier_get_backend (self->barrier);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  MetaX11Barriers *barriers = meta_backend_x11_get_barriers (backend_x11);
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  if (!meta_barrier_is_active (self->barrier))
    return;

  XFixesDestroyPointerBarrier (xdisplay, self->xbarrier);
  g_hash_table_remove (barriers->barriers, &self->xbarrier);
  self->xbarrier = 0;
}

MetaBarrierImpl *
meta_barrier_impl_x11_new (MetaBarrier *barrier)
{
  MetaBarrierImplX11 *self;
  MetaBackend *backend;
  MetaBackendX11 *backend_x11;
  MetaX11Barriers *barriers;
  Display *xdisplay;
  Window root;
  MetaBorder *border;
  unsigned int allowed_motion_dirs;

  self = g_object_new (META_TYPE_BARRIER_IMPL_X11, NULL);
  self->barrier = barrier;

  backend = meta_barrier_get_backend (self->barrier);
  backend_x11 = META_BACKEND_X11 (backend);
  xdisplay = meta_backend_x11_get_xdisplay (backend_x11);
  root = DefaultRootWindow (xdisplay);

  border = meta_barrier_get_border (barrier);
  allowed_motion_dirs = meta_border_get_allows_directions (border);
  self->xbarrier = XFixesCreatePointerBarrier (xdisplay, root,
                                               (int) border->line.a.x,
                                               (int) border->line.a.y,
                                               (int) border->line.b.x,
                                               (int) border->line.b.y,
                                               allowed_motion_dirs,
                                               0, NULL);

  barriers = meta_backend_x11_get_barriers (backend_x11);
  g_hash_table_insert (barriers->barriers, &self->xbarrier, barrier);

  return META_BARRIER_IMPL (self);
}

static void
meta_barrier_fire_xevent (MetaBarrier    *barrier,
                          XIBarrierEvent *xevent)
{
  MetaBarrierEvent *event = g_new0 (MetaBarrierEvent, 1);

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
      meta_barrier_emit_hit_signal (barrier, event);
      break;
    case XI_BarrierLeave:
      meta_barrier_emit_left_signal (barrier, event);
      break;
    default:
      g_assert_not_reached ();
    }

  meta_barrier_event_unref (event);
}

gboolean
meta_x11_barriers_process_xevent (MetaX11Barriers *barriers,
                                  XIEvent         *event)
{
  MetaBarrier *barrier;
  XIBarrierEvent *xev;

  switch (event->evtype)
    {
    case XI_BarrierHit:
    case XI_BarrierLeave:
      break;
    default:
      return FALSE;
    }

  xev = (XIBarrierEvent *) event;
  barrier = g_hash_table_lookup (barriers->barriers, &xev->barrier);
  if (barrier)
    {
      meta_barrier_fire_xevent (barrier, xev);
      return TRUE;
    }

  return FALSE;
}

static void
meta_barrier_impl_x11_class_init (MetaBarrierImplX11Class *klass)
{
  MetaBarrierImplClass *impl_class = META_BARRIER_IMPL_CLASS (klass);

  impl_class->is_active = meta_barrier_impl_x11_is_active;
  impl_class->release = meta_barrier_impl_x11_release;
  impl_class->destroy = meta_barrier_impl_x11_destroy;
}

static void
meta_barrier_impl_x11_init (MetaBarrierImplX11 *self)
{
}

MetaX11Barriers *
meta_x11_barriers_new (MetaBackendX11 *backend_x11)
{
  MetaX11Barriers *x11_barriers;

  x11_barriers = g_new0 (MetaX11Barriers, 1);
  x11_barriers->barriers = g_hash_table_new (meta_unsigned_long_hash,
                                             meta_unsigned_long_equal);

  return x11_barriers;
}

void
meta_x11_barriers_free (MetaX11Barriers *x11_barriers)
{
  g_assert (g_hash_table_size (x11_barriers->barriers) == 0);
  g_hash_table_unref (x11_barriers->barriers);
  g_free (x11_barriers);
}
