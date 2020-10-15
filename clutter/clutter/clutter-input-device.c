/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright © 2009, 2010, 2011  Intel Corp.
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

/**
 * SECTION:clutter-input-device
 * @short_description: An input device managed by Clutter
 *
 * #ClutterInputDevice represents an input device known to Clutter.
 *
 * The #ClutterInputDevice class holds the state of the device, but
 * its contents are usually defined by the Clutter backend in use.
 */

#include "clutter-build-config.h"

#include "clutter-input-device.h"

#include "clutter-actor-private.h"
#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-event-private.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"
#include "clutter-input-device-private.h"
#include "clutter-input-device-tool.h"

#include <math.h>

enum
{
  PROP_0,

  PROP_BACKEND,

  PROP_ID,
  PROP_NAME,

  PROP_DEVICE_TYPE,
  PROP_SEAT,
  PROP_DEVICE_MODE,

  PROP_HAS_CURSOR,

  PROP_N_AXES,

  PROP_VENDOR_ID,
  PROP_PRODUCT_ID,

  PROP_N_STRIPS,
  PROP_N_RINGS,
  PROP_N_MODE_GROUPS,
  PROP_DEVICE_NODE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE (ClutterInputDevice, clutter_input_device, G_TYPE_OBJECT);

static void
clutter_input_device_dispose (GObject *gobject)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (gobject);

  g_clear_pointer (&device->device_name, g_free);
  g_clear_pointer (&device->vendor_id, g_free);
  g_clear_pointer (&device->product_id, g_free);
  g_clear_pointer (&device->node_path, g_free);

  if (device->associated != NULL)
    {
      if (device->device_mode == CLUTTER_INPUT_MODE_PHYSICAL)
        _clutter_input_device_remove_physical_device (device->associated, device);

      _clutter_input_device_set_associated_device (device->associated, NULL);
      g_object_unref (device->associated);
      device->associated = NULL;
    }

  if (device->accessibility_virtual_device)
    g_clear_object (&device->accessibility_virtual_device);

  g_clear_pointer (&device->axes, g_array_unref);
  g_clear_pointer (&device->keys, g_array_unref);
  g_clear_pointer (&device->scroll_info, g_array_unref);

  G_OBJECT_CLASS (clutter_input_device_parent_class)->dispose (gobject);
}

