/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "clutter-build-config.h"

#include "clutter-backend-private.h"
#include "clutter-input-device-tool.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-seat.h"

enum
{
  DEVICE_ADDED,
  DEVICE_REMOVED,
  TOOL_CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0 };

enum
{
  PROP_0,
  PROP_BACKEND,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct _ClutterSeatPrivate ClutterSeatPrivate;

struct _ClutterSeatPrivate
{
  ClutterBackend *backend;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (ClutterSeat, clutter_seat, G_TYPE_OBJECT)

static void
clutter_seat_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  ClutterSeat *seat = CLUTTER_SEAT (object);
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_seat_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  ClutterSeat *seat = CLUTTER_SEAT (object);
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
clutter_seat_finalize (GObject *object)
{
  ClutterSeat *seat = CLUTTER_SEAT (object);
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_clear_object (&priv->backend);

  G_OBJECT_CLASS (clutter_seat_parent_class)->finalize (object);
}

static void
clutter_seat_class_init (ClutterSeatClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = clutter_seat_set_property;
  object_class->get_property = clutter_seat_get_property;
  object_class->finalize = clutter_seat_finalize;

  signals[DEVICE_ADDED] =
    g_signal_new (I_("device-added"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_DEVICE);
  g_signal_set_va_marshaller (signals[DEVICE_ADDED],
                              G_TYPE_FROM_CLASS (object_class),
                              g_cclosure_marshal_VOID__OBJECTv);

  signals[DEVICE_REMOVED] =
    g_signal_new (I_("device-removed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_INPUT_DEVICE);
  g_signal_set_va_marshaller (signals[DEVICE_REMOVED],
                              G_TYPE_FROM_CLASS (object_class),
                              g_cclosure_marshal_VOID__OBJECTv);
  signals[TOOL_CHANGED] =
    g_signal_new (I_("tool-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  CLUTTER_TYPE_INPUT_DEVICE_TOOL);
  g_signal_set_va_marshaller (signals[TOOL_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__OBJECT_OBJECTv);

  props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         P_("Backend"),
                         P_("Backend"),
                         CLUTTER_TYPE_BACKEND,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
clutter_seat_init (ClutterSeat *seat)
{
}

/**
 * clutter_seat_get_pointer:
 * @seat: a #ClutterSeat
 *
 * Returns the master pointer
 *
 * Returns: (transfer none): the master pointer
 **/
ClutterInputDevice *
clutter_seat_get_pointer (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->get_pointer (seat);
}

/**
 * clutter_seat_get_keyboard:
 * @seat: a #ClutterSeat
 *
 * Returns the master keyboard
 *
 * Returns: (transfer none): the master keyboard
 **/
ClutterInputDevice *
clutter_seat_get_keyboard (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->get_keyboard (seat);
}

/**
 * clutter_seat_list_devices:
 * @seat: a #ClutterSeat
 *
 * Returns the list of HW devices
 *
 * Returns: (transfer container) (element-type Clutter.InputDevice): A list of #ClutterInputDevice
 **/
GList *
clutter_seat_list_devices (ClutterSeat *seat)
{
  g_return_val_if_fail (CLUTTER_IS_SEAT (seat), NULL);

  return CLUTTER_SEAT_GET_CLASS (seat)->list_devices (seat);
}

void
clutter_seat_bell_notify (ClutterSeat *seat)
{
  CLUTTER_SEAT_GET_CLASS (seat)->bell_notify (seat);
}
