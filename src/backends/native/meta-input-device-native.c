/*
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <math.h>
#include <graphene-gobject.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-input-thread.h"
#include "clutter/clutter-mutter.h"

G_DEFINE_TYPE (MetaInputDeviceNative,
               meta_input_device_native,
               META_TYPE_INPUT_DEVICE)

enum
{
  PROP_0,
  PROP_DEVICE_MATRIX,
  PROP_OUTPUT_ASPECT_RATIO,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS] = { 0 };

typedef struct _PadFeature PadFeature;

struct _PadFeature
{
  ClutterInputDevicePadFeature feature;
  int n_feature;
  int group;
  gboolean mode_switch;
};

static void
meta_input_device_native_finalize (GObject *object)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (object);

  g_warn_if_fail (!device_evdev->libinput_device);

  g_clear_pointer (&device_evdev->pad_features, g_array_unref);
  g_clear_pointer (&device_evdev->modes, g_array_unref);

  G_OBJECT_CLASS (meta_input_device_native_parent_class)->finalize (object);
}

static void
meta_input_device_native_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MetaInputDeviceNative *device = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      {
        const graphene_matrix_t *matrix = g_value_get_boxed (value);
        graphene_matrix_init_identity (&device->device_matrix);
        graphene_matrix_multiply (&device->device_matrix,
                                  matrix, &device->device_matrix);
        break;
      }
    case PROP_OUTPUT_ASPECT_RATIO:
      device->output_ratio = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_input_device_native_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MetaInputDeviceNative *device = META_INPUT_DEVICE_NATIVE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MATRIX:
      g_value_set_boxed (value, &device->device_matrix);
      break;
    case PROP_OUTPUT_ASPECT_RATIO:
      g_value_set_double (value, device->output_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
meta_input_device_native_is_mode_switch_button (ClutterInputDevice *device,
                                                uint32_t            group,
                                                uint32_t            button)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  int i;

  if (!device_native->pad_features)
    return FALSE;

  for (i = 0; i < device_native->pad_features->len; i++)
    {
      PadFeature *pad_feature;

      pad_feature = &g_array_index (device_native->pad_features, PadFeature, i);

      if (pad_feature->feature == CLUTTER_PAD_FEATURE_BUTTON &&
          pad_feature->group == group &&
          pad_feature->n_feature == button)
        return pad_feature->mode_switch;
    }

  return FALSE;
}

static int
meta_input_device_native_get_group_n_modes (ClutterInputDevice *device,
                                            int                 group)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);

  if (!device_native->modes || group >= device_native->modes->len)
    return -1;

  return g_array_index (device_native->modes, int, group);
}

static gboolean
meta_input_device_native_is_grouped (ClutterInputDevice *device,
                                     ClutterInputDevice *other_device)
{
  MetaInputDeviceNative *device_native, *other_device_native;

  device_native = META_INPUT_DEVICE_NATIVE (device);
  other_device_native = META_INPUT_DEVICE_NATIVE (other_device);

  return device_native->group == other_device_native->group;
}

static int
meta_input_device_native_get_pad_feature_group (ClutterInputDevice           *device,
                                                ClutterInputDevicePadFeature  feature,
                                                int                           n_feature)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  int i;

  if (!device_native->pad_features)
    return -1;

  for (i = 0; i < device_native->pad_features->len; i++)
    {
      PadFeature *pad_feature;

      pad_feature = &g_array_index (device_native->pad_features, PadFeature, i);

      if (pad_feature->feature == feature &&
          pad_feature->n_feature == n_feature)
        return pad_feature->group;
    }

  return -1;
}

static gboolean
meta_input_device_native_get_dimensions (ClutterInputDevice *device,
                                         unsigned int       *width,
                                         unsigned int       *height)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);

  if (device_native->width > 0 && device_native->height > 0)
    {
      *width = device_native->width;
      *height = device_native->height;
      return TRUE;
    }

  return FALSE;
}

static void
meta_input_device_native_class_init (MetaInputDeviceNativeClass *klass)
{
  ClutterInputDeviceClass *device_class = CLUTTER_INPUT_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_input_device_native_finalize;
  object_class->set_property = meta_input_device_native_set_property;
  object_class->get_property = meta_input_device_native_get_property;

  device_class->is_mode_switch_button = meta_input_device_native_is_mode_switch_button;
  device_class->get_group_n_modes = meta_input_device_native_get_group_n_modes;
  device_class->is_grouped = meta_input_device_native_is_grouped;
  device_class->get_pad_feature_group = meta_input_device_native_get_pad_feature_group;
  device_class->get_dimensions = meta_input_device_native_get_dimensions;

  obj_props[PROP_DEVICE_MATRIX] =
    g_param_spec_boxed ("device-matrix", NULL, NULL,
                        GRAPHENE_TYPE_MATRIX,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);
  obj_props[PROP_OUTPUT_ASPECT_RATIO] =
    g_param_spec_double ("output-aspect-ratio", NULL, NULL,
                         0, G_MAXDOUBLE, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_input_device_native_init (MetaInputDeviceNative *self)
{
  graphene_matrix_init_identity (&self->device_matrix);
  self->device_aspect_ratio = 0;
  self->output_ratio = 0;
  self->width = -1;
  self->height = -1;
}

static void
update_pad_features (MetaInputDeviceNative *device_native)
{
  ClutterInputDevice *device = CLUTTER_INPUT_DEVICE (device_native);
  struct libinput_device *libinput_device;
  struct libinput_tablet_pad_mode_group *mode_group;
  int n_groups, n_buttons, n_rings, n_strips, n_dials, n_modes, i, j;

  libinput_device = meta_input_device_native_get_libinput_device (device);
  n_rings = libinput_device_tablet_pad_get_num_rings (libinput_device);
  n_strips = libinput_device_tablet_pad_get_num_strips (libinput_device);
  n_dials = libinput_device_tablet_pad_get_num_dials (libinput_device);
  n_groups = libinput_device_tablet_pad_get_num_mode_groups (libinput_device);
  n_buttons = libinput_device_tablet_pad_get_num_buttons (libinput_device);

  device_native->pad_features = g_array_new (FALSE, FALSE, sizeof (PadFeature));
  device_native->modes = g_array_sized_new (FALSE, FALSE, sizeof (int), n_groups);

  for (i = 0; i < n_groups; i++)
    {
      mode_group =
        libinput_device_tablet_pad_get_mode_group (libinput_device, i);

      n_modes = libinput_tablet_pad_mode_group_get_num_modes (mode_group);
      g_array_append_val (device_native->modes, n_modes);

      for (j = 0; j < n_buttons; j++)
        {
          gboolean is_mode_switch =
            libinput_tablet_pad_mode_group_button_is_toggle (mode_group, j) != 0;
          PadFeature feature = { CLUTTER_PAD_FEATURE_BUTTON, j, i, is_mode_switch };

          if (libinput_tablet_pad_mode_group_has_button (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }

      for (j = 0; j < n_rings; j++)
        {
          PadFeature feature = { CLUTTER_PAD_FEATURE_RING, j, i };

          if (libinput_tablet_pad_mode_group_has_ring (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }

      for (j = 0; j < n_strips; j++)
        {
          PadFeature feature = { CLUTTER_PAD_FEATURE_STRIP, j, i };

          if (libinput_tablet_pad_mode_group_has_strip (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }

      for (j = 0; j < n_dials; j++)
        {
          PadFeature feature = { CLUTTER_PAD_FEATURE_DIAL, j, i };

          if (libinput_tablet_pad_mode_group_has_dial (mode_group, j))
            g_array_append_val (device_native->pad_features, feature);
        }
    }
}

static ClutterInputDeviceType
determine_device_type (struct libinput_device *ldev)
{
  /* This setting is specific to touchpads and alike, only in these
   * devices there is this additional layer of touch event interpretation.
   */
  if (libinput_device_config_tap_get_finger_count (ldev) > 0)
    return CLUTTER_TOUCHPAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
    return CLUTTER_TABLET_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_PAD))
    return CLUTTER_PAD_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_POINTER))
    return CLUTTER_POINTER_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TOUCH))
    return CLUTTER_TOUCHSCREEN_DEVICE;
  else if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_KEYBOARD))
    return CLUTTER_KEYBOARD_DEVICE;
  else
    return CLUTTER_EXTENSION_DEVICE;
}