static void
clutter_input_device_set_property (GObject      *gobject,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ClutterInputDevice *self = CLUTTER_INPUT_DEVICE (gobject);

  switch (prop_id)
    {
    case PROP_ID:
      self->id = g_value_get_int (value);
      break;

    case PROP_DEVICE_TYPE:
      self->device_type = g_value_get_enum (value);
      break;

    case PROP_SEAT:
      self->seat = g_value_get_object (value);
      break;

    case PROP_DEVICE_MODE:
      self->device_mode = g_value_get_enum (value);
      break;

    case PROP_BACKEND:
      self->backend = g_value_get_object (value);
      break;

    case PROP_NAME:
      self->device_name = g_value_dup_string (value);
      break;

    case PROP_HAS_CURSOR:
      self->has_cursor = g_value_get_boolean (value);
      break;

    case PROP_VENDOR_ID:
      self->vendor_id = g_value_dup_string (value);
      break;

    case PROP_PRODUCT_ID:
      self->product_id = g_value_dup_string (value);
      break;

    case PROP_N_RINGS:
      self->n_rings = g_value_get_int (value);
      break;

    case PROP_N_STRIPS:
      self->n_strips = g_value_get_int (value);
      break;

    case PROP_N_MODE_GROUPS:
      self->n_mode_groups = g_value_get_int (value);
      break;

    case PROP_DEVICE_NODE:
      self->node_path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_input_device_get_property (GObject    *gobject,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterInputDevice *self = CLUTTER_INPUT_DEVICE (gobject);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_int (value, self->id);
      break;

    case PROP_DEVICE_TYPE:
      g_value_set_enum (value, self->device_type);
      break;

    case PROP_SEAT:
      g_value_set_object (value, self->seat);
      break;

    case PROP_DEVICE_MODE:
      g_value_set_enum (value, self->device_mode);
      break;

    case PROP_BACKEND:
      g_value_set_object (value, self->backend);
      break;

    case PROP_NAME:
      g_value_set_string (value, self->device_name);
      break;

    case PROP_HAS_CURSOR:
      g_value_set_boolean (value, self->has_cursor);
      break;

    case PROP_N_AXES:
      g_value_set_uint (value, clutter_input_device_get_n_axes (self));
      break;

    case PROP_VENDOR_ID:
      g_value_set_string (value, self->vendor_id);
      break;

    case PROP_PRODUCT_ID:
      g_value_set_string (value, self->product_id);
      break;

    case PROP_N_RINGS:
      g_value_set_int (value, self->n_rings);
      break;

    case PROP_N_STRIPS:
      g_value_set_int (value, self->n_strips);
      break;

    case PROP_N_MODE_GROUPS:
      g_value_set_int (value, self->n_mode_groups);
      break;

    case PROP_DEVICE_NODE:
      g_value_set_string (value, self->node_path);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_input_device_class_init (ClutterInputDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  /**
   * ClutterInputDevice:id:
   *
   * The unique identifier of the device
   *
   * Since: 1.2
   */
  obj_props[PROP_ID] =
    g_param_spec_int ("id",
                      P_("Id"),
                      P_("Unique identifier of the device"),
                      -1, G_MAXINT,
                      0,
                      CLUTTER_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:name:
   *
   * The name of the device
   *
   * Since: 1.2
   */
  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         P_("Name"),
                         P_("The name of the device"),
                         NULL,
                         CLUTTER_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:device-type:
   *
   * The type of the device
   *
   * Since: 1.2
   */
  obj_props[PROP_DEVICE_TYPE] =
    g_param_spec_enum ("device-type",
                       P_("Device Type"),
                       P_("The type of the device"),
                       CLUTTER_TYPE_INPUT_DEVICE_TYPE,
                       CLUTTER_POINTER_DEVICE,
                       CLUTTER_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:seat:
   *
   * The #ClutterSeat instance which owns the device
   */
  obj_props[PROP_SEAT] =
    g_param_spec_object ("seat",
                         P_("Seat"),
                         P_("Seat"),
                         CLUTTER_TYPE_SEAT,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:mode:
   *
   * The mode of the device.
   *
   * Since: 1.6
   */
  obj_props[PROP_DEVICE_MODE] =
    g_param_spec_enum ("device-mode",
                       P_("Device Mode"),
                       P_("The mode of the device"),
                       CLUTTER_TYPE_INPUT_MODE,
                       CLUTTER_INPUT_MODE_FLOATING,
                       CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:has-cursor:
   *
   * Whether the device has an on screen cursor following its movement.
   *
   * Since: 1.6
   */
  obj_props[PROP_HAS_CURSOR] =
    g_param_spec_boolean ("has-cursor",
                          P_("Has Cursor"),
                          P_("Whether the device has a cursor"),
                          FALSE,
                          CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:n-axes:
   *
   * The number of axes of the device.
   *
   * Since: 1.6
   */
  obj_props[PROP_N_AXES] =
    g_param_spec_uint ("n-axes",
                       P_("Number of Axes"),
                       P_("The number of axes on the device"),
                       0, G_MAXUINT,
                       0,
                       CLUTTER_PARAM_READABLE);

  /**
   * ClutterInputDevice:backend:
   *
   * The #ClutterBackend that created the device.
   *
   * Since: 1.6
   */
  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         P_("Backend"),
                         P_("The backend instance"),
                         CLUTTER_TYPE_BACKEND,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:vendor-id:
   *
   * Vendor ID of this device.
   *
   * Since: 1.22
   */
  obj_props[PROP_VENDOR_ID] =
    g_param_spec_string ("vendor-id",
                         P_("Vendor ID"),
                         P_("Vendor ID"),
                         NULL,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * ClutterInputDevice:product-id:
   *
   * Product ID of this device.
   *
   * Since: 1.22
   */
  obj_props[PROP_PRODUCT_ID] =
    g_param_spec_string ("product-id",
                         P_("Product ID"),
                         P_("Product ID"),
                         NULL,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  obj_props[PROP_N_RINGS] =
    g_param_spec_int ("n-rings",
                      P_("Number of rings"),
                      P_("Number of rings (circular sliders) in this device"),
                      0, G_MAXINT, 0,
                      CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  obj_props[PROP_N_STRIPS] =
    g_param_spec_int ("n-strips",
                      P_("Number of strips"),
                      P_("Number of strips (linear sliders) in this device"),
                      0, G_MAXINT, 0,
                      CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  obj_props[PROP_N_MODE_GROUPS] =
    g_param_spec_int ("n-mode-groups",
                      P_("Number of mode groups"),
                      P_("Number of mode groups"),
                      0, G_MAXINT, 0,
                      CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  obj_props[PROP_DEVICE_NODE] =
    g_param_spec_string ("device-node",
                         P_("Device node path"),
                         P_("Device node path"),
                         NULL,
                         CLUTTER_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  gobject_class->dispose = clutter_input_device_dispose;
  gobject_class->set_property = clutter_input_device_set_property;
  gobject_class->get_property = clutter_input_device_get_property;
  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_input_device_init (ClutterInputDevice *self)
{
  self->id = -1;
  self->device_type = CLUTTER_POINTER_DEVICE;

  self->click_count = 0;

  self->previous_time = CLUTTER_CURRENT_TIME;
  self->previous_x = -1;
  self->previous_y = -1;
  self->current_button_number = self->previous_button_number = -1;
}

/**
 * clutter_input_device_get_modifier_state:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the current modifiers state of the device, as seen
 * by the last event Clutter processed.
 *
 * Return value: the last known modifier state
 *
 * Since: 1.16
 */
ClutterModifierType
clutter_input_device_get_modifier_state (ClutterInputDevice *device)
{
  uint32_t modifiers;
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  seat = clutter_input_device_get_seat (device);

  if (!clutter_seat_query_state (seat, device, NULL, NULL, &modifiers))
    return 0;

  return modifiers;
}

/**
 * clutter_input_device_get_device_type:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the type of @device
 *
 * Return value: the type of the device
 *
 * Since: 1.0
 */
ClutterInputDeviceType
clutter_input_device_get_device_type (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        CLUTTER_POINTER_DEVICE);

  return device->device_type;
}

/**
 * clutter_input_device_get_device_id:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the unique identifier of @device
 *
 * Return value: the identifier of the device
 *
 * Since: 1.0
 */
gint
clutter_input_device_get_device_id (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), -1);

  return device->id;
}

/**
 * clutter_input_device_get_coords:
 * @device: a #ClutterInputDevice
 * @sequence: (allow-none): a #ClutterEventSequence, or %NULL if
 *   the device is not touch-based
 * @point: (out caller-allocates): return location for the pointer
 *   or touch point
 *
 * Retrieves the latest coordinates of a pointer or touch point of
 * @device.
 *
 * Return value: %FALSE if the device's sequence hasn't been found,
 *   and %TRUE otherwise.
 *
 * Since: 1.12
 */
gboolean
clutter_input_device_get_coords (ClutterInputDevice   *device,
                                 ClutterEventSequence *sequence,
                                 graphene_point_t     *point)
{
  ClutterSeat *seat;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);
  g_return_val_if_fail (point != NULL, FALSE);

  seat = clutter_input_device_get_seat (device);

  return clutter_seat_query_state (seat, device, sequence, point, NULL);
}

/**
 * clutter_input_device_get_device_name:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the name of the @device
 *
 * Return value: the name of the device, or %NULL. The returned string
 *   is owned by the #ClutterInputDevice and should never be modified
 *   or freed
 *
 * Since: 1.2
 */
const gchar *
clutter_input_device_get_device_name (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return device->device_name;
}

/**
 * clutter_input_device_get_has_cursor:
 * @device: a #ClutterInputDevice
 *
 * Retrieves whether @device has a pointer that follows the
 * device motion.
 *
 * Return value: %TRUE if the device has a cursor
 *
 * Since: 1.6
 */
gboolean
clutter_input_device_get_has_cursor (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  return device->has_cursor;
}

/**
 * clutter_input_device_get_device_mode:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the #ClutterInputMode of @device.
 *
 * Return value: the device mode
 *
 * Since: 1.6
 */
ClutterInputMode
clutter_input_device_get_device_mode (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        CLUTTER_INPUT_MODE_FLOATING);

  return device->device_mode;
}

/*< private >
 * clutter_input_device_reset_axes:
 * @device: a #ClutterInputDevice
 *
 * Resets the axes on @device
 */
void
_clutter_input_device_reset_axes (ClutterInputDevice *device)
{
  if (device->axes != NULL)
    {
      g_array_free (device->axes, TRUE);
      device->axes = NULL;

      g_object_notify_by_pspec (G_OBJECT (device), obj_props[PROP_N_AXES]);
    }
}

/*< private >
 * clutter_input_device_add_axis:
 * @device: a #ClutterInputDevice
 * @axis: the axis type
 * @minimum: the minimum axis value
 * @maximum: the maximum axis value
 * @resolution: the axis resolution
 *
 * Adds an axis of type @axis on @device.
 */
guint
_clutter_input_device_add_axis (ClutterInputDevice *device,
                                ClutterInputAxis    axis,
                                gdouble             minimum,
                                gdouble             maximum,
                                gdouble             resolution)
{
  ClutterAxisInfo info;
  guint pos;

  if (device->axes == NULL)
    device->axes = g_array_new (FALSE, TRUE, sizeof (ClutterAxisInfo));

  info.axis = axis;
  info.min_value = minimum;
  info.max_value = maximum;
  info.resolution = resolution;

  switch (axis)
    {
    case CLUTTER_INPUT_AXIS_X:
    case CLUTTER_INPUT_AXIS_Y:
      info.min_axis = 0;
      info.max_axis = 0;
      break;

    case CLUTTER_INPUT_AXIS_XTILT:
    case CLUTTER_INPUT_AXIS_YTILT:
      info.min_axis = -1;
      info.max_axis = 1;
      break;

    default:
      info.min_axis = 0;
      info.max_axis = 1;
      break;
    }

  device->axes = g_array_append_val (device->axes, info);
  pos = device->axes->len - 1;

  g_object_notify_by_pspec (G_OBJECT (device), obj_props[PROP_N_AXES]);

  return pos;
}

/*< private >
 * clutter_input_translate_axis:
 * @device: a #ClutterInputDevice
 * @index_: the index of the axis
 * @gint: the absolute value of the axis
 * @axis_value: (out): the translated value of the axis
 *
 * Performs a conversion from the absolute value of the axis
 * to a relative value.
 *
 * The axis at @index_ must not be %CLUTTER_INPUT_AXIS_X or
 * %CLUTTER_INPUT_AXIS_Y.
 *
 * Return value: %TRUE if the conversion was successful
 */
gboolean
_clutter_input_device_translate_axis (ClutterInputDevice *device,
                                      guint               index_,
                                      gdouble             value,
                                      gdouble            *axis_value)
{
  ClutterAxisInfo *info;
  gdouble width;
  gdouble real_value;

  if (device->axes == NULL || index_ >= device->axes->len)
    return FALSE;

  info = &g_array_index (device->axes, ClutterAxisInfo, index_);

  if (info->axis == CLUTTER_INPUT_AXIS_X ||
      info->axis == CLUTTER_INPUT_AXIS_Y)
    return FALSE;

  if (fabs (info->max_value - info->min_value) < 0.0000001)
    return FALSE;

  width = info->max_value - info->min_value;
  real_value = (info->max_axis * (value - info->min_value)
             + info->min_axis * (info->max_value - value))
             / width;

  if (axis_value)
    *axis_value = real_value;

  return TRUE;
}

/**
 * clutter_input_device_get_axis:
 * @device: a #ClutterInputDevice
 * @index_: the index of the axis
 *
 * Retrieves the type of axis on @device at the given index.
 *
 * Return value: the axis type
 *
 * Since: 1.6
 */
ClutterInputAxis
clutter_input_device_get_axis (ClutterInputDevice *device,
                               guint               index_)
{
  ClutterAxisInfo *info;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        CLUTTER_INPUT_AXIS_IGNORE);

  if (device->axes == NULL)
    return CLUTTER_INPUT_AXIS_IGNORE;

  if (index_ >= device->axes->len)
    return CLUTTER_INPUT_AXIS_IGNORE;

  info = &g_array_index (device->axes, ClutterAxisInfo, index_);

  return info->axis;
}

/**
 * clutter_input_device_get_axis_value:
 * @device: a #ClutterInputDevice
 * @axes: (array): an array of axes values, typically
 *   coming from clutter_event_get_axes()
 * @axis: the axis to extract
 * @value: (out): return location for the axis value
 *
 * Extracts the value of the given @axis of a #ClutterInputDevice from
 * an array of axis values.
 *
 * An example of typical usage for this function is:
 *
 * |[
 *   ClutterInputDevice *device = clutter_event_get_device (event);
 *   gdouble *axes = clutter_event_get_axes (event, NULL);
 *   gdouble pressure_value = 0;
 *
 *   clutter_input_device_get_axis_value (device, axes,
 *                                        CLUTTER_INPUT_AXIS_PRESSURE,
 *                                        &pressure_value);
 * ]|
 *
 * Return value: %TRUE if the value was set, and %FALSE otherwise
 *
 * Since: 1.6
 */
gboolean
clutter_input_device_get_axis_value (ClutterInputDevice *device,
                                     gdouble            *axes,
                                     ClutterInputAxis    axis,
                                     gdouble            *value)
{
  gint i;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);
  g_return_val_if_fail (device->axes != NULL, FALSE);

  for (i = 0; i < device->axes->len; i++)
    {
      ClutterAxisInfo *info;

      info = &g_array_index (device->axes, ClutterAxisInfo, i);

      if (info->axis == axis)
        {
          if (value)
            *value = axes[i];

          return TRUE;
        }
    }

  return FALSE;
}

/**
 * clutter_input_device_get_n_axes:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the number of axes available on @device.
 *
 * Return value: the number of axes on the device
 *
 * Since: 1.6
 */
guint
clutter_input_device_get_n_axes (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  if (device->axes != NULL)
    return device->axes->len;

  return 0;
}

/*< private >
 * clutter_input_device_set_n_keys:
 * @device: a #ClutterInputDevice
 * @n_keys: the number of keys of the device
 *
 * Initializes the keys of @device.
 *
 * Call clutter_input_device_set_key() on each key to set the keyval
 * and modifiers.
 */
void
_clutter_input_device_set_n_keys (ClutterInputDevice *device,
                                  guint               n_keys)
{
  if (device->keys != NULL)
    g_array_free (device->keys, TRUE);

  device->n_keys = n_keys;
  device->keys = g_array_sized_new (FALSE, TRUE,
                                    sizeof (ClutterKeyInfo),
                                    n_keys);
}

/**
 * clutter_input_device_get_n_keys:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the number of keys registered for @device.
 *
 * Return value: the number of registered keys
 *
 * Since: 1.6
 */
guint
clutter_input_device_get_n_keys (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  return device->n_keys;
}

/**
 * clutter_input_device_set_key:
 * @device: a #ClutterInputDevice
 * @index_: the index of the key
 * @keyval: the keyval
 * @modifiers: a bitmask of modifiers
 *
 * Sets the keyval and modifiers at the given @index_ for @device.
 *
 * Clutter will use the keyval and modifiers set when filling out
 * an event coming from the same input device.
 *
 * Since: 1.6
 */
void
clutter_input_device_set_key (ClutterInputDevice  *device,
                              guint                index_,
                              guint                keyval,
                              ClutterModifierType  modifiers)
{
  ClutterKeyInfo *key_info;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (index_ < device->n_keys);

  key_info = &g_array_index (device->keys, ClutterKeyInfo, index_);
  key_info->keyval = keyval;
  key_info->modifiers = modifiers;
}

/**
 * clutter_input_device_get_key:
 * @device: a #ClutterInputDevice
 * @index_: the index of the key
 * @keyval: (out): return location for the keyval at @index_
 * @modifiers: (out): return location for the modifiers at @index_
 *
 * Retrieves the key set using clutter_input_device_set_key()
 *
 * Return value: %TRUE if a key was set at the given index
 *
 * Since: 1.6
 */
gboolean
clutter_input_device_get_key (ClutterInputDevice  *device,
                              guint                index_,
                              guint               *keyval,
                              ClutterModifierType *modifiers)
{
  ClutterKeyInfo *key_info;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  if (device->keys == NULL)
    return FALSE;

  if (index_ > device->keys->len)
    return FALSE;

  key_info = &g_array_index (device->keys, ClutterKeyInfo, index_);

  if (!key_info->keyval && !key_info->modifiers)
    return FALSE;

  if (keyval)
    *keyval = key_info->keyval;

  if (modifiers)
    *modifiers = key_info->modifiers;

  return TRUE;
}

/*< private >
 * clutter_input_device_add_physical_device:
 * @logical: a #ClutterInputDevice
 * @physical: a #ClutterInputDevice
 *
 * Adds @physical to the list of physical devices of @logical
 *
 * This function does not increase the reference count of either @logical
 * or @physical.
 */
void
_clutter_input_device_add_physical_device (ClutterInputDevice *logical,
                                           ClutterInputDevice *physical)
{
  if (g_list_find (logical->physical_devices, physical) == NULL)
    logical->physical_devices = g_list_prepend (logical->physical_devices, physical);
}

/*< private >
 * clutter_input_device_remove_physical_device:
 * @logical: a #ClutterInputDevice
 * @physical: a #ClutterInputDevice
 *
 * Removes @physical from the list of physical devices of @logical.
 *
 * This function does not decrease the reference count of either @logical
 * or @physical.
 */
void
_clutter_input_device_remove_physical_device (ClutterInputDevice *logical,
                                              ClutterInputDevice *physical)
{
  if (g_list_find (logical->physical_devices, physical) != NULL)
    logical->physical_devices = g_list_remove (logical->physical_devices, physical);
}

/**
 * clutter_input_device_get_physical_devices:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the physical devices attached to @device.
 *
 * Return value: (transfer container) (element-type Clutter.InputDevice): a
 *   list of #ClutterInputDevice, or %NULL. The contents of the list are
 *   owned by the device. Use g_list_free() when done
 *
 * Since: 1.6
 */
GList *
clutter_input_device_get_physical_devices (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return g_list_copy (device->physical_devices);
}

/*< internal >
 * clutter_input_device_set_associated_device:
 * @device: a #ClutterInputDevice
 * @associated: (allow-none): a #ClutterInputDevice, or %NULL
 *
 * Sets the associated device for @device.
 *
 * This function keeps a reference on the associated device.
 */
void
_clutter_input_device_set_associated_device (ClutterInputDevice *device,
                                             ClutterInputDevice *associated)
{
  if (device->associated == associated)
    return;

  if (device->associated != NULL)
    g_object_unref (device->associated);

  device->associated = associated;
  if (device->associated != NULL)
    g_object_ref (device->associated);

  CLUTTER_NOTE (MISC, "Associating device %d '%s' to device %d '%s'",
                clutter_input_device_get_device_id (device),
                clutter_input_device_get_device_name (device),
                device->associated != NULL
                  ? clutter_input_device_get_device_id (device->associated)
                  : -1,
                device->associated != NULL
                  ? clutter_input_device_get_device_name (device->associated)
                  : "(none)");

  if (device->device_mode != CLUTTER_INPUT_MODE_LOGICAL)
    {
      if (device->associated != NULL)
        device->device_mode = CLUTTER_INPUT_MODE_PHYSICAL;
      else
        device->device_mode = CLUTTER_INPUT_MODE_FLOATING;

      g_object_notify_by_pspec (G_OBJECT (device), obj_props[PROP_DEVICE_MODE]);
    }
}

/**
 * clutter_input_device_get_associated_device:
 * @device: a #ClutterInputDevice
 *
 * Retrieves a pointer to the #ClutterInputDevice that has been
 * associated to @device.
 *
 * If the #ClutterInputDevice:device-mode property of @device is
 * set to %CLUTTER_INPUT_MODE_LOGICAL, this function will return
 * %NULL.
 *
 * Return value: (transfer none): a #ClutterInputDevice, or %NULL
 *
 * Since: 1.6
 */
ClutterInputDevice *
clutter_input_device_get_associated_device (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return device->associated;
}

/**
 * clutter_input_device_keycode_to_evdev:
 * @device: A #ClutterInputDevice
 * @hardware_keycode: The hardware keycode from a #ClutterKeyEvent
 * @evdev_keycode: The return location for the evdev keycode
 *
 * Translates a hardware keycode from a #ClutterKeyEvent to the
 * equivalent evdev keycode. Note that depending on the input backend
 * used by Clutter this function can fail if there is no obvious
 * mapping between the key codes. The hardware keycode can be taken
 * from the #ClutterKeyEvent.hardware_keycode member of #ClutterKeyEvent.
 *
 * Return value: %TRUE if the conversion succeeded, %FALSE otherwise.
 *
 * Since: 1.10
 */
gboolean
clutter_input_device_keycode_to_evdev (ClutterInputDevice *device,
                                       guint               hardware_keycode,
                                       guint              *evdev_keycode)
{
  ClutterInputDeviceClass *device_class;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);
  if (device_class->keycode_to_evdev == NULL)
    return FALSE;
  else
    return device_class->keycode_to_evdev (device,
                                           hardware_keycode,
                                           evdev_keycode);
}

void
_clutter_input_device_add_scroll_info (ClutterInputDevice     *device,
                                       guint                   index_,
                                       ClutterScrollDirection  direction,
                                       gdouble                 increment)
{
  ClutterScrollInfo info;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (index_ < clutter_input_device_get_n_axes (device));

  info.axis_id = index_;
  info.direction = direction;
  info.increment = increment;
  info.last_value_valid = FALSE;

  if (device->scroll_info == NULL)
    {
      device->scroll_info = g_array_new (FALSE,
                                         FALSE,
                                         sizeof (ClutterScrollInfo));
    }

  g_array_append_val (device->scroll_info, info);
}

gboolean
_clutter_input_device_get_scroll_delta (ClutterInputDevice     *device,
                                        guint                   index_,
                                        gdouble                 value,
                                        ClutterScrollDirection *direction_p,
                                        gdouble                *delta_p)
{
  guint i;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);
  g_return_val_if_fail (index_ < clutter_input_device_get_n_axes (device), FALSE);

  if (device->scroll_info == NULL)
    return FALSE;

  for (i = 0; i < device->scroll_info->len; i++)
    {
      ClutterScrollInfo *info = &g_array_index (device->scroll_info,
                                                ClutterScrollInfo,
                                                i);

      if (info->axis_id == index_)
        {
          if (direction_p != NULL)
            *direction_p = info->direction;

          if (delta_p != NULL)
            *delta_p = 0.0;

          if (info->last_value_valid)
            {
              if (delta_p != NULL)
                {
                  *delta_p = (value - info->last_value)
                           / info->increment;
                }

              info->last_value = value;
            }
          else
            {
              info->last_value = value;
              info->last_value_valid = TRUE;
            }

          return TRUE;
        }
    }

  return FALSE;
}

void
_clutter_input_device_reset_scroll_info (ClutterInputDevice *device)
{
  guint i;

  if (device->scroll_info == NULL)
    return;

  for (i = 0; i < device->scroll_info->len; i++)
    {
      ClutterScrollInfo *info = &g_array_index (device->scroll_info,
                                                ClutterScrollInfo,
                                                i);

      info->last_value_valid = FALSE;
    }
}

static void
on_grab_actor_destroy (ClutterActor       *actor,
                       ClutterInputDevice *device)
{
  switch (device->device_type)
    {
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_TABLET_DEVICE:
      device->pointer_grab_actor = NULL;
      break;

    case CLUTTER_KEYBOARD_DEVICE:
      device->keyboard_grab_actor = NULL;
      break;

    default:
      g_assert_not_reached ();
    }
}

/**
 * clutter_input_device_grab:
 * @device: a #ClutterInputDevice
 * @actor: a #ClutterActor
 *
 * Acquires a grab on @actor for the given @device.
 *
 * Any event coming from @device will be delivered to @actor, bypassing
 * the usual event delivery mechanism, until the grab is released by
 * calling clutter_input_device_ungrab().
 *
 * The grab is client-side: even if the windowing system used by the Clutter
 * backend has the concept of "device grabs", Clutter will not use them.
 *
 * Only #ClutterInputDevice of types %CLUTTER_POINTER_DEVICE,
 * %CLUTTER_TABLET_DEVICE and %CLUTTER_KEYBOARD_DEVICE can hold a grab.
 *
 * Since: 1.10
 */
void
clutter_input_device_grab (ClutterInputDevice *device,
                           ClutterActor       *actor)
{
  ClutterActor **grab_actor;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  switch (device->device_type)
    {
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_TABLET_DEVICE:
      grab_actor = &device->pointer_grab_actor;
      break;

    case CLUTTER_KEYBOARD_DEVICE:
      grab_actor = &device->keyboard_grab_actor;
      break;

    default:
      g_critical ("Only pointer and keyboard devices can grab an actor");
      return;
    }

  if (*grab_actor != NULL)
    {
      g_signal_handlers_disconnect_by_func (*grab_actor,
                                            G_CALLBACK (on_grab_actor_destroy),
                                            device);
    }

  *grab_actor = actor;

  g_signal_connect (*grab_actor,
                    "destroy",
                    G_CALLBACK (on_grab_actor_destroy),
                    device);
}

/**
 * clutter_input_device_ungrab:
 * @device: a #ClutterInputDevice
 *
 * Releases the grab on the @device, if one is in place.
 *
 * Since: 1.10
 */
void
clutter_input_device_ungrab (ClutterInputDevice *device)
{
  ClutterActor **grab_actor;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  switch (device->device_type)
    {
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_TABLET_DEVICE:
      grab_actor = &device->pointer_grab_actor;
      break;

    case CLUTTER_KEYBOARD_DEVICE:
      grab_actor = &device->keyboard_grab_actor;
      break;

    default:
      return;
    }

  if (*grab_actor == NULL)
    return;

  g_signal_handlers_disconnect_by_func (*grab_actor,
                                        G_CALLBACK (on_grab_actor_destroy),
                                        device);

  *grab_actor = NULL;
}

/**
 * clutter_input_device_get_grabbed_actor:
 * @device: a #ClutterInputDevice
 *
 * Retrieves a pointer to the #ClutterActor currently grabbing all
 * the events coming from @device.
 *
 * Return value: (transfer none): a #ClutterActor, or %NULL
 *
 * Since: 1.10
 */
ClutterActor *
clutter_input_device_get_grabbed_actor (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  switch (device->device_type)
    {
    case CLUTTER_POINTER_DEVICE:
    case CLUTTER_TABLET_DEVICE:
      return device->pointer_grab_actor;

    case CLUTTER_KEYBOARD_DEVICE:
      return device->keyboard_grab_actor;

    default:
      g_critical ("Only pointer and keyboard devices can grab an actor");
    }

  return NULL;
}

static void
on_grab_sequence_actor_destroy (ClutterActor       *actor,
                                ClutterInputDevice *device)
{
  ClutterEventSequence *sequence =
    g_hash_table_lookup (device->inv_sequence_grab_actors, actor);

  if (sequence != NULL)
    {
      g_hash_table_remove (device->sequence_grab_actors, sequence);
      g_hash_table_remove (device->inv_sequence_grab_actors, actor);
    }
}

/**
 * clutter_input_device_sequence_grab:
 * @device: a #ClutterInputDevice
 * @sequence: a #ClutterEventSequence
 * @actor: a #ClutterActor
 *
 * Acquires a grab on @actor for the given @device and the given touch
 * @sequence.
 *
 * Any touch event coming from @device and from @sequence will be
 * delivered to @actor, bypassing the usual event delivery mechanism,
 * until the grab is released by calling
 * clutter_input_device_sequence_ungrab().
 *
 * The grab is client-side: even if the windowing system used by the Clutter
 * backend has the concept of "device grabs", Clutter will not use them.
 *
 * Since: 1.12
 */
void
clutter_input_device_sequence_grab (ClutterInputDevice   *device,
                                    ClutterEventSequence *sequence,
                                    ClutterActor         *actor)
{
  ClutterActor *grab_actor;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  if (device->sequence_grab_actors == NULL)
    {
      grab_actor = NULL;
      device->sequence_grab_actors = g_hash_table_new (NULL, NULL);
      device->inv_sequence_grab_actors = g_hash_table_new (NULL, NULL);
    }
  else
    {
      grab_actor = g_hash_table_lookup (device->sequence_grab_actors, sequence);
    }

  if (grab_actor != NULL)
    {
      g_signal_handlers_disconnect_by_func (grab_actor,
                                            G_CALLBACK (on_grab_sequence_actor_destroy),
                                            device);
      g_hash_table_remove (device->sequence_grab_actors, sequence);
      g_hash_table_remove (device->inv_sequence_grab_actors, grab_actor);
    }

  g_hash_table_insert (device->sequence_grab_actors, sequence, actor);
  g_hash_table_insert (device->inv_sequence_grab_actors, actor, sequence);
  g_signal_connect (actor,
                    "destroy",
                    G_CALLBACK (on_grab_sequence_actor_destroy),
                    device);
}

/**
 * clutter_input_device_sequence_ungrab:
 * @device: a #ClutterInputDevice
 * @sequence: a #ClutterEventSequence
 *
 * Releases the grab on the @device for the given @sequence, if one is
 * in place.
 *
 * Since: 1.12
 */
void
clutter_input_device_sequence_ungrab (ClutterInputDevice   *device,
                                      ClutterEventSequence *sequence)
{
  ClutterActor *grab_actor;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  if (device->sequence_grab_actors == NULL)
    return;

  grab_actor = g_hash_table_lookup (device->sequence_grab_actors, sequence);

  if (grab_actor == NULL)
    return;

  g_signal_handlers_disconnect_by_func (grab_actor,
                                        G_CALLBACK (on_grab_sequence_actor_destroy),
                                        device);
  g_hash_table_remove (device->sequence_grab_actors, sequence);
  g_hash_table_remove (device->inv_sequence_grab_actors, grab_actor);

  if (g_hash_table_size (device->sequence_grab_actors) == 0)
    {
      g_hash_table_destroy (device->sequence_grab_actors);
      device->sequence_grab_actors = NULL;
      g_hash_table_destroy (device->inv_sequence_grab_actors);
      device->inv_sequence_grab_actors = NULL;
    }
}

/**
 * clutter_input_device_sequence_get_grabbed_actor:
 * @device: a #ClutterInputDevice
 * @sequence: a #ClutterEventSequence
 *
 * Retrieves a pointer to the #ClutterActor currently grabbing the
 * touch events coming from @device given the @sequence.
 *
 * Return value: (transfer none): a #ClutterActor, or %NULL
 *
 * Since: 1.12
 */
ClutterActor *
clutter_input_device_sequence_get_grabbed_actor (ClutterInputDevice   *device,
                                                 ClutterEventSequence *sequence)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  if (device->sequence_grab_actors == NULL)
    return NULL;

  return g_hash_table_lookup (device->sequence_grab_actors, sequence);
}

/**
 * clutter_input_device_get_vendor_id:
 * @device: a physical #ClutterInputDevice
 *
 * Gets the vendor ID of this device.
 *
 * Returns: the vendor ID
 *
 * Since: 1.22
 */
const gchar *
clutter_input_device_get_vendor_id (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL, NULL);

  return device->vendor_id;
}

/**
 * clutter_input_device_get_product_id:
 * @device: a physical #ClutterInputDevice
 *
 * Gets the product ID of this device.
 *
 * Returns: the product ID
 *
 * Since: 1.22
 */
const gchar *
clutter_input_device_get_product_id (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL, NULL);

  return device->product_id;
}

void
clutter_input_device_add_tool (ClutterInputDevice     *device,
                               ClutterInputDeviceTool *tool)
{
  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL);
  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE_TOOL (tool));

  if (!device->tools)
    device->tools = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

  g_ptr_array_add (device->tools, tool);
}

ClutterInputDeviceTool *
clutter_input_device_lookup_tool (ClutterInputDevice         *device,
                                  guint64                     serial,
                                  ClutterInputDeviceToolType  type)
{
  ClutterInputDeviceTool *tool;
  guint i;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL, NULL);

  if (!device->tools)
    return NULL;

  for (i = 0; i < device->tools->len; i++)
    {
      tool = g_ptr_array_index (device->tools, i);

      if (serial == clutter_input_device_tool_get_serial (tool) &&
          type == clutter_input_device_tool_get_tool_type (tool))
        return tool;
    }

  return NULL;
}

