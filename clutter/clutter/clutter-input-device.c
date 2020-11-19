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
  PROP_DEVICE_NODE,

  PROP_LAST
};

static void on_cursor_actor_destroy (ClutterActor       *actor,
                                     ClutterInputDevice *device);
static void on_cursor_actor_reactive_changed (ClutterActor       *actor,
                                              GParamSpec         *pspec,
                                              ClutterInputDevice *device);

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

  if (device->accessibility_virtual_device)
    g_clear_object (&device->accessibility_virtual_device);

  g_clear_pointer (&device->touch_sequence_actors, g_hash_table_unref);

  if (device->cursor_actor)
    {
      g_signal_handlers_disconnect_by_func (device->cursor_actor,
                                            G_CALLBACK (on_cursor_actor_destroy),
                                            device);
      g_signal_handlers_disconnect_by_func (device->cursor_actor,
                                            G_CALLBACK (on_cursor_actor_reactive_changed),
                                            device);
      _clutter_actor_set_has_pointer (device->cursor_actor, FALSE);
      device->cursor_actor = NULL;
    }

  if (device->inv_touch_sequence_actors)
    {
      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, device->inv_touch_sequence_actors);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          g_signal_handlers_disconnect_by_func (key,
                                                G_CALLBACK (on_cursor_actor_destroy),
                                                device);
          g_signal_handlers_disconnect_by_func (device->cursor_actor,
                                                G_CALLBACK (on_cursor_actor_reactive_changed),
                                                device);
          _clutter_actor_set_has_pointer (key, FALSE);
          g_list_free (value);
        }

      g_hash_table_unref (device->inv_touch_sequence_actors);
      device->inv_touch_sequence_actors = NULL;
    }

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
  self->device_type = CLUTTER_POINTER_DEVICE;

  self->click_count = 0;

  self->previous_time = CLUTTER_CURRENT_TIME;
  self->previous_x = -1;
  self->previous_y = -1;
  self->current_button_number = self->previous_button_number = -1;

  self->touch_sequence_actors = g_hash_table_new (NULL, NULL);
  self->inv_touch_sequence_actors = g_hash_table_new (NULL, NULL);
}

static void
_clutter_input_device_associate_actor (ClutterInputDevice   *device,
                                       ClutterEventSequence *sequence,
                                       ClutterActor         *actor)
{
  if (sequence == NULL)
    device->cursor_actor = actor;
  else
    {
      GList *sequences =
        g_hash_table_lookup (device->inv_touch_sequence_actors, actor);

      g_hash_table_insert (device->touch_sequence_actors, sequence, actor);
      g_hash_table_insert (device->inv_touch_sequence_actors,
                           actor, g_list_prepend (sequences, sequence));
    }

  g_signal_connect (actor,
                    "destroy", G_CALLBACK (on_cursor_actor_destroy),
                    device);
  g_signal_connect (actor,
                    "notify::reactive", G_CALLBACK (on_cursor_actor_reactive_changed),
                    device);
  _clutter_actor_set_has_pointer (actor, TRUE);
}

static void
_clutter_input_device_unassociate_actor (ClutterInputDevice   *device,
                                         ClutterActor         *actor,
                                         gboolean              destroyed)
{
  if (device->cursor_actor == actor)
    device->cursor_actor = NULL;
  else
    {
      GList *l, *sequences =
        g_hash_table_lookup (device->inv_touch_sequence_actors,
                             actor);

      for (l = sequences; l != NULL; l = l->next)
        g_hash_table_remove (device->touch_sequence_actors, l->data);

      g_list_free (sequences);
      g_hash_table_remove (device->inv_touch_sequence_actors, actor);
    }

  if (destroyed == FALSE)
    {
      g_signal_handlers_disconnect_by_func (actor,
                                            G_CALLBACK (on_cursor_actor_destroy),
                                            device);
      g_signal_handlers_disconnect_by_func (actor,
                                            G_CALLBACK (on_cursor_actor_reactive_changed),
                                            device);
      _clutter_actor_set_has_pointer (actor, FALSE);
    }
}