static gboolean
has_udev_property (struct udev_device *udev_device,
                   const char         *property)
{
  struct udev_device *parent_udev_device;

  if (NULL != udev_device_get_property_value (udev_device, property))
    return TRUE;

  parent_udev_device = udev_device_get_parent (udev_device);

  if (!parent_udev_device)
    return FALSE;

  return udev_device_get_property_value (parent_udev_device, property) != NULL;
}

static ClutterInputCapabilities
translate_device_capabilities (struct libinput_device *ldev)
{
  ClutterInputCapabilities caps = 0;
  struct udev_device *udev_device;

  /* This setting is specific to touchpads and alike, only in these
   * devices there is this additional layer of touch event interpretation.
   */
  if (libinput_device_config_tap_get_finger_count (ldev) > 0)
    caps |= CLUTTER_INPUT_CAPABILITY_TOUCHPAD;
  if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_TOOL))
    caps |= CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
  if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TABLET_PAD))
    caps |= CLUTTER_INPUT_CAPABILITY_TABLET_PAD;
  if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_POINTER))
    caps |= CLUTTER_INPUT_CAPABILITY_POINTER;
  if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_TOUCH))
    caps |= CLUTTER_INPUT_CAPABILITY_TOUCH;
  if (libinput_device_has_capability (ldev, LIBINPUT_DEVICE_CAP_KEYBOARD))
    caps |= CLUTTER_INPUT_CAPABILITY_KEYBOARD;

  udev_device = libinput_device_get_udev_device (ldev);

  if (udev_device)
    {
      if (has_udev_property (udev_device, "ID_INPUT_TRACKBALL"))
        caps |= CLUTTER_INPUT_CAPABILITY_TRACKBALL;
      if (has_udev_property (udev_device, "ID_INPUT_POINTINGSTICK"))
        caps |= CLUTTER_INPUT_CAPABILITY_TRACKPOINT;

      udev_device_unref (udev_device);
    }

  return caps;
}

