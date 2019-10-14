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
#include "clutter-input-pointer-a11y-private.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-seat.h"

enum
{
  DEVICE_ADDED,
  DEVICE_REMOVED,
  TOOL_CHANGED,
  KBD_A11Y_MASK_CHANGED,
  KBD_A11Y_FLAGS_CHANGED,
  PTR_A11Y_DWELL_CLICK_TYPE_CHANGED,
  PTR_A11Y_TIMEOUT_STARTED,
  PTR_A11Y_TIMEOUT_STOPPED,
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

  /* Keyboard a11y */
  ClutterKbdA11ySettings kbd_a11y_settings;

  /* Pointer a11y */
  ClutterPointerA11ySettings pointer_a11y_settings;
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

  /**
   * ClutterSeat::kbd-a11y-mods-state-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @latched_mask: the latched modifier mask from stickykeys
   * @locked_mask:  the locked modifier mask from stickykeys
   *
   * The ::kbd-a11y-mods-state-changed signal is emitted each time either the
   * latched modifiers mask or locked modifiers mask are changed as the
   * result of keyboard accessibilty's sticky keys operations.
   */
  signals[KBD_A11Y_MASK_CHANGED] =
    g_signal_new (I_("kbd-a11y-mods-state-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[KBD_A11Y_MASK_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__UINT_UINTv);

  /**
   * ClutterSeat::kbd-a11y-flags-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @settings_flags: the new ClutterKeyboardA11yFlags configuration
   * @changed_mask: the ClutterKeyboardA11yFlags changed
   *
   * The ::kbd-a11y-flags-changed signal is emitted each time the
   * ClutterKeyboardA11yFlags configuration is changed as the result of
   * keyboard accessibility operations.
   */
  signals[KBD_A11Y_FLAGS_CHANGED] =
    g_signal_new (I_("kbd-a11y-flags-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[KBD_A11Y_FLAGS_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__UINT_UINTv);

  /**
   * ClutterSeat::ptr-a11y-dwell-click-type-changed:
   * @seat: the #ClutterSeat that emitted the signal
   * @click_type: the new #ClutterPointerA11yDwellClickType mode
   *
   * The ::ptr-a11y-dwell-click-type-changed signal is emitted each time
   * the ClutterPointerA11yDwellClickType mode is changed as the result
   * of pointer accessibility operations.
   */
  signals[PTR_A11Y_DWELL_CLICK_TYPE_CHANGED] =
    g_signal_new (I_("ptr-a11y-dwell-click-type-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__FLAGS,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_POINTER_A11Y_DWELL_CLICK_TYPE);
  g_signal_set_va_marshaller (signals[PTR_A11Y_DWELL_CLICK_TYPE_CHANGED],
                              G_TYPE_FROM_CLASS (object_class),
                              g_cclosure_marshal_VOID__FLAGSv);

  /**
   * ClutterSeat::ptr-a11y-timeout-started:
   * @seat: the #ClutterSeat that emitted the signal
   * @device: the core pointer #ClutterInputDevice
   * @timeout_type: the type of timeout #ClutterPointerA11yTimeoutType
   * @delay: the delay in ms before secondary-click is triggered.
   *
   * The ::ptr-a11y-timeout-started signal is emitted when a
   * pointer accessibility timeout delay is started, so that upper
   * layers can notify the user with some visual feedback.
   */
  signals[PTR_A11Y_TIMEOUT_STARTED] =
    g_signal_new (I_("ptr-a11y-timeout-started"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_FLAGS_UINT,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  CLUTTER_TYPE_POINTER_A11Y_TIMEOUT_TYPE,
                  G_TYPE_UINT);
  g_signal_set_va_marshaller (signals[PTR_A11Y_TIMEOUT_STARTED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__OBJECT_FLAGS_UINTv);

  /**
   * ClutterSeat::ptr-a11y-timeout-stopped:
   * @seat: the #ClutterSeat that emitted the signal
   * @device: the core pointer #ClutterInputDevice
   * @timeout_type: the type of timeout #ClutterPointerA11yTimeoutType
   * @clicked: %TRUE if the timeout finished and triggered a click
   *
   * The ::ptr-a11y-timeout-stopped signal is emitted when a running
   * pointer accessibility timeout delay is stopped, either because
   * it's triggered at the end of the delay or cancelled, so that
   * upper layers can notify the user with some visual feedback.
   */
  signals[PTR_A11Y_TIMEOUT_STOPPED] =
    g_signal_new (I_("ptr-a11y-timeout-stopped"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  _clutter_marshal_VOID__OBJECT_FLAGS_BOOLEAN,
                  G_TYPE_NONE, 3,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  CLUTTER_TYPE_POINTER_A11Y_TIMEOUT_TYPE,
                  G_TYPE_BOOLEAN);
  g_signal_set_va_marshaller (signals[PTR_A11Y_TIMEOUT_STOPPED],
                              G_TYPE_FROM_CLASS (object_class),
                              _clutter_marshal_VOID__OBJECT_FLAGS_BOOLEANv);

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

/**
 * clutter_seat_get_keymap:
 * @seat: a #ClutterSeat
 *
 * Returns the seat keymap
 *
 * Returns: (transfer none): the seat keymap
 **/
ClutterKeymap *
clutter_seat_get_keymap (ClutterSeat *seat)
{
  return CLUTTER_SEAT_GET_CLASS (seat)->get_keymap (seat);
}

static gboolean
are_kbd_a11y_settings_equal (ClutterKbdA11ySettings *a,
                             ClutterKbdA11ySettings *b)
{
  return (memcmp (a, b, sizeof (ClutterKbdA11ySettings)) == 0);
}

void
clutter_seat_set_kbd_a11y_settings (ClutterSeat            *seat,
                                    ClutterKbdA11ySettings *settings)
{
  ClutterSeatClass *seat_class;
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  if (are_kbd_a11y_settings_equal (&priv->kbd_a11y_settings, settings))
    return;

  priv->kbd_a11y_settings = *settings;

  seat_class = CLUTTER_SEAT_GET_CLASS (seat);
  if (seat_class->apply_kbd_a11y_settings)
    seat_class->apply_kbd_a11y_settings (seat, settings);
}

void
clutter_seat_get_kbd_a11y_settings (ClutterSeat            *seat,
                                    ClutterKbdA11ySettings *settings)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  *settings = priv->kbd_a11y_settings;
}

void
clutter_seat_ensure_a11y_state (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  if (core_pointer)
    {
      if (_clutter_is_input_pointer_a11y_enabled (core_pointer))
        _clutter_input_pointer_a11y_add_device (core_pointer);
    }
}

static gboolean
are_pointer_a11y_settings_equal (ClutterPointerA11ySettings *a,
                                 ClutterPointerA11ySettings *b)
{
  return (memcmp (a, b, sizeof (ClutterPointerA11ySettings)) == 0);
}

static void
clutter_seat_enable_pointer_a11y (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  _clutter_input_pointer_a11y_add_device (core_pointer);
}

static void
clutter_seat_disable_pointer_a11y (ClutterSeat *seat)
{
  ClutterInputDevice *core_pointer;

  core_pointer = clutter_seat_get_pointer (seat);

  _clutter_input_pointer_a11y_remove_device (core_pointer);
}

/**
 * clutter_seat_set_pointer_a11y_settings:
 * @seat: a #ClutterSeat
 * @settings: a pointer to a #ClutterPointerA11ySettings
 *
 * Sets the pointer accessibility settings
 **/
void
clutter_seat_set_pointer_a11y_settings (ClutterSeat                *seat,
                                        ClutterPointerA11ySettings *settings)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  if (are_pointer_a11y_settings_equal (&priv->pointer_a11y_settings, settings))
    return;

  if (priv->pointer_a11y_settings.controls == 0 && settings->controls != 0)
    clutter_seat_enable_pointer_a11y (seat);
  else if (priv->pointer_a11y_settings.controls != 0 && settings->controls == 0)
    clutter_seat_disable_pointer_a11y (seat);

  priv->pointer_a11y_settings = *settings;
}

/**
 * clutter_seat_get_pointer_a11y_settings:
 * @seat: a #ClutterSeat
 * @settings: a pointer to a #ClutterPointerA11ySettings
 *
 * Gets the current pointer accessibility settings
 **/
void
clutter_seat_get_pointer_a11y_settings (ClutterSeat                *seat,
                                        ClutterPointerA11ySettings *settings)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  *settings = priv->pointer_a11y_settings;
}

/**
 * clutter_seat_set_pointer_a11y_dwell_click_type:
 * @seat: a #ClutterSeat
 * @click_type: type of click as #ClutterPointerA11yDwellClickType
 *
 * Sets the dwell click type
 **/
void
clutter_seat_set_pointer_a11y_dwell_click_type (ClutterSeat                      *seat,
                                                ClutterPointerA11yDwellClickType  click_type)
{
  ClutterSeatPrivate *priv = clutter_seat_get_instance_private (seat);

  g_return_if_fail (CLUTTER_IS_SEAT (seat));

  priv->pointer_a11y_settings.dwell_click_type = click_type;
}