static void
on_cursor_actor_destroy (ClutterActor       *actor,
                         ClutterInputDevice *device)
{
  _clutter_input_device_unassociate_actor (device, actor, TRUE);
}

static void
on_cursor_actor_reactive_changed (ClutterActor       *actor,
                                  GParamSpec         *pspec,
                                  ClutterInputDevice *device)
{
  if (!clutter_actor_get_reactive (actor))
    _clutter_input_device_unassociate_actor (device, actor, FALSE);
}

/*< private >
 * clutter_input_device_set_actor:
 * @device: a #ClutterInputDevice
 * @actor: a #ClutterActor
 * @emit_crossing: %TRUE to emit crossing events
 *
 * Sets the actor under the pointer coordinates of @device
 *
 * This function is called by clutter_input_device_update()
 * and it will:
 *
 *   - queue a %CLUTTER_LEAVE event on the previous pointer actor
 *     of @device, if any
 *   - set to %FALSE the :has-pointer property of the previous
 *     pointer actor of @device, if any
 *   - queue a %CLUTTER_ENTER event on the new pointer actor
 *   - set to %TRUE the :has-pointer property of the new pointer
 *     actor
 */
static void
_clutter_input_device_set_actor (ClutterInputDevice   *device,
                                 ClutterEventSequence *sequence,
                                 ClutterStage         *stage,
                                 ClutterActor         *actor,
                                 gboolean              emit_crossing,
                                 graphene_point_t      coords,
                                 uint32_t              time_ms)
{
  ClutterActor *old_actor = clutter_input_device_get_actor (device, sequence);

  if (old_actor == actor)
    return;

  if (old_actor != NULL)
    {
      ClutterActor *tmp_old_actor;

      if (emit_crossing)
        {
          ClutterEvent *event;

          event = clutter_event_new (CLUTTER_LEAVE);
          event->crossing.time = time_ms;
          event->crossing.flags = 0;
          event->crossing.stage = stage;
          event->crossing.source = old_actor;
          event->crossing.x = coords.x;
          event->crossing.y = coords.y;
          event->crossing.related = actor;
          event->crossing.sequence = sequence;
          clutter_event_set_device (event, device);

          /* we need to make sure that this event is processed
           * before any other event we might have queued up until
           * now, so we go on, and synthesize the event emission
           * ourselves
           */
          _clutter_process_event (event);

          clutter_event_free (event);
        }

      /* processing the event might have destroyed the actor */
      tmp_old_actor = clutter_input_device_get_actor (device, sequence);
      _clutter_input_device_unassociate_actor (device,
                                               old_actor,
                                               tmp_old_actor == NULL);
      old_actor = tmp_old_actor;
    }

  if (actor != NULL)
    {
      _clutter_input_device_associate_actor (device, sequence, actor);

      if (emit_crossing)
        {
          ClutterEvent *event;

          event = clutter_event_new (CLUTTER_ENTER);
          event->crossing.time = time_ms;
          event->crossing.flags = 0;
          event->crossing.stage = stage;
          event->crossing.x = coords.x;
          event->crossing.y = coords.y;
          event->crossing.source = actor;
          event->crossing.related = old_actor;
          event->crossing.sequence = sequence;
          clutter_event_set_device (event, device);

          /* see above */
          _clutter_process_event (event);

          clutter_event_free (event);
        }
    }
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

/*
 * clutter_input_device_update:
 * @device: a #ClutterInputDevice
 *
 * Updates the input @device by determining the #ClutterActor underneath the
 * pointer's cursor
 *
 * This function calls _clutter_input_device_set_actor() if needed.
 *
 * This function only works for #ClutterInputDevice of type
 * %CLUTTER_POINTER_DEVICE.
 *
 * Since: 1.2
 */
ClutterActor *
clutter_input_device_update (ClutterInputDevice   *device,
                             ClutterEventSequence *sequence,
                             ClutterStage         *stage,
                             gboolean              emit_crossing,
                             ClutterEvent         *for_event)
{
  ClutterActor *new_cursor_actor;
  ClutterActor *old_cursor_actor;
  graphene_point_t point = GRAPHENE_POINT_INIT (-1.0f, -1.0f);
  ClutterInputDeviceType device_type = device->device_type;
  uint32_t time_ms;

  g_assert (device_type != CLUTTER_KEYBOARD_DEVICE &&
            device_type != CLUTTER_PAD_DEVICE);

  if (for_event)
    {
      clutter_event_get_coords (for_event, &point.x, &point.y);
      time_ms = clutter_event_get_time (for_event);
    }
  else
    {
      clutter_seat_query_state (clutter_input_device_get_seat (device),
                                device, NULL, &point, NULL);
      time_ms = CLUTTER_CURRENT_TIME;
    }

  old_cursor_actor = clutter_input_device_get_actor (device, sequence);
  new_cursor_actor =
    _clutter_stage_do_pick (stage, point.x, point.y, CLUTTER_PICK_REACTIVE);

  /* if the pick could not find an actor then we do not update the
   * input device, to avoid ghost enter/leave events; the pick should
   * never fail, except for bugs in the glReadPixels() implementation
   * in which case this is the safest course of action anyway
   */
  if (new_cursor_actor == NULL)
    return NULL;

  CLUTTER_NOTE (EVENT,
                "Actor under cursor (device '%s', at %.2f, %.2f): %s",
                clutter_input_device_get_device_name (device),
                point.x,
                point.y,
                _clutter_actor_get_debug_name (new_cursor_actor));

  /* short-circuit here */
  if (new_cursor_actor == old_cursor_actor)
    return old_cursor_actor;

  _clutter_input_device_set_actor (device, sequence,
                                   stage,
                                   new_cursor_actor,
                                   emit_crossing,
                                   point, time_ms);

  return new_cursor_actor;
}

/**
 * clutter_input_device_get_actor:
 * @device: a #ClutterInputDevice
 * @sequence: (allow-none): an optional #ClutterEventSequence
 *
 * Retrieves the #ClutterActor underneath the pointer or touchpoint
 * of @device and @sequence.
 *
 * Return value: (transfer none): a pointer to the #ClutterActor or %NULL
 *
 * Since: 1.2
 */
ClutterActor *
clutter_input_device_get_actor (ClutterInputDevice   *device,
                                ClutterEventSequence *sequence)
{
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  if (sequence == NULL)
    return device->cursor_actor;

  return g_hash_table_lookup (device->touch_sequence_actors, sequence);
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
 * clutter_input_device_remove_sequence:
 * @device: a #ClutterInputDevice
 * @sequence: a #ClutterEventSequence
 *
 * Stop tracking information related to a touch point.
 */
void
_clutter_input_device_remove_event_sequence (ClutterInputDevice *device,
                                             ClutterEvent       *event)
{
  ClutterEventSequence *sequence = clutter_event_get_event_sequence (event);
  ClutterActor *actor;

  actor = g_hash_table_lookup (device->touch_sequence_actors, sequence);

  if (actor != NULL)
    {
      GList *sequences =
        g_hash_table_lookup (device->inv_touch_sequence_actors, actor);
      ClutterStage *stage =
        CLUTTER_STAGE (clutter_actor_get_stage (actor));
      graphene_point_t point;

      sequences = g_list_remove (sequences, sequence);

      g_hash_table_replace (device->inv_touch_sequence_actors,
                            actor, sequences);
      clutter_event_get_coords (event, &point.x, &point.y);
      _clutter_input_device_set_actor (device, sequence, stage,
                                       NULL, TRUE, point,
                                       clutter_event_get_time (event));
      g_hash_table_remove (device->touch_sequence_actors, sequence);
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