/*
 * meta_input_device_native_new:
 * @manager: the device manager
 * @seat: the seat the device will belong to
 * @libinput_device: the libinput device
 *
 * Create a new ClutterInputDevice given a libinput device and associate
 * it with the provided seat.
 */
ClutterInputDevice *
meta_input_device_native_new_in_impl (MetaSeatImpl           *seat_impl,
                                      struct libinput_device *libinput_device)
{
  MetaInputDeviceNative *device;
  ClutterInputDeviceType type;
  ClutterInputCapabilities capabilities;
  unsigned int vendor, product, bustype;
  int n_rings = 0, n_strips = 0, n_dials = 0, n_groups = 1, n_buttons = 0;
  char *node_path;
  double width, height;

  capabilities = translate_device_capabilities (libinput_device);
  type = determine_device_type (libinput_device);
  vendor = libinput_device_get_id_vendor (libinput_device);
  product = libinput_device_get_id_product (libinput_device);
  bustype = libinput_device_get_id_bustype (libinput_device);
  node_path = g_strdup_printf ("/dev/input/%s", libinput_device_get_sysname (libinput_device));

  if (libinput_device_has_capability (libinput_device,
                                      LIBINPUT_DEVICE_CAP_TABLET_PAD))
    {
      n_rings = libinput_device_tablet_pad_get_num_rings (libinput_device);
      n_strips = libinput_device_tablet_pad_get_num_strips (libinput_device);
      n_dials = libinput_device_tablet_pad_get_num_dials (libinput_device);
      n_groups = libinput_device_tablet_pad_get_num_mode_groups (libinput_device);
      n_buttons = libinput_device_tablet_pad_get_num_buttons (libinput_device);
    }

