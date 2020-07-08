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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

/**
 * SECTION:meta-pointer-confinement-wayland
 * @title: MetaPointerConfinementWayland
 * @short_description: A #MetaPointerConstraint implementing pointer confinement
 *
 * A MetaPointerConfinementConstraint implements the client pointer constraint
 * "pointer confinement": the cursor should not be able to "break out" of a
 * certain area defined by the client requesting it.
 */

#include "config.h"

#include "wayland/meta-pointer-confinement-wayland.h"

#include <glib-object.h>
#include <cairo.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-pointer-constraint.h"
#include "wayland/meta-wayland-pointer-constraints.h"
#include "wayland/meta-wayland-pointer.h"
#include "wayland/meta-wayland-seat.h"
#include "wayland/meta-wayland-surface.h"

typedef struct _MetaPointerConfinementWaylandPrivate MetaPointerConfinementWaylandPrivate;

struct _MetaPointerConfinementWaylandPrivate
{
  MetaWaylandPointerConstraint *constraint;
  gboolean enabled;
};

enum
{
  PROP_0,
  PROP_WAYLAND_POINTER_CONSTRAINT,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (MetaPointerConfinementWayland,
                            meta_pointer_confinement_wayland,
                            G_TYPE_OBJECT)

static void
meta_pointer_confinement_wayland_update (MetaPointerConfinementWayland *self)
{
  MetaPointerConstraint *constraint;

  constraint =
    META_POINTER_CONFINEMENT_WAYLAND_GET_CLASS (self)->create_constraint (self);
  meta_backend_set_client_pointer_constraint (meta_get_backend (), constraint);
  g_object_unref (constraint);
}

static void
surface_geometry_changed (MetaWaylandSurface            *surface,
                          MetaPointerConfinementWayland *self)
{
  meta_pointer_confinement_wayland_update (self);
}

static void
window_position_changed (MetaWindow                    *window,
                         MetaPointerConfinementWayland *self)
{
  meta_pointer_confinement_wayland_update (self);
}

void
meta_pointer_confinement_wayland_enable (MetaPointerConfinementWayland *confinement)
{
  MetaPointerConfinementWaylandPrivate *priv;
  MetaWaylandPointerConstraint *constraint;
  MetaWaylandSurface *surface;
  MetaWindow *window;

  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);
  g_assert (!priv->enabled);

  priv->enabled = TRUE;
  constraint = priv->constraint;

  surface = meta_wayland_pointer_constraint_get_surface (constraint);
  g_signal_connect_object (surface,
                           "geometry-changed",
                           G_CALLBACK (surface_geometry_changed),
                           confinement,
                           0);

  window = meta_wayland_surface_get_window (surface);
  if (window)
    {
      g_signal_connect_object (window,
                               "position-changed",
                               G_CALLBACK (window_position_changed),
                               confinement,
                               0);
    }

  meta_pointer_confinement_wayland_update (confinement);
}

void
meta_pointer_confinement_wayland_disable (MetaPointerConfinementWayland *confinement)
{
  MetaPointerConfinementWaylandPrivate *priv;
  MetaWaylandPointerConstraint *constraint;
  MetaWaylandSurface *surface;
  MetaWindow *window;

  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);
  constraint = priv->constraint;
  g_assert (priv->enabled);

  priv->enabled = FALSE;
  surface = meta_wayland_pointer_constraint_get_surface (constraint);
  g_signal_handlers_disconnect_by_func (surface, surface_geometry_changed,
                                        confinement);

  window = meta_wayland_surface_get_window (surface);
  if (window)
    {
      g_signal_handlers_disconnect_by_func (window, window_position_changed,
                                            confinement);
    }

  meta_backend_set_client_pointer_constraint (meta_get_backend (), NULL);
}

static void
meta_pointer_confinement_wayland_init (MetaPointerConfinementWayland *confinement_wayland)
{
}

static void
meta_pointer_confinement_wayland_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  MetaPointerConfinementWayland *confinement;
  MetaPointerConfinementWaylandPrivate *priv;

  confinement = META_POINTER_CONFINEMENT_WAYLAND (object);
  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);

  switch (prop_id)
    {
    case PROP_WAYLAND_POINTER_CONSTRAINT:
      g_value_set_object (value, priv->constraint);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_pointer_confinement_wayland_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  MetaPointerConfinementWayland *confinement;
  MetaPointerConfinementWaylandPrivate *priv;

  confinement = META_POINTER_CONFINEMENT_WAYLAND (object);
  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);

  switch (prop_id)
    {
    case PROP_WAYLAND_POINTER_CONSTRAINT:
      priv->constraint = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static MetaPointerConstraint *
meta_pointer_confinement_wayland_create_constraint (MetaPointerConfinementWayland *confinement)
{
  MetaPointerConfinementWaylandPrivate *priv;
  MetaPointerConstraint *constraint;
  MetaWaylandSurface *surface;
  cairo_region_t *region;
  float dx, dy;

  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);

  surface = meta_wayland_pointer_constraint_get_surface (priv->constraint);
  region =
    meta_wayland_pointer_constraint_calculate_effective_region (priv->constraint);

  meta_wayland_surface_get_absolute_coordinates (surface, 0, 0, &dx, &dy);
  cairo_region_translate (region, dx, dy);

  constraint = meta_pointer_constraint_new (region);
  cairo_region_destroy (region);

  return constraint;
}

static void
meta_pointer_confinement_wayland_class_init (MetaPointerConfinementWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_pointer_confinement_wayland_set_property;
  object_class->get_property = meta_pointer_confinement_wayland_get_property;

  klass->create_constraint = meta_pointer_confinement_wayland_create_constraint;

  props[PROP_WAYLAND_POINTER_CONSTRAINT] =
    g_param_spec_object ("wayland-pointer-constraint",
                         "Wayland pointer constraint",
                         "Wayland pointer constraint",
                         META_TYPE_WAYLAND_POINTER_CONSTRAINT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, props);
}

MetaPointerConfinementWayland *
meta_pointer_confinement_wayland_new (MetaWaylandPointerConstraint *constraint)
{
  return g_object_new (META_TYPE_POINTER_CONFINEMENT_WAYLAND,
                       "wayland-pointer-constraint", constraint,
                       NULL);
}

MetaWaylandPointerConstraint *
meta_pointer_confinement_wayland_get_wayland_pointer_constraint (MetaPointerConfinementWayland *confinement)
{
  MetaPointerConfinementWaylandPrivate *priv;

  priv = meta_pointer_confinement_wayland_get_instance_private (confinement);
  return priv->constraint;
}