void
clutter_input_device_update_from_tool (ClutterInputDevice     *device,
                                       ClutterInputDeviceTool *tool)
{
  ClutterInputDeviceClass *device_class;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);

  if (device_class->update_from_tool)
    device_class->update_from_tool (device, tool);
}

gint
clutter_input_device_get_n_rings (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  return device->n_rings;
}

gint
clutter_input_device_get_n_strips (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  return device->n_strips;
}

gint
clutter_input_device_get_n_mode_groups (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, 0);

  return device->n_mode_groups;
}

gint
clutter_input_device_get_group_n_modes (ClutterInputDevice *device,
                                        gint                group)
{
  ClutterInputDeviceClass *device_class;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, 0);
  g_return_val_if_fail (group >= 0, 0);

  device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);

  if (device_class->get_group_n_modes)
    return device_class->get_group_n_modes (device, group);

  return 0;
}

gboolean
clutter_input_device_is_mode_switch_button (ClutterInputDevice *device,
                                            guint               group,
                                            guint               button)
{
  ClutterInputDeviceClass *device_class;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, FALSE);

  device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);

  if (device_class->is_mode_switch_button)
    return device_class->is_mode_switch_button (device, group, button);

  return FALSE;
}

gint
clutter_input_device_get_mode_switch_button_group (ClutterInputDevice *device,
                                                   guint               button)
{
  gint group;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), -1);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, -1);

  for (group = 0; group < device->n_mode_groups; group++)
    {
      if (clutter_input_device_is_mode_switch_button (device, group, button))
        return group;
    }

  return -1;
}

const gchar *
clutter_input_device_get_device_node (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return device->node_path;
}

gboolean
clutter_input_device_is_grouped (ClutterInputDevice *device,
                                 ClutterInputDevice *other_device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (other_device), FALSE);

  return CLUTTER_INPUT_DEVICE_GET_CLASS (device)->is_grouped (device, other_device);
}

/**
 * clutter_input_device_get_seat:
 * @device: a #ClutterInputDevice
 *
 * Returns the seat the device belongs to
 *
 * Returns: (transfer none): the device seat
 **/
ClutterSeat *
clutter_input_device_get_seat (ClutterInputDevice *device)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return device->seat;
}