  device = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                         "backend", meta_seat_impl_get_backend (seat_impl),
                         "name", libinput_device_get_name (libinput_device),
                         "device-type", type,
                         "capabilities", capabilities,
                         "device-mode", CLUTTER_INPUT_MODE_PHYSICAL,
                         "vendor-id", vendor,
                         "product-id", product,
                         "bus-type", bustype,
                         "n-rings", n_rings,
                         "n-strips", n_strips,
                         "n-dials", n_dials,
                         "n-mode-groups", n_groups,
                         "n-buttons", n_buttons,
                         "device-node", node_path,
                         "seat", seat_impl->seat_native,
                         NULL);

  device->libinput_device = libinput_device;

  libinput_device_set_user_data (libinput_device, device);
  libinput_device_ref (libinput_device);
  g_free (node_path);

  if (libinput_device_has_capability (libinput_device,
                                      LIBINPUT_DEVICE_CAP_TABLET_PAD))
    update_pad_features (device);

  if (libinput_device_get_size (libinput_device, &width, &height) == 0)
    {
      device->device_aspect_ratio = width / height;
      device->width = (int) width;
      device->height = (int) height;
    }

  device->group = (intptr_t) libinput_device_get_device_group (libinput_device);

  return CLUTTER_INPUT_DEVICE (device);
}

/*
 * meta_input_device_native_new_virtual_in_impl:
 * @seat: the seat the device will belong to
 * @type: the input device type
 *
 * Create a new virtual ClutterInputDevice of the given type.
 */
ClutterInputDevice *
meta_input_device_native_new_virtual_in_impl (MetaSeatImpl           *seat_impl,
                                              ClutterInputDeviceType  type,
                                              ClutterInputMode        mode)
{
  MetaInputDeviceNative *device;
  const char *name;

  switch (type)
    {
    case CLUTTER_KEYBOARD_DEVICE:
      name = "Virtual keyboard device for seat";
      break;
    case CLUTTER_POINTER_DEVICE:
      name = "Virtual pointer device for seat";
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      name = "Virtual touchscreen device for seat";
      break;
    default:
      name = "Virtual device for seat";
      break;
    };

  device = g_object_new (META_TYPE_INPUT_DEVICE_NATIVE,
                         "backend", meta_seat_impl_get_backend (seat_impl),
                         "name", name,
                         "device-type", type,
                         "device-mode", mode,
                         "seat", seat_impl->seat_native,
                         NULL);

  return CLUTTER_INPUT_DEVICE (device);
}

void
meta_input_device_native_update_leds_in_impl (MetaInputDeviceNative *device,
                                              enum libinput_led      leds)
{
  if (!device->libinput_device)
    return;

  libinput_device_led_update (device->libinput_device, leds);
}

/**
 * meta_input_device_native_get_libinput_device:
 * @device: a #ClutterInputDevice
 *
 * Retrieves the libinput_device struct held in @device.
 *
 * Returns: The libinput_device struct
 **/
struct libinput_device *
meta_input_device_native_get_libinput_device (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_evdev;

  g_return_val_if_fail (META_IS_INPUT_DEVICE_NATIVE (device), NULL);

  device_evdev = META_INPUT_DEVICE_NATIVE (device);

  return device_evdev->libinput_device;
}

