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

  PROP_NAME,

  PROP_DEVICE_TYPE,
  PROP_SEAT,
  PROP_DEVICE_MODE,

  PROP_HAS_CURSOR,

  PROP_VENDOR_ID,
  PROP_PRODUCT_ID,

  PROP_N_STRIPS,
  PROP_N_RINGS,
  PROP_N_MODE_GROUPS,
  PROP_N_BUTTONS,
  PROP_DEVICE_NODE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

typedef struct _ClutterInputDevicePrivate ClutterInputDevicePrivate;

struct _ClutterInputDevicePrivate
{
  ClutterInputDeviceType device_type;
  ClutterInputMode device_mode;

  char *device_name;

  ClutterSeat *seat;

  ClutterBackend *backend;

  char *vendor_id;
  char *product_id;
  char *node_path;

  int n_rings;
  int n_strips;
  int n_mode_groups;
  int n_buttons;

  gboolean has_cursor;
};

G_DEFINE_TYPE_WITH_PRIVATE (ClutterInputDevice, clutter_input_device, G_TYPE_OBJECT);

static void
clutter_input_device_dispose (GObject *gobject)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (gobject);
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_clear_pointer (&priv->device_name, g_free);
  g_clear_pointer (&priv->vendor_id, g_free);
  g_clear_pointer (&priv->product_id, g_free);
  g_clear_pointer (&priv->node_path, g_free);

  if (device->accessibility_virtual_device)
    g_clear_object (&device->accessibility_virtual_device);

  G_OBJECT_CLASS (clutter_input_device_parent_class)->dispose (gobject);
}

static void
clutter_input_device_set_property (GObject      *gobject,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ClutterInputDevice *self = CLUTTER_INPUT_DEVICE (gobject);
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DEVICE_TYPE:
      priv->device_type = g_value_get_enum (value);
      break;

    case PROP_SEAT:
      priv->seat = g_value_get_object (value);
      break;

    case PROP_DEVICE_MODE:
      priv->device_mode = g_value_get_enum (value);
      break;

    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;

    case PROP_NAME:
      priv->device_name = g_value_dup_string (value);
      break;

    case PROP_HAS_CURSOR:
      priv->has_cursor = g_value_get_boolean (value);
      break;

    case PROP_VENDOR_ID:
      priv->vendor_id = g_value_dup_string (value);
      break;

    case PROP_PRODUCT_ID:
      priv->product_id = g_value_dup_string (value);
      break;

    case PROP_N_RINGS:
      priv->n_rings = g_value_get_int (value);
      break;

    case PROP_N_STRIPS:
      priv->n_strips = g_value_get_int (value);
      break;

    case PROP_N_MODE_GROUPS:
      priv->n_mode_groups = g_value_get_int (value);
      break;

    case PROP_N_BUTTONS:
      priv->n_buttons = g_value_get_int (value);
      break;

    case PROP_DEVICE_NODE:
      priv->node_path = g_value_dup_string (value);
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DEVICE_TYPE:
      g_value_set_enum (value, priv->device_type);
      break;

    case PROP_SEAT:
      g_value_set_object (value, priv->seat);
      break;

    case PROP_DEVICE_MODE:
      g_value_set_enum (value, priv->device_mode);
      break;

    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;

    case PROP_NAME:
      g_value_set_string (value, priv->device_name);
      break;

    case PROP_HAS_CURSOR:
      g_value_set_boolean (value, priv->has_cursor);
      break;

    case PROP_VENDOR_ID:
      g_value_set_string (value, priv->vendor_id);
      break;

    case PROP_PRODUCT_ID:
      g_value_set_string (value, priv->product_id);
      break;

    case PROP_N_RINGS:
      g_value_set_int (value, priv->n_rings);
      break;

    case PROP_N_STRIPS:
      g_value_set_int (value, priv->n_strips);
      break;

    case PROP_N_MODE_GROUPS:
      g_value_set_int (value, priv->n_mode_groups);
      break;

    case PROP_N_BUTTONS:
      g_value_set_int (value, priv->n_buttons);
      break;

    case PROP_DEVICE_NODE:
      g_value_set_string (value, priv->node_path);
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

  obj_props[PROP_N_BUTTONS] =
    g_param_spec_int ("n-buttons",
                      P_("Number of buttons"),
                      P_("Number of buttons"),
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (self);

  priv->device_type = CLUTTER_POINTER_DEVICE;

  self->click_count = 0;

  self->previous_time = CLUTTER_CURRENT_TIME;
  self->previous_x = -1;
  self->previous_y = -1;
  self->current_button_number = self->previous_button_number = -1;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        CLUTTER_POINTER_DEVICE);

  return priv->device_type;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return priv->device_name;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), FALSE);

  return priv->has_cursor;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        CLUTTER_INPUT_MODE_FLOATING);

  return priv->device_mode;
}

static void
on_grab_actor_destroy (ClutterActor       *actor,
                       ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  switch (priv->device_type)
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  switch (priv->device_type)
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  switch (priv->device_type)
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  switch (priv->device_type)
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL, NULL);

  return priv->vendor_id;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_mode (device) != CLUTTER_INPUT_MODE_LOGICAL, NULL);

  return priv->product_id;
}

gint
clutter_input_device_get_n_rings (ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  return priv->n_rings;
}

gint
clutter_input_device_get_n_strips (ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);

  return priv->n_strips;
}

gint
clutter_input_device_get_n_mode_groups (ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, 0);

  return priv->n_mode_groups;
}

gint
clutter_input_device_get_n_buttons (ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), 0);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, 0);

  return priv->n_buttons;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);
  gint group;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), -1);
  g_return_val_if_fail (clutter_input_device_get_device_type (device) ==
                        CLUTTER_PAD_DEVICE, -1);

  for (group = 0; group < priv->n_mode_groups; group++)
    {
      if (clutter_input_device_is_mode_switch_button (device, group, button))
        return group;
    }

  return -1;
}

int
clutter_input_device_get_pad_feature_group (ClutterInputDevice           *device,
                                            ClutterInputDevicePadFeature  feature,
                                            int                           n_feature)
{
  ClutterInputDeviceClass *device_class;

  device_class = CLUTTER_INPUT_DEVICE_GET_CLASS (device);
  if (!device_class->get_pad_feature_group)
    return 0;

  return CLUTTER_INPUT_DEVICE_GET_CLASS (device)->get_pad_feature_group (device,
                                                                         feature,
                                                                         n_feature);
}

const gchar *
clutter_input_device_get_device_node (ClutterInputDevice *device)
{
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return priv->node_path;
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
  ClutterInputDevicePrivate *priv =
    clutter_input_device_get_instance_private (device);

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  return priv->seat;
}