void
meta_input_device_native_translate_coordinates_in_impl (ClutterInputDevice *device,
                                                        MetaViewportInfo   *viewports,
                                                        float              *x,
                                                        float              *y)
{
  MetaInputDeviceNative *device_evdev = META_INPUT_DEVICE_NATIVE (device);
  double min_x = 0, min_y = 0, max_x = 1, max_y = 1;
  float stage_width, stage_height;
  double x_d, y_d;
  graphene_point_t min_point, max_point, pos_point;

  if (device_evdev->mapping_mode == META_INPUT_DEVICE_MAPPING_RELATIVE)
    return;

  meta_viewport_info_get_extents (viewports, &stage_width, &stage_height);
  x_d = *x / stage_width;
  y_d = *y / stage_height;

  /* Apply aspect ratio */
  if (device_evdev->output_ratio > 0 &&
      device_evdev->device_aspect_ratio > 0)
    {
      double ratio = device_evdev->device_aspect_ratio / device_evdev->output_ratio;

      if (ratio > 1)
        x_d *= ratio;
      else if (ratio < 1)
        y_d *= 1 / ratio;
    }

  graphene_matrix_transform_point (&device_evdev->device_matrix,
                                   &GRAPHENE_POINT_INIT ((float) min_x,
                                                         (float) min_y),
                                   &min_point);
  min_x = min_point.x;
  min_y = min_point.y;
  graphene_matrix_transform_point (&device_evdev->device_matrix,
                                   &GRAPHENE_POINT_INIT ((float) max_x,
                                                         (float) max_y),
                                   &max_point);
  max_x = max_point.x;
  max_y = max_point.y;
  graphene_matrix_transform_point (&device_evdev->device_matrix,
                                   &GRAPHENE_POINT_INIT ((float) x_d,
                                                         (float) y_d),
                                   &pos_point);
  x_d = pos_point.x;
  y_d = pos_point.y;

  *x = (float) (CLAMP (x_d,
                       MIN (min_x, max_x),
                       MAX (min_x, max_x)) *
                stage_width);
  *y = (float) (CLAMP (y_d,
                       MIN (min_y, max_y),
                       MAX (min_y, max_y)) *
                stage_height);
}

MetaInputDeviceMapping
meta_input_device_native_get_mapping_mode_in_impl (ClutterInputDevice *device)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  ClutterInputDeviceType device_type;

  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device),
                        META_INPUT_DEVICE_MAPPING_ABSOLUTE);

  device_type = clutter_input_device_get_device_type (device);
  g_return_val_if_fail (device_type == CLUTTER_TABLET_DEVICE ||
                        device_type == CLUTTER_PEN_DEVICE ||
                        device_type == CLUTTER_ERASER_DEVICE,
                        META_INPUT_DEVICE_MAPPING_ABSOLUTE);

  return device_native->mapping_mode;
}

void
meta_input_device_native_set_mapping_mode_in_impl (ClutterInputDevice     *device,
                                                   MetaInputDeviceMapping  mapping)
{
  MetaInputDeviceNative *device_native = META_INPUT_DEVICE_NATIVE (device);
  ClutterInputDeviceType device_type;

  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));

  device_type = clutter_input_device_get_device_type (device);
  g_return_if_fail (device_type == CLUTTER_TABLET_DEVICE ||
                    device_type == CLUTTER_PEN_DEVICE ||
                    device_type == CLUTTER_ERASER_DEVICE);

  device_native->mapping_mode = mapping;
}

void
meta_input_device_native_detach_libinput_in_impl (MetaInputDeviceNative *device_native)
{
  g_clear_pointer (&device_native->libinput_device, libinput_device_unref);
}

gboolean
meta_input_device_native_has_scroll_inverted (MetaInputDeviceNative *device_native)
{
  struct libinput_device *libinput_device = device_native->libinput_device;

  if (!libinput_device)
    return FALSE;

  if (!libinput_device_config_scroll_has_natural_scroll (libinput_device))
    return FALSE;

  return !!libinput_device_config_scroll_get_natural_scroll_enabled (libinput_device);
}
