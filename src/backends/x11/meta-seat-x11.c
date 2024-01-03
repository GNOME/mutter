/*
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
#include "config.h"

#include <linux/input-event-codes.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XKB.h>

#ifdef HAVE_LIBGUDEV
#include <gudev/gudev.h>
#endif

#include "backends/meta-input-settings-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-event-x11.h"
#include "backends/x11/meta-input-device-tool-x11.h"
#include "backends/x11/meta-input-device-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-stage-x11.h"
#include "backends/x11/meta-virtual-input-device-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "clutter/clutter-mutter.h"
#include "core/bell.h"
#include "meta-seat-x11.h"
#include "mtk/mtk-x11.h"

enum
{
  PROP_0,
  PROP_BACKEND,
  PROP_OPCODE,
  PROP_POINTER_ID,
  PROP_KEYBOARD_ID,
  N_PROPS,

  /* This property is overridden */
  PROP_TOUCH_MODE,
};

typedef struct _MetaTouchInfo MetaTouchInfo;

struct _MetaTouchInfo
{
  ClutterEventSequence *sequence;
  double x;
  double y;
};

struct _MetaSeatX11
{
  ClutterSeat parent_instance;

  MetaBackend *backend;

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;
  GList *devices;
  GHashTable *devices_by_id;
  GHashTable *tools_by_serial;
  GHashTable *touch_coords;
  MetaKeymapX11 *keymap;

#ifdef HAVE_LIBGUDEV
  GUdevClient *udev_client;
#endif

  int pointer_id;
  int keyboard_id;
  int opcode;
  ClutterGrabState grab_state;
  guint has_touchscreens : 1;
  guint touch_mode : 1;
  guint has_pointer_focus : 1;
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_TYPE (MetaSeatX11, meta_seat_x11, CLUTTER_TYPE_SEAT)

static const char *clutter_input_axis_atom_names[] = {
  "Abs X",              /* CLUTTER_INPUT_AXIS_X */
  "Abs Y",              /* CLUTTER_INPUT_AXIS_Y */
  "Abs Pressure",       /* CLUTTER_INPUT_AXIS_PRESSURE */
  "Abs Tilt X",         /* CLUTTER_INPUT_AXIS_XTILT */
  "Abs Tilt Y",         /* CLUTTER_INPUT_AXIS_YTILT */
  "Abs Wheel",          /* CLUTTER_INPUT_AXIS_WHEEL */
  "Abs Distance",       /* CLUTTER_INPUT_AXIS_DISTANCE */
};

static const char *wacom_type_atoms[] = {
    "STYLUS",
    "CURSOR",
    "ERASER",
    "PAD",
    "TOUCH"
};
#define N_WACOM_TYPE_ATOMS G_N_ELEMENTS (wacom_type_atoms)

enum
{
    WACOM_TYPE_STYLUS,
    WACOM_TYPE_CURSOR,
    WACOM_TYPE_ERASER,
    WACOM_TYPE_PAD,
    WACOM_TYPE_TOUCH,
};

enum
{
  PAD_AXIS_FIRST  = 3, /* First axes are always x/y/pressure, ignored in pads */
  PAD_AXIS_STRIP1 = PAD_AXIS_FIRST,
  PAD_AXIS_STRIP2,
  PAD_AXIS_RING1,
  PAD_AXIS_RING2,
};

#define N_AXIS_ATOMS    G_N_ELEMENTS (clutter_input_axis_atom_names)

static Atom clutter_input_axis_atoms[N_AXIS_ATOMS] = { 0, };

static Display *
xdisplay_from_seat (MetaSeatX11 *seat_x11)
{
  return meta_backend_x11_get_xdisplay (META_BACKEND_X11 (seat_x11->backend));
}

static Window
root_xwindow_from_seat (MetaSeatX11 *seat_x11)
{
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (seat_x11->backend);

  return meta_backend_x11_get_root_xwindow (backend_x11);
}

static void
translate_valuator_class (Display             *xdisplay,
                          ClutterInputDevice  *device,
                          XIValuatorClassInfo *class)
{
  static gboolean atoms_initialized = FALSE;
  ClutterInputAxis i, axis = CLUTTER_INPUT_AXIS_IGNORE;

  if (G_UNLIKELY (!atoms_initialized))
    {
      XInternAtoms (xdisplay,
                    (char **) clutter_input_axis_atom_names, N_AXIS_ATOMS,
                    False,
                    clutter_input_axis_atoms);

      atoms_initialized = TRUE;
    }

  for (i = 0;
       i < N_AXIS_ATOMS;
       i += 1)
    {
      if (clutter_input_axis_atoms[i] == class->label)
        {
          axis = i + 1;
          break;
        }
    }

  meta_input_device_x11_add_axis (device, axis,
                                  class->min,
                                  class->max,
                                  class->resolution);

  g_debug ("Added axis '%s' (min:%.2f, max:%.2fd, res:%d) of device %d",
           axis == CLUTTER_INPUT_AXIS_IGNORE ?
             "Ignored" :
             clutter_input_axis_atom_names[axis - 1],
           class->min,
           class->max,
           class->resolution,
           meta_input_device_x11_get_device_id (device));
}

static void
translate_device_classes (Display             *xdisplay,
                          ClutterInputDevice  *device,
                          XIAnyClassInfo     **classes,
                          int                  n_classes)
{
  int i;

  for (i = 0; i < n_classes; i++)
    {
      XIAnyClassInfo *class_info = classes[i];

      switch (class_info->type)
        {
        case XIValuatorClass:
          translate_valuator_class (xdisplay, device,
                                    (XIValuatorClassInfo *) class_info);
          break;

        case XIScrollClass:
          {
            XIScrollClassInfo *scroll_info = (XIScrollClassInfo *) class_info;
            ClutterScrollDirection direction;

            if (scroll_info->scroll_type == XIScrollTypeVertical)
              direction = CLUTTER_SCROLL_DOWN;
            else
              direction = CLUTTER_SCROLL_RIGHT;

            g_debug ("Scroll valuator %d: %s, increment: %f",
                     scroll_info->number,
                     scroll_info->scroll_type == XIScrollTypeVertical
                     ? "vertical"
                     : "horizontal",
                     scroll_info->increment);

            meta_input_device_x11_add_scroll_info (device,
                                                   scroll_info->number,
                                                   direction,
                                                   scroll_info->increment);
          }
          break;

        default:
          break;
        }
    }
}

static gboolean
is_touch_device (XIAnyClassInfo           **classes,
                 int                        n_classes,
                 ClutterInputDeviceType    *device_type,
                 ClutterInputCapabilities  *capabilities,
                 uint32_t                  *n_touch_points)
{
  int i;

  for (i = 0; i < n_classes; i++)
    {
      XITouchClassInfo *class = (XITouchClassInfo *) classes[i];

      if (class->type != XITouchClass)
        continue;

      if (class->num_touches > 0)
        {
          if (class->mode == XIDirectTouch)
            {
              *device_type = CLUTTER_TOUCHSCREEN_DEVICE;
              *capabilities = CLUTTER_INPUT_CAPABILITY_TOUCH;
            }
          else if (class->mode == XIDependentTouch)
            {
              *device_type = CLUTTER_TOUCHPAD_DEVICE;
              *capabilities =
                CLUTTER_INPUT_CAPABILITY_POINTER |
                CLUTTER_INPUT_CAPABILITY_TOUCHPAD;
            }
          else
            {
              continue;
            }

          *n_touch_points = class->num_touches;

          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
is_touchpad_device (MetaSeatX11  *seat_x11,
                    XIDeviceInfo *info)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;
  Atom prop;

  prop = XInternAtom (xdisplay,
                      "libinput Tapping Enabled", True);
  if (prop == None)
    return FALSE;

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGetProperty (xdisplay,
                      info->deviceid,
                      prop,
                      0, 1, False, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  mtk_x11_error_trap_pop (xdisplay);

  /* We don't care about the data */
  XFree (data);

  if (rc != Success || type != XA_INTEGER || format != 8 || nitems != 1)
    return FALSE;

  return TRUE;
}

static gboolean
get_device_ids (MetaSeatX11   *seat_x11,
                XIDeviceInfo  *info,
                char         **vendor_id,
                char         **product_id)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGetProperty (xdisplay,
                      info->deviceid,
                      XInternAtom (xdisplay, "Device Product ID", False),
                      0, 2, False, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  mtk_x11_error_trap_pop (xdisplay);

  if (rc != Success || type != XA_INTEGER || format != 32 || nitems != 2)
    {
      XFree (data);
      return FALSE;
    }

  if (vendor_id)
    *vendor_id = g_strdup_printf ("%.4x", data[0]);
  if (product_id)
    *product_id = g_strdup_printf ("%.4x", data[1]);

  XFree (data);

  return TRUE;
}

static char *
get_device_node_path (MetaSeatX11  *seat_x11,
                      XIDeviceInfo *info)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  gulong nitems, bytes_after;
  guchar *data;
  int rc, format;
  Atom prop, type;
  char *node_path;

  prop = XInternAtom (xdisplay, "Device Node", False);
  if (prop == None)
    return NULL;

  mtk_x11_error_trap_push (xdisplay);

  rc = XIGetProperty (xdisplay,
                      info->deviceid, prop, 0, 1024, False,
                      XA_STRING, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);

  if (mtk_x11_error_trap_pop_with_return (xdisplay))
    return NULL;

  if (rc != Success || type != XA_STRING || format != 8)
    {
      XFree (data);
      return FALSE;
    }

  node_path = g_strdup ((char *) data);
  XFree (data);

  return node_path;
}

static void
get_pad_features (XIDeviceInfo *info,
                  uint32_t     *n_rings,
                  uint32_t     *n_strips)
{
  int i, rings = 0, strips = 0;

  for (i = PAD_AXIS_FIRST; i < info->num_classes; i++)
    {
      XIValuatorClassInfo *valuator = (XIValuatorClassInfo*) info->classes[i];
      int axis = valuator->number;

      if (valuator->type != XIValuatorClass)
        continue;
      if (valuator->max <= 1)
        continue;

      /* Ring/strip axes are fixed in pad devices as handled by the
       * wacom driver. Match those to detect pad features.
       */
      if (axis == PAD_AXIS_STRIP1 || axis == PAD_AXIS_STRIP2)
        strips++;
      else if (axis == PAD_AXIS_RING1 || axis == PAD_AXIS_RING2)
        rings++;
    }

  *n_rings = rings;
  *n_strips = strips;
}

/* The Wacom driver exports the tool type as property. Use that over
   guessing based on the device name */
static gboolean
guess_source_from_wacom_type (MetaSeatX11              *seat_x11,
                              XIDeviceInfo             *info,
                              ClutterInputDeviceType   *source_out,
                              ClutterInputCapabilities *capabilities_out)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int rc, format;
  Atom type;
  Atom prop;
  Atom device_type;
  Atom types[N_WACOM_TYPE_ATOMS];

  prop = XInternAtom (xdisplay, "Wacom Tool Type", True);
  if (prop == None)
    return FALSE;

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGetProperty (xdisplay,
                      info->deviceid,
                      prop,
                      0, 1, False, XA_ATOM, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  mtk_x11_error_trap_pop (xdisplay);

  if (rc != Success || type != XA_ATOM || format != 32 || nitems != 1)
    {
      XFree (data);
      return FALSE;
    }

  device_type = *data;
  XFree (data);

  if (device_type == 0)
      return FALSE;

  rc = XInternAtoms (xdisplay,
                     (char **)wacom_type_atoms,
                     N_WACOM_TYPE_ATOMS,
                     False,
                     types);
  if (rc == 0)
      return FALSE;

  if (device_type == types[WACOM_TYPE_STYLUS])
    {
      *source_out = CLUTTER_PEN_DEVICE;
      *capabilities_out = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
    }
  else if (device_type == types[WACOM_TYPE_CURSOR])
    {
      *source_out = CLUTTER_CURSOR_DEVICE;
      *capabilities_out = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
    }
  else if (device_type == types[WACOM_TYPE_ERASER])
    {
      *source_out = CLUTTER_ERASER_DEVICE;
      *capabilities_out = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
    }
  else if (device_type == types[WACOM_TYPE_PAD])
    {
      *source_out = CLUTTER_PAD_DEVICE;
      *capabilities_out = CLUTTER_INPUT_CAPABILITY_TABLET_PAD;
    }
  else if (device_type == types[WACOM_TYPE_TOUCH])
    {
        uint32_t num_touches = 0;

        if (!is_touch_device (info->classes, info->num_classes,
                              source_out, capabilities_out, &num_touches))
          {
            *source_out = CLUTTER_TOUCHSCREEN_DEVICE;
            *capabilities_out = CLUTTER_INPUT_CAPABILITY_TOUCH;
          }
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}

#ifdef HAVE_LIBGUDEV
static gboolean
has_udev_property (GUdevDevice *udev_device,
                   const char  *property_name)
{
  g_autoptr (GUdevDevice) parent_udev_device = NULL;

  if (NULL != g_udev_device_get_property (udev_device, property_name))
    return TRUE;

  parent_udev_device = g_udev_device_get_parent (udev_device);

  if (!parent_udev_device)
    return FALSE;

  return g_udev_device_get_property (parent_udev_device, property_name) != NULL;
}
#endif

static ClutterInputDevice *
create_device (MetaSeatX11    *seat_x11,
               ClutterBackend *clutter_backend,
               XIDeviceInfo   *info)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  ClutterInputDeviceType source, touch_source;
  ClutterInputCapabilities capabilities = 0;
  ClutterInputDevice *retval;
  ClutterInputMode mode;
  uint32_t num_touches = 0, num_rings = 0, num_strips = 0;
  char *vendor_id = NULL, *product_id = NULL, *node_path = NULL;

  if (info->use == XIMasterKeyboard || info->use == XISlaveKeyboard)
    {
      source = CLUTTER_KEYBOARD_DEVICE;
      capabilities = CLUTTER_INPUT_CAPABILITY_KEYBOARD;
    }
  else if (is_touchpad_device (seat_x11, info))
    {
      source = CLUTTER_TOUCHPAD_DEVICE;
    }
  else if (info->use == XISlavePointer &&
           is_touch_device (info->classes, info->num_classes,
                            &touch_source, &capabilities,
                            &num_touches))
    {
      source = touch_source;
    }
  else if (!guess_source_from_wacom_type (seat_x11, info, &source, &capabilities))
    {
      char *name;

      name = g_ascii_strdown (info->name, -1);

      if (strstr (name, "eraser") != NULL)
        {
          source = CLUTTER_ERASER_DEVICE;
          capabilities = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
        }
      else if (strstr (name, "cursor") != NULL)
        {
          source = CLUTTER_CURSOR_DEVICE;
          capabilities = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
        }
      else if (strstr (name, " pad") != NULL)
        {
          source = CLUTTER_PAD_DEVICE;
          capabilities = CLUTTER_INPUT_CAPABILITY_TABLET_PAD;
        }
      else if (strstr (name, "wacom") != NULL || strstr (name, "pen") != NULL)
        {
          source = CLUTTER_PEN_DEVICE;
          capabilities = CLUTTER_INPUT_CAPABILITY_TABLET_TOOL;
        }
      else if (strstr (name, "touchpad") != NULL)
        {
          source = CLUTTER_TOUCHPAD_DEVICE;
          capabilities =
            CLUTTER_INPUT_CAPABILITY_POINTER |
            CLUTTER_INPUT_CAPABILITY_TOUCHPAD;
        }
      else
        {
          source = CLUTTER_POINTER_DEVICE;
          capabilities = CLUTTER_INPUT_CAPABILITY_POINTER;
        }

      g_free (name);
    }

  switch (info->use)
    {
    case XIMasterKeyboard:
    case XIMasterPointer:
      mode = CLUTTER_INPUT_MODE_LOGICAL;
      break;

    case XISlaveKeyboard:
    case XISlavePointer:
      mode = CLUTTER_INPUT_MODE_PHYSICAL;
      break;

    case XIFloatingSlave:
    default:
      mode = CLUTTER_INPUT_MODE_FLOATING;
      break;
    }

  if (info->use != XIMasterKeyboard &&
      info->use != XIMasterPointer)
    {
      get_device_ids (seat_x11, info, &vendor_id, &product_id);
      node_path = get_device_node_path (seat_x11, info);
    }

#ifdef HAVE_LIBGUDEV
  if (node_path)
    {
      g_autoptr (GUdevDevice) udev_device = NULL;

      udev_device = g_udev_client_query_by_device_file (seat_x11->udev_client,
                                                        node_path);
      if (udev_device)
        {
          if (has_udev_property (udev_device, "ID_INPUT_TRACKBALL"))
            capabilities |= CLUTTER_INPUT_CAPABILITY_TRACKBALL;
          if (has_udev_property (udev_device, "ID_INPUT_POINTINGSTICK"))
            capabilities |= CLUTTER_INPUT_CAPABILITY_TRACKPOINT;
        }
    }
#endif

  if (source == CLUTTER_PAD_DEVICE)
    get_pad_features (info, &num_rings, &num_strips);

  retval = g_object_new (META_TYPE_INPUT_DEVICE_X11,
                         "backend", seat_x11->backend,
                         "name", info->name,
                         "id", info->deviceid,
                         "has-cursor", (info->use == XIMasterPointer),
                         "device-type", source,
                         "capabilities", capabilities,
                         "device-mode", mode,
                         "vendor-id", vendor_id,
                         "product-id", product_id,
                         "device-node", node_path,
                         "n-rings", num_rings,
                         "n-strips", num_strips,
                         "n-mode-groups", MAX (num_rings, num_strips),
                         "seat", seat_x11,
                         NULL);

  translate_device_classes (xdisplay, retval,
                            info->classes,
                            info->num_classes);

  g_free (vendor_id);
  g_free (product_id);
  g_free (node_path);

  g_debug ("Created device '%s' (id: %d, has-cursor: %s)",
           info->name,
           info->deviceid,
           info->use == XIMasterPointer ? "yes" : "no");

  return retval;
}

static void
pad_passive_button_grab (MetaSeatX11        *seat_x11,
                         ClutterInputDevice *device)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  Window root_xwindow = root_xwindow_from_seat (seat_x11);
  XIGrabModifiers xi_grab_mods = { XIAnyModifier, };
  XIEventMask xi_event_mask;
  int device_id, rc;

  device_id = meta_input_device_x11_get_device_id (device);

  xi_event_mask.deviceid = device_id;
  xi_event_mask.mask_len = XIMaskLen (XI_LASTEVENT);
  xi_event_mask.mask = g_new0 (unsigned char, xi_event_mask.mask_len);

  XISetMask (xi_event_mask.mask, XI_Motion);
  XISetMask (xi_event_mask.mask, XI_ButtonPress);
  XISetMask (xi_event_mask.mask, XI_ButtonRelease);

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGrabButton (xdisplay,
                     device_id, XIAnyButton,
                     root_xwindow, None,
                     XIGrabModeSync, XIGrabModeSync,
                     True, &xi_event_mask, 1, &xi_grab_mods);
  if (rc != 0)
    {
      g_warning ("Could not passively grab pad device: %s",
                 clutter_input_device_get_device_name (device));
    }
  else
    {
      XIAllowEvents (xdisplay, device_id, XIAsyncDevice, CLUTTER_CURRENT_TIME);
    }

  mtk_x11_error_trap_pop (xdisplay);

  g_free (xi_event_mask.mask);
}

static void
update_touch_mode (MetaSeatX11 *seat_x11)
{
  gboolean touch_mode;

  touch_mode = seat_x11->has_touchscreens;

  if (seat_x11->touch_mode == touch_mode)
    return;

  seat_x11->touch_mode = touch_mode;
  g_object_notify (G_OBJECT (seat_x11), "touch-mode");
}

static ClutterInputDevice *
add_device (MetaSeatX11    *seat_x11,
            ClutterBackend *clutter_backend,
            XIDeviceInfo   *info)
{
  ClutterInputDevice *device;

  device = create_device (seat_x11, clutter_backend, info);

  g_hash_table_replace (seat_x11->devices_by_id,
                        GINT_TO_POINTER (info->deviceid),
                        device);

  if (info->use == XIMasterPointer &&
      info->deviceid == seat_x11->pointer_id)
    {
      seat_x11->core_pointer = device;
    }
  else if (info->use == XIMasterKeyboard &&
           info->deviceid == seat_x11->keyboard_id)
    {
      seat_x11->core_keyboard = device;
    }
  else if ((info->use == XISlavePointer &&
            info->attachment == seat_x11->pointer_id) ||
           (info->use == XISlaveKeyboard &&
            info->attachment == seat_x11->keyboard_id))
    {
      seat_x11->devices = g_list_prepend (seat_x11->devices, device);
    }
  else
    {
      g_warning ("Unhandled device: %s",
                 clutter_input_device_get_device_name (device));
    }

  if (clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE)
    pad_passive_button_grab (seat_x11, device);

  return device;
}

static gboolean
has_touchscreens (MetaSeatX11 *seat_x11)
{
  GList *l;

  for (l = seat_x11->devices; l; l = l->next)
    {
      if (clutter_input_device_get_device_type (l->data) == CLUTTER_TOUCHSCREEN_DEVICE)
        return TRUE;
    }

  return FALSE;
}

static void
remove_device (MetaSeatX11        *seat_x11,
               ClutterInputDevice *device)
{
  if (seat_x11->core_pointer == device)
    {
      seat_x11->core_pointer = NULL;
    }
  else if (seat_x11->core_keyboard == device)
    {
      seat_x11->core_keyboard = NULL;
    }
  else
    {
      seat_x11->devices = g_list_remove (seat_x11->devices, device);
    }
}

static gboolean
meta_seat_x11_handle_event_post (ClutterSeat        *seat,
                                 const ClutterEvent *event)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  ClutterInputDevice *device;
  MetaInputSettings *input_settings;
  ClutterEventType event_type;
  gboolean is_touch;

  event_type = clutter_event_type (event);

  if (event_type != CLUTTER_DEVICE_ADDED &&
      event_type != CLUTTER_DEVICE_REMOVED)
    return TRUE;

  device = clutter_event_get_device (event);
  is_touch =
    clutter_input_device_get_device_type (device) == CLUTTER_TOUCHSCREEN_DEVICE;
  input_settings = meta_backend_get_input_settings (seat_x11->backend);

  switch (event_type)
    {
      case CLUTTER_DEVICE_ADDED:
        meta_input_settings_add_device (input_settings, device);
        seat_x11->has_touchscreens |= is_touch;
        break;
      case CLUTTER_DEVICE_REMOVED:
        if (is_touch)
          seat_x11->has_touchscreens = has_touchscreens (seat_x11);
        meta_input_settings_remove_device (input_settings, device);
        break;
      default:
        break;
    }

  if (is_touch)
    update_touch_mode (seat_x11);

  return TRUE;
}

static uint
device_get_tool_serial (MetaSeatX11        *seat_x11,
                        ClutterInputDevice *device)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  gulong nitems, bytes_after;
  uint32_t *data = NULL;
  int serial_id = 0;
  int rc, format;
  Atom type;
  Atom prop;

  prop = XInternAtom (xdisplay, "Wacom Serial IDs", True);
  if (prop == None)
    return 0;

  mtk_x11_error_trap_push (xdisplay);
  rc = XIGetProperty (xdisplay,
                      meta_input_device_x11_get_device_id (device),
                      prop, 0, 4, FALSE, XA_INTEGER, &type, &format, &nitems, &bytes_after,
                      (guchar **) &data);
  mtk_x11_error_trap_pop (xdisplay);

  if (rc == Success && type == XA_INTEGER && format == 32 && nitems >= 4)
    serial_id = data[3];

  XFree (data);

  return serial_id;
}

static ClutterEvent *
translate_hierarchy_event (ClutterBackend   *clutter_backend,
                           MetaSeatX11      *seat_x11,
                           XIHierarchyEvent *ev)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  int i;
  ClutterEvent *event = NULL;

  for (i = 0; i < ev->num_info; i++)
    {
      if (ev->info[i].flags & XIDeviceEnabled &&
          !g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (ev->info[i].deviceid)))
        {
          XIDeviceInfo *info;
          int n_devices;

          g_debug ("Hierarchy event: device enabled");

          mtk_x11_error_trap_push (xdisplay);
          info = XIQueryDevice (xdisplay,
                                ev->info[i].deviceid,
                                &n_devices);
          mtk_x11_error_trap_pop (xdisplay);
          if (info != NULL)
            {
              ClutterInputDevice *device;

              device = add_device (seat_x11, clutter_backend, &info[0]);

              event = clutter_event_device_notify_new (CLUTTER_DEVICE_ADDED,
                                                       CLUTTER_EVENT_NONE,
                                                       ms2us (ev->time),
                                                       device);
              XIFreeDeviceInfo (info);
            }
        }
      else if (ev->info[i].flags & XIDeviceDisabled)
        {
          g_autoptr (ClutterInputDevice) device = NULL;
          g_debug ("Hierarchy event: device disabled");

          g_hash_table_steal_extended (seat_x11->devices_by_id,
                                       GINT_TO_POINTER (ev->info[i].deviceid),
                                       NULL,
                                       (gpointer) &device);

          if (device != NULL)
            {
              remove_device (seat_x11, device);

              event = clutter_event_device_notify_new (CLUTTER_DEVICE_REMOVED,
                                                       CLUTTER_EVENT_NONE,
                                                       ms2us (ev->time),
                                                       device);
            }
        }
      else if ((ev->info[i].flags & XISlaveAttached) ||
               (ev->info[i].flags & XISlaveDetached))
        {
          g_debug ("Hierarchy event: physical device %s",
                   (ev->info[i].flags & XISlaveAttached)
                   ? "attached"
                   : "detached");
        }
    }

  return event;
}

static void
translate_property_event (MetaSeatX11 *seat_x11,
                          XIEvent     *event)
{
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  XIPropertyEvent *xev = (XIPropertyEvent *) event;
  Atom serial_ids_prop;
  ClutterInputDevice *device;

  serial_ids_prop = XInternAtom (xdisplay, "Wacom Serial IDs", True);
  if (serial_ids_prop == None)
    return;

  device = g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (xev->deviceid));
  if (!device)
    return;

  if (xev->property == serial_ids_prop)
    {
      ClutterInputDeviceTool *tool = NULL;
      ClutterInputDeviceToolType type;
      MetaInputSettings *input_settings;
      int serial_id;

      serial_id = device_get_tool_serial (seat_x11, device);

      if (serial_id != 0)
        {
          tool = g_hash_table_lookup (seat_x11->tools_by_serial,
                                      GUINT_TO_POINTER (serial_id));
          if (!tool)
            {
              type = clutter_input_device_get_device_type (device) == CLUTTER_ERASER_DEVICE ?
                CLUTTER_INPUT_DEVICE_TOOL_ERASER : CLUTTER_INPUT_DEVICE_TOOL_PEN;
              tool = meta_input_device_tool_x11_new (serial_id, type);
              g_hash_table_insert (seat_x11->tools_by_serial,
                                   GUINT_TO_POINTER (serial_id),
                                   tool);
            }
        }

      meta_input_device_x11_update_tool (device, tool);
      input_settings = meta_backend_get_input_settings (seat_x11->backend);
      meta_input_settings_notify_tool_change (input_settings, device, tool);
    }
}

static void
emulate_motion (MetaSeatX11 *seat_x11,
                double       x,
                double       y)
{
  ClutterInputDevice *pointer;
  ClutterEvent *event;

  pointer = clutter_seat_get_pointer (CLUTTER_SEAT (seat_x11));

  event = clutter_event_motion_new (CLUTTER_EVENT_FLAG_SYNTHETIC,
                                    CLUTTER_CURRENT_TIME,
                                    pointer,
                                    NULL, 0,
                                    GRAPHENE_POINT_INIT (x, y),
                                    GRAPHENE_POINT_INIT (0, 0),
                                    GRAPHENE_POINT_INIT (0, 0),
                                    GRAPHENE_POINT_INIT (0, 0),
                                    NULL);

  clutter_event_put (event);
  clutter_event_free (event);
}

static void
translate_raw_event (MetaSeatX11 *seat_x11,
                     XEvent      *xevent)
{
  ClutterInputDevice *device;
  XGenericEventCookie *cookie;
  XIEvent *xi_event;
  XIRawEvent *xev;
  float x,y;

  cookie = &xevent->xcookie;
  xi_event = (XIEvent *) cookie->data;
  xev = (XIRawEvent *) xi_event;

  device = g_hash_table_lookup (seat_x11->devices_by_id,
                                GINT_TO_POINTER (xev->deviceid));
  if (device == NULL)
    return;

  switch (cookie->evtype)
    {
    case XI_RawMotion:
      g_debug ("raw motion: device:%d '%s'",
               meta_input_device_x11_get_device_id (device),
               clutter_input_device_get_device_name (device));

      /* We don't get actual pointer location with raw events, and we cannot
       * rely on `clutter_input_device_get_coords()` either because of
       * unreparented toplevels (like all client-side decoration windows),
       * so we need to explicitly query the pointer here...
       */
      if (meta_input_device_x11_get_pointer_location (device, &x, &y))
        {
          if (_clutter_is_input_pointer_a11y_enabled (device))
            _clutter_input_pointer_a11y_on_motion_event (device, x, y);
          if (!seat_x11->has_pointer_focus)
            emulate_motion (seat_x11, x, y);
        }
      break;
    case XI_RawButtonPress:
    case XI_RawButtonRelease:
      g_debug ("raw button %s: device:%d '%s' button %i",
               cookie->evtype == XI_RawButtonPress
               ? "press  "
               : "release",
               meta_input_device_x11_get_device_id (device),
               clutter_input_device_get_device_name (device),
               xev->detail);
      if (_clutter_is_input_pointer_a11y_enabled (device))
        {
          _clutter_input_pointer_a11y_on_button_event (device,
                                                       xev->detail,
                                                       (cookie->evtype == XI_RawButtonPress));
        }
      break;
    }
}

static gboolean
translate_pad_axis (ClutterInputDevice *device,
                    XIValuatorState    *valuators,
                    ClutterEventType   *evtype,
                    uint32_t           *number,
                    double             *value)
{
  double *values;
  int i;

  values = valuators->values;

  for (i = PAD_AXIS_FIRST; i < valuators->mask_len * 8; i++)
    {
      double val;
      uint32_t axis_number = 0;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;

      val = *values++;
      if (val <= 0)
        continue;

      meta_input_device_x11_translate_axis (device, i, val, value);

      if (i == PAD_AXIS_RING1 || i == PAD_AXIS_RING2)
        {
          *evtype = CLUTTER_PAD_RING;
          (*value) *= 360.0;
        }
      else if (i == PAD_AXIS_STRIP1 || i == PAD_AXIS_STRIP2)
        {
          *evtype = CLUTTER_PAD_STRIP;
        }
      else
        continue;

      if (i == PAD_AXIS_STRIP2 || i == PAD_AXIS_RING2)
        axis_number++;

      *number = axis_number;
      return TRUE;
    }

  return FALSE;
}

static ClutterEvent *
translate_pad_axis_event (XIDeviceEvent      *xev,
                          ClutterInputDevice *device)
{
  double value;
  uint32_t number, mode = 0;
  ClutterEventType evtype;
  ClutterEvent *event = NULL;

  if (!translate_pad_axis (device, &xev->valuators,
                           &evtype, &number, &value))
    return FALSE;

  /* When touching a ring/strip a first XI_Motion event
   * is generated. Use it to reset the pad state, so
   * later events actually have a directionality.
   */
  if (xev->evtype == XI_Motion)
    value = -1;

#ifdef HAVE_LIBWACOM
  mode = meta_input_device_x11_get_pad_group_mode (device, number);
#endif

  if (evtype == CLUTTER_PAD_RING)
    {
      event = clutter_event_pad_ring_new (CLUTTER_EVENT_NONE,
                                          ms2us (xev->time),
                                          device,
                                          CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN,
                                          number,
                                          0,
                                          value,
                                          mode);
    }
  else
    {
      event = clutter_event_pad_strip_new (CLUTTER_EVENT_NONE,
                                           ms2us (xev->time),
                                           device,
                                           CLUTTER_INPUT_DEVICE_PAD_SOURCE_UNKNOWN,
                                           number,
                                           0,
                                           value,
                                           mode);
    }

  g_debug ("%s: win:0x%x, device:%d '%s', time:%lu "
           "(value:%f)",
           evtype == CLUTTER_PAD_RING
           ? "pad ring  "
           : "pad strip",
           (unsigned int) xev->event,
           meta_input_device_x11_get_device_id (device),
           clutter_input_device_get_device_name (device),
           xev->time, value);

  return event;
}

static ClutterStage *
get_event_stage (MetaSeatX11 *seat_x11,
                 XIEvent     *xi_event)
{
  Window xwindow = None;

  switch (xi_event->evtype)
    {
    case XI_KeyPress:
    case XI_KeyRelease:
    case XI_ButtonPress:
    case XI_ButtonRelease:
    case XI_Motion:
    case XI_TouchBegin:
    case XI_TouchUpdate:
    case XI_TouchEnd:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;

        xwindow = xev->event;
      }
      break;

    case XI_Enter:
    case XI_Leave:
    case XI_FocusIn:
    case XI_FocusOut:
      {
        XIEnterEvent *xev = (XIEnterEvent *) xi_event;

        xwindow = xev->event;
      }
      break;

    case XI_HierarchyChanged:
      return CLUTTER_STAGE (meta_backend_get_stage (seat_x11->backend));

    default:
      break;
    }

  if (xwindow == None)
    return NULL;

  return meta_x11_get_stage_from_window (xwindow);
}

/*
 * print_key_sym: Translate a symbol to its printable form if any
 * @symbol: the symbol to translate
 * @buffer: the buffer where to put the translated string
 * @len: size of the buffer
 *
 * Translates @symbol into a printable representation in @buffer, if possible.
 *
 * Return value: The number of bytes of the translated string, 0 if the
 *               symbol can't be printed
 *
 * Note: The code is derived from libX11's src/KeyBind.c
 *       Copyright 1985, 1987, 1998  The Open Group
 *
 * Note: This code works for Latin-1 symbols. clutter_keysym_to_unicode()
 *       does the work for the other keysyms.
 */
static int
print_keysym (uint32_t symbol,
              char    *buffer,
              int      len)
{
  unsigned long high_bytes;
  unsigned char c;

  high_bytes = symbol >> 8;
  if (!(len &&
        ((high_bytes == 0) ||
         ((high_bytes == 0xFF) &&
          (((symbol >= CLUTTER_KEY_BackSpace) &&
            (symbol <= CLUTTER_KEY_Clear)) ||
           (symbol == CLUTTER_KEY_Return) ||
           (symbol == CLUTTER_KEY_Escape) ||
           (symbol == CLUTTER_KEY_KP_Space) ||
           (symbol == CLUTTER_KEY_KP_Tab) ||
           (symbol == CLUTTER_KEY_KP_Enter) ||
           ((symbol >= CLUTTER_KEY_KP_Multiply) &&
            (symbol <= CLUTTER_KEY_KP_9)) ||
           (symbol == CLUTTER_KEY_KP_Equal) ||
           (symbol == CLUTTER_KEY_Delete))))))
    return 0;

  /* if X keysym, convert to ascii by grabbing low 7 bits */
  if (symbol == CLUTTER_KEY_KP_Space)
    c = CLUTTER_KEY_space & 0x7F; /* patch encoding botch */
  else if (high_bytes == 0xFF)
    c = symbol & 0x7F;
  else
    c = symbol & 0xFF;

  buffer[0] = c;
  return 1;
}

static double *
translate_axes (ClutterInputDevice *device,
                double              x,
                double              y,
                XIValuatorState    *valuators)
{
  uint32_t i;
  double *retval;
  double *values;

  retval = g_new0 (double, CLUTTER_INPUT_AXIS_LAST);
  values = valuators->values;

  for (i = 0; i < valuators->mask_len * 8; i++)
    {
      ClutterInputAxis axis;
      double val;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;
      if (!meta_input_device_x11_get_axis (device, i, &axis))
        continue;

      val = *values++;

      switch (axis)
        {
        case CLUTTER_INPUT_AXIS_X:
          retval[axis] = x;
          break;

        case CLUTTER_INPUT_AXIS_Y:
          retval[axis] = y;
          break;

        default:
          meta_input_device_x11_translate_axis (device, i, val, &retval[axis]);
          break;
        }
    }

  return retval;
}

static double
scroll_valuators_changed (ClutterInputDevice *device,
                          XIValuatorState    *valuators,
                          double             *dx_p,
                          double             *dy_p)
{
  gboolean retval = FALSE;
  uint32_t n_axes, n_val, i;
  double *values;

  n_axes = meta_input_device_x11_get_n_axes (device);
  values = valuators->values;

  *dx_p = *dy_p = 0.0;

  n_val = 0;

  for (i = 0; i < MIN (valuators->mask_len * 8, n_axes); i++)
    {
      ClutterScrollDirection direction;
      double delta;

      if (!XIMaskIsSet (valuators->mask, i))
        continue;

      if (meta_input_device_x11_get_scroll_delta (device, i,
                                                  values[n_val],
                                                  &direction,
                                                  &delta))
        {
          retval = TRUE;

          if (direction == CLUTTER_SCROLL_UP ||
              direction == CLUTTER_SCROLL_DOWN)
            *dy_p = delta;
          else
            *dx_p = delta;
        }

      n_val += 1;
    }

  return retval;
}

static void
translate_coords (MetaStageX11 *stage_x11,
                  double        event_x,
                  double        event_y,
                  float        *x_out,
                  float        *y_out)
{
  MetaStageImpl *stage_impl = META_STAGE_IMPL (stage_x11);
  ClutterActor *stage = CLUTTER_ACTOR (stage_impl->wrapper);
  float stage_width;
  float stage_height;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  *x_out = CLAMP (event_x, 0, stage_width);
  *y_out = CLAMP (event_y, 0, stage_height);
}

static void
on_keymap_state_change (MetaKeymapX11 *keymap_x11,
                        gpointer       data)
{
  ClutterSeat *seat = data;
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  MetaInputSettings *input_settings;
  MetaKbdA11ySettings kbd_a11y_settings;

  /* On keymaps state change, just reapply the current settings, it'll
   * take care of enabling/disabling mousekeys based on NumLock state.
   */
  input_settings = meta_backend_get_input_settings (seat_x11->backend);
  meta_input_settings_get_kbd_a11y_settings (input_settings, &kbd_a11y_settings);
  meta_seat_x11_apply_kbd_a11y_settings (seat, &kbd_a11y_settings);
}

static void
meta_seat_x11_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      seat_x11->backend = g_value_get_object (value);
      break;
    case PROP_OPCODE:
      seat_x11->opcode = g_value_get_int (value);
      break;
    case PROP_POINTER_ID:
      seat_x11->pointer_id = g_value_get_int (value);
      break;
    case PROP_KEYBOARD_ID:
      seat_x11->keyboard_id = g_value_get_int (value);
      break;
    case PROP_TOUCH_MODE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_seat_x11_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, seat_x11->backend);
      break;
    case PROP_OPCODE:
      g_value_set_int (value, seat_x11->opcode);
      break;
    case PROP_POINTER_ID:
      g_value_set_int (value, seat_x11->pointer_id);
      break;
    case PROP_KEYBOARD_ID:
      g_value_set_int (value, seat_x11->keyboard_id);
      break;
    case PROP_TOUCH_MODE:
      g_value_set_boolean (value, seat_x11->touch_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

void
meta_seat_x11_notify_devices (MetaSeatX11  *seat_x11,
			      ClutterStage *stage)
{
  GHashTableIter iter;
  ClutterInputDevice *device;

  g_hash_table_iter_init (&iter, seat_x11->devices_by_id);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
    {
      ClutterEvent *event;

      event = clutter_event_device_notify_new (CLUTTER_DEVICE_ADDED,
                                               CLUTTER_EVENT_NONE,
                                               CLUTTER_CURRENT_TIME,
                                               device);
      clutter_event_put (event);
      clutter_event_free (event);
    }
}

static void
meta_seat_x11_constructed (GObject *object)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  Window root_xwindow = root_xwindow_from_seat (seat_x11);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (seat_x11->backend);
  XIDeviceInfo *info;
  XIEventMask event_mask;
  unsigned char mask[XIMaskLen(XI_LASTEVENT)] = { 0, };
  int n_devices, i;
#ifdef HAVE_LIBGUDEV
  const char *udev_subsystems[] = { "input", NULL };

  seat_x11->udev_client = g_udev_client_new (udev_subsystems);
#endif

  info = XIQueryDevice (xdisplay, XIAllDevices, &n_devices);

  for (i = 0; i < n_devices; i++)
    {
      XIDeviceInfo *xi_device = &info[i];

      if (!xi_device->enabled)
        continue;

      add_device (seat_x11, clutter_backend, xi_device);
    }

  XIFreeDeviceInfo (info);

  XISetMask (mask, XI_HierarchyChanged);
  XISetMask (mask, XI_DeviceChanged);
  XISetMask (mask, XI_PropertyEvent);

  event_mask.deviceid = XIAllDevices;
  event_mask.mask_len = sizeof (mask);
  event_mask.mask = mask;

  XISelectEvents (xdisplay, root_xwindow,
                  &event_mask, 1);

  memset(mask, 0, sizeof (mask));
  XISetMask (mask, XI_RawMotion);
  XISetMask (mask, XI_RawButtonPress);
  XISetMask (mask, XI_RawButtonRelease);

  if (meta_backend_x11_get_barriers (META_BACKEND_X11 (seat_x11->backend)))
    {
      XISetMask (mask, XI_BarrierHit);
      XISetMask (mask, XI_BarrierLeave);
    }

  event_mask.deviceid = XIAllMasterDevices;
  event_mask.mask_len = sizeof (mask);
  event_mask.mask = mask;

  XISelectEvents (xdisplay, root_xwindow,
                  &event_mask, 1);

  XSync (xdisplay, False);

  seat_x11->keymap = g_object_new (META_TYPE_KEYMAP_X11,
                                   "backend", seat_x11->backend,
                                   NULL);
  g_signal_connect (seat_x11->keymap,
                    "state-changed",
                    G_CALLBACK (on_keymap_state_change),
                    seat_x11);

  meta_seat_x11_a11y_init (CLUTTER_SEAT (seat_x11));

  if (G_OBJECT_CLASS (meta_seat_x11_parent_class)->constructed)
    G_OBJECT_CLASS (meta_seat_x11_parent_class)->constructed (object);
}

static void
meta_seat_x11_finalize (GObject *object)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (object);

#ifdef HAVE_LIBGUDEV
  g_clear_object (&seat_x11->udev_client);
#endif

  g_hash_table_unref (seat_x11->devices_by_id);
  g_hash_table_unref (seat_x11->tools_by_serial);
  g_hash_table_unref (seat_x11->touch_coords);
  g_list_free (seat_x11->devices);

  G_OBJECT_CLASS (meta_seat_x11_parent_class)->finalize (object);
}

static ClutterInputDevice *
meta_seat_x11_get_pointer (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  return seat_x11->core_pointer;
}

static ClutterInputDevice *
meta_seat_x11_get_keyboard (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  return seat_x11->core_keyboard;
}

static const GList *
meta_seat_x11_peek_devices (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);

  return (const GList *) seat_x11->devices;
}

static void
meta_seat_x11_bell_notify (ClutterSeat *seat)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  MetaContext *context = meta_backend_get_context (seat_x11->backend);
  MetaDisplay *display = meta_context_get_display (context);

  meta_bell_notify (display, NULL);
}

static ClutterKeymap *
meta_seat_x11_get_keymap (ClutterSeat *seat)
{
  return CLUTTER_KEYMAP (META_SEAT_X11 (seat)->keymap);
}

static ClutterVirtualInputDevice *
meta_seat_x11_create_virtual_device (ClutterSeat            *seat,
                                     ClutterInputDeviceType  device_type)
{
  return g_object_new (META_TYPE_VIRTUAL_INPUT_DEVICE_X11,
                       "seat", seat,
                       "device-type", device_type,
                       NULL);
}

static ClutterVirtualDeviceType
meta_seat_x11_get_supported_virtual_device_types (ClutterSeat *seat)
{
  return (CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD |
          CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER);
}

static void
meta_seat_x11_warp_pointer (ClutterSeat *seat,
                            int          x,
                            int          y)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  Window root_xwindow = root_xwindow_from_seat (seat_x11);

  mtk_x11_error_trap_push (xdisplay);
  XIWarpPointer (xdisplay,
                 seat_x11->pointer_id,
                 None,
                 root_xwindow,
                 0, 0, 0, 0,
                 x, y);
  mtk_x11_error_trap_pop (xdisplay);
}

static void
meta_seat_x11_init_pointer_position (ClutterSeat *seat,
                                     float        x,
                                     float        y)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  Window root_xwindow = root_xwindow_from_seat (seat_x11);

  mtk_x11_error_trap_push (xdisplay);
  XIWarpPointer (xdisplay,
                 seat_x11->pointer_id,
                 None,
                 root_xwindow,
                 0, 0, 0, 0,
                 (int) x, (int) y);
  mtk_x11_error_trap_pop (xdisplay);
}

static uint32_t
translate_state (XIButtonState   *button_state,
                 XIModifierState *modifier_state,
                 XIGroupState    *group_state)
{
  uint32_t state = 0;
  int i;

  if (modifier_state)
    state |= modifier_state->effective;

  if (button_state)
    {
      for (i = 1; i < button_state->mask_len * 8; i++)
        {
          if (!XIMaskIsSet (button_state->mask, i))
            continue;

          switch (i)
            {
            case 1:
              state |= CLUTTER_BUTTON1_MASK;
              break;
            case 2:
              state |= CLUTTER_BUTTON2_MASK;
              break;
            case 3:
              state |= CLUTTER_BUTTON3_MASK;
              break;
            case 8:
              state |= CLUTTER_BUTTON4_MASK;
              break;
            case 9:
              state |= CLUTTER_BUTTON5_MASK;
              break;
            default:
              break;
            }
        }
    }

  if (group_state)
    state |= XkbBuildCoreState (0, group_state->effective);

  return state;
}

static gboolean
meta_seat_x11_query_state (ClutterSeat          *seat,
                           ClutterInputDevice   *device,
                           ClutterEventSequence *sequence,
                           graphene_point_t     *coords,
                           ClutterModifierType  *modifiers)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (seat_x11->backend);
  Display *xdisplay = xdisplay_from_seat (seat_x11);
  Window root_ret, child_ret;
  double root_x, root_y, win_x, win_y;
  XIButtonState button_state = { 0 };
  XIModifierState modifier_state;
  XIGroupState group_state;

  mtk_x11_error_trap_push (xdisplay);
  XIQueryPointer (xdisplay,
                  seat_x11->pointer_id,
                  meta_backend_x11_get_xwindow (backend_x11),
                  &root_ret, &child_ret,
                  &root_x, &root_y, &win_x, &win_y,
                  &button_state, &modifier_state, &group_state);
  if (mtk_x11_error_trap_pop_with_return (xdisplay))
    {
      g_free (button_state.mask);
      return FALSE;
    }

  if (sequence)
    {
      MetaTouchInfo *touch_info;

      touch_info = g_hash_table_lookup (seat_x11->touch_coords, sequence);
      if (!touch_info)
        {
          g_free (button_state.mask);
          return FALSE;
        }

      if (coords)
        {
          coords->x = touch_info->x;
          coords->y = touch_info->y;
        }
    }
  else
    {
      if (coords)
        {
          coords->x = win_x;
          coords->y = win_y;
        }
    }

  if (modifiers)
    *modifiers = translate_state (&button_state, &modifier_state, &group_state);

  g_free (button_state.mask);
  return TRUE;
}

static void
meta_seat_x11_update_touchpoint (MetaSeatX11          *seat,
                                 ClutterEventSequence *sequence,
                                 double                x,
                                 double                y)
{
  MetaTouchInfo *touch_info;

  touch_info = g_hash_table_lookup (seat->touch_coords, sequence);
  if (!touch_info)
    {
      touch_info = g_new0 (MetaTouchInfo, 1);
      touch_info->sequence = sequence;
      g_hash_table_insert (seat->touch_coords, sequence, touch_info);
    }

  touch_info->x = x;
  touch_info->y = y;
}

static void
meta_seat_x11_remove_touchpoint (MetaSeatX11          *seat,
                                 ClutterEventSequence *sequence)
{
  g_hash_table_remove (seat->touch_coords, sequence);
}

static void
meta_touch_info_free (MetaTouchInfo *touch_info)
{
  g_free (touch_info);
}

static ClutterGrabState
meta_seat_x11_grab (ClutterSeat *seat,
                    uint32_t     time)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  MetaBackend *backend = seat_x11->backend;
  ClutterGrabState state = CLUTTER_GRAB_STATE_NONE;

  g_return_val_if_fail (seat_x11->grab_state == CLUTTER_GRAB_STATE_NONE,
                        seat_x11->grab_state);

  if (meta_backend_grab_device (backend,
                                META_VIRTUAL_CORE_POINTER_ID,
                                time))
    state |= CLUTTER_GRAB_STATE_POINTER;

  if (meta_backend_grab_device (backend,
                                META_VIRTUAL_CORE_KEYBOARD_ID,
                                time))
    state |= CLUTTER_GRAB_STATE_KEYBOARD;

  seat_x11->grab_state = state;

  return state;
}

static void
meta_seat_x11_ungrab (ClutterSeat *seat,
                      uint32_t     time)
{
  MetaSeatX11 *seat_x11 = META_SEAT_X11 (seat);
  MetaBackend *backend = seat_x11->backend;

  if ((seat_x11->grab_state & CLUTTER_GRAB_STATE_POINTER) != 0)
    {
      meta_backend_ungrab_device (backend,
                                  META_VIRTUAL_CORE_POINTER_ID,
                                  time);
    }

  if ((seat_x11->grab_state & CLUTTER_GRAB_STATE_KEYBOARD) != 0)
    {
      meta_backend_ungrab_device (backend,
                                  META_VIRTUAL_CORE_KEYBOARD_ID,
                                  time);
    }

  seat_x11->grab_state = CLUTTER_GRAB_STATE_NONE;
}

static void
meta_seat_x11_class_init (MetaSeatX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterSeatClass *seat_class = CLUTTER_SEAT_CLASS (klass);

  object_class->set_property = meta_seat_x11_set_property;
  object_class->get_property = meta_seat_x11_get_property;
  object_class->constructed = meta_seat_x11_constructed;
  object_class->finalize = meta_seat_x11_finalize;

  seat_class->get_pointer = meta_seat_x11_get_pointer;
  seat_class->get_keyboard = meta_seat_x11_get_keyboard;
  seat_class->peek_devices = meta_seat_x11_peek_devices;
  seat_class->bell_notify = meta_seat_x11_bell_notify;
  seat_class->get_keymap = meta_seat_x11_get_keymap;
  seat_class->create_virtual_device = meta_seat_x11_create_virtual_device;
  seat_class->get_supported_virtual_device_types = meta_seat_x11_get_supported_virtual_device_types;
  seat_class->warp_pointer = meta_seat_x11_warp_pointer;
  seat_class->init_pointer_position = meta_seat_x11_init_pointer_position;
  seat_class->handle_event_post = meta_seat_x11_handle_event_post;
  seat_class->query_state = meta_seat_x11_query_state;
  seat_class->grab = meta_seat_x11_grab;
  seat_class->ungrab = meta_seat_x11_ungrab;

  props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  props[PROP_OPCODE] =
    g_param_spec_int ("opcode", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);
  props[PROP_POINTER_ID] =
    g_param_spec_int ("pointer-id", NULL, NULL,
                      2, G_MAXINT, 2,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);
  props[PROP_KEYBOARD_ID] =
    g_param_spec_int ("keyboard-id", NULL, NULL,
                      2, G_MAXINT, 2,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);

  g_object_class_override_property (object_class, PROP_TOUCH_MODE,
                                    "touch-mode");
}

static void
meta_seat_x11_init (MetaSeatX11 *seat)
{
  seat->devices_by_id = g_hash_table_new_full (NULL, NULL,
                                               NULL,
                                               (GDestroyNotify) g_object_unref);
  seat->tools_by_serial = g_hash_table_new_full (NULL, NULL, NULL,
                                                 (GDestroyNotify) g_object_unref);
  seat->touch_coords = g_hash_table_new_full (NULL, NULL, NULL,
                                              (GDestroyNotify) meta_touch_info_free);
}

MetaSeatX11 *
meta_seat_x11_new (MetaBackend *backend,
                   int          opcode,
                   int          logical_pointer,
                   int          logical_keyboard)
{
  MetaSeatX11 *seat_x11;

  seat_x11 = g_object_new (META_TYPE_SEAT_X11,
                           "backend", backend,
                           "opcode", opcode,
                           "pointer-id", logical_pointer,
                           "keyboard-id", logical_keyboard,
                           NULL);

  return seat_x11;
}

MetaBackend *
meta_seat_x11_get_backend (MetaSeatX11 *seat_x11)
{
  return seat_x11->backend;
}

static ClutterInputDevice *
get_source_device_checked (MetaSeatX11   *seat,
                           XIDeviceEvent *xev)
{
  ClutterInputDevice *source_device;

  source_device = g_hash_table_lookup (seat->devices_by_id,
                                       GINT_TO_POINTER (xev->sourceid));

  if (!source_device)
    g_warning ("Impossible to get the source device with id %d for event of "
               "type %d", xev->sourceid, xev->evtype);

  return source_device;
}

static uint32_t
evdev_button_code (uint32_t x_button)
{
  uint32_t button;

  switch (x_button)
    {
    case 1:
      button = BTN_LEFT;
      break;

      /* The evdev input right and middle button numbers are swapped
         relative to how Clutter numbers them */
    case 2:
      button = BTN_MIDDLE;
      break;

    case 3:
      button = BTN_RIGHT;
      break;

    default:
      button = x_button + (BTN_LEFT - 1) + 4;
      break;
    }

  return button;
}

static ClutterModifierType
get_modifier_for_button (int i)
{
  switch (i)
    {
    case 1:
      return CLUTTER_BUTTON1_MASK;
    case 2:
      return CLUTTER_BUTTON2_MASK;
    case 3:
      return CLUTTER_BUTTON3_MASK;
    case 4:
      return CLUTTER_BUTTON4_MASK;
    case 5:
      return CLUTTER_BUTTON5_MASK;
    default:
      return 0;
    }
}

ClutterEvent *
meta_seat_x11_translate_event (MetaSeatX11  *seat,
                               XEvent       *xevent)
{
  Display *xdisplay = xdisplay_from_seat (seat);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (seat->backend);
  ClutterStage *stage = NULL;
  MetaStageX11 *stage_x11 = NULL;
  ClutterInputDevice *device, *source_device;
  XGenericEventCookie *cookie;
  ClutterEvent *event = NULL;
  XIEvent *xi_event;

  if (meta_keymap_x11_handle_event (seat->keymap, xevent))
    return NULL;

  cookie = &xevent->xcookie;

  if (cookie->type != GenericEvent ||
      cookie->extension != seat->opcode)
    return NULL;

  xi_event = (XIEvent *) cookie->data;

  if (!xi_event)
    return NULL;

  if (cookie->evtype == XI_RawMotion ||
      cookie->evtype == XI_RawButtonPress ||
      cookie->evtype == XI_RawButtonRelease)
    {
      translate_raw_event (seat, xevent);
      return NULL;
    }

  if (!(xi_event->evtype == XI_DeviceChanged ||
        xi_event->evtype == XI_PropertyEvent))
    {
      stage = get_event_stage (seat, xi_event);
      if (stage == NULL || CLUTTER_ACTOR_IN_DESTRUCTION (stage))
        return NULL;
      else
        stage_x11 = META_STAGE_X11 (_clutter_stage_get_window (stage));
    }

  switch (xi_event->evtype)
    {
    case XI_HierarchyChanged:
      {
        XIHierarchyEvent *xev = (XIHierarchyEvent *) xi_event;

        event = translate_hierarchy_event (clutter_backend, seat, xev);
      }
      break;

    case XI_DeviceChanged:
      {
        XIDeviceChangedEvent *xev = (XIDeviceChangedEvent *) xi_event;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));
        if (device)
          {
            meta_input_device_x11_reset_axes (device);
            translate_device_classes (xdisplay,
                                      device,
                                      xev->classes,
                                      xev->num_classes);
          }

        if (source_device)
          meta_input_device_x11_reset_scroll_info (source_device);
      }
      break;
    case XI_KeyPress:
    case XI_KeyRelease:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        MetaKeymapX11 *keymap_x11 = seat->keymap;
        char buffer[7] = { 0, };
        uint32_t keyval, evcode, keycode;
        ClutterModifierType state;
        int len;
        gunichar unicode_value;

        source_device = get_source_device_checked (seat, xev);
        if (!source_device)
          return NULL;

        state = translate_state (&xev->buttons, &xev->mods, &xev->group);

        keycode = xev->detail;

        /* clutter-xkb-utils.c adds a fixed offset of 8 to go into XKB's
         * range, so we do the reverse here. */
        evcode = keycode - 8;

        /* keyval is the key ignoring all modifiers ('1' vs. '!') */
        keyval = meta_keymap_x11_translate_key_state (keymap_x11,
                                                      keycode,
                                                      &state,
                                                      NULL);

        /* XXX keep this in sync with the evdev device manager */
        len = print_keysym (keyval, buffer, sizeof (buffer));
        if (len == 0)
          {
            /* not printable */
            unicode_value = (gunichar) '\0';
          }
        else
          {
            unicode_value = g_utf8_get_char_validated (buffer, len);
            if (unicode_value == -1 ||
                unicode_value == -2)
              unicode_value = (gunichar) '\0';
          }

        event = clutter_event_key_new ((xev->evtype == XI_KeyPress) ?
                                       CLUTTER_KEY_PRESS : CLUTTER_KEY_RELEASE,
                                       (xev->evtype == XI_KeyPress &&
                                        xev->flags & XIKeyRepeat) ?
                                       CLUTTER_EVENT_FLAG_REPEATED :
                                       CLUTTER_EVENT_NONE,
                                       ms2us (xev->time),
                                       source_device,
                                       state,
                                       keyval,
                                       evcode,
                                       keycode,
                                       unicode_value);

        g_debug ("%s: win:0x%x device:%d source:%d, key: %12s (%d)",
                 clutter_event_type (event) == CLUTTER_KEY_PRESS
                 ? "key press  "
                 : "key release",
                 (unsigned int) stage_x11->xwin,
                 xev->deviceid,
                 xev->sourceid,
                 keyval ? buffer : "(none)",
                 keyval);

        if (xi_event->evtype == XI_KeyPress)
          meta_stage_x11_set_user_time (stage_x11, xev->time);
      }
      break;

    case XI_ButtonPress:
    case XI_ButtonRelease:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        ClutterScrollDirection scroll_direction;
        ClutterModifierType state;
        ClutterInputDeviceTool *tool;
        float x, y;
        int button;
        uint32_t evdev_code;
        double *axes;

        source_device = get_source_device_checked (seat, xev);
        if (!source_device)
              return NULL;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        if (clutter_input_device_get_device_type (source_device) == CLUTTER_PAD_DEVICE)
          {
            uint32_t button, group = 0, mode = 0;

            /* We got these events because of the passive button grab */
            XIAllowEvents (xdisplay, xev->sourceid, XIAsyncDevice, xev->time);

            if (xev->detail >= 4 && xev->detail <= 7)
              {
                if (xi_event->evtype == XI_ButtonPress)
                  event = translate_pad_axis_event (xev, source_device);
                break;
              }

            /* The 4-7 button range is taken as non-existent on pad devices,
             * let the buttons above that take over this range.
             */
            if (xev->detail > 7)
              xev->detail -= 4;

            /* Pad buttons are 0-indexed */
            button = xev->detail - 1;

#ifdef HAVE_LIBWACOM
            meta_input_device_x11_update_pad_state (device,
                                                    button,
                                                    (xi_event->evtype == XI_ButtonPress),
                                                    &group,
                                                    &mode);
#endif

            event = clutter_event_pad_button_new ((xi_event->evtype == XI_ButtonPress) ?
                                                  CLUTTER_PAD_BUTTON_PRESS :
                                                  CLUTTER_PAD_BUTTON_RELEASE,
                                                  CLUTTER_EVENT_NONE,
                                                  us2ms (xev->time),
                                                  source_device,
                                                  button,
                                                  group,
                                                  mode);

            g_debug ("%s: win:0x%x, device:%d '%s', time:%lu "
                     "(button:%d)",
                     (xi_event->evtype == XI_ButtonPress)
                     ? "pad button press  "
                     : "pad button release",
                     (unsigned int) stage_x11->xwin,
                     meta_input_device_x11_get_device_id (device),
                     clutter_input_device_get_device_name (device),
                     xev->time,
                     button);
            break;
          }

        switch (xev->detail)
          {
          case 4:
          case 5:
          case 6:
          case 7:
            /* we only generate Scroll events on ButtonPress */
            if (xi_event->evtype == XI_ButtonRelease)
              return NULL;

            if (xev->detail == 4)
              scroll_direction = CLUTTER_SCROLL_UP;
            else if (xev->detail == 5)
              scroll_direction = CLUTTER_SCROLL_DOWN;
            else if (xev->detail == 6)
              scroll_direction = CLUTTER_SCROLL_LEFT;
            else
              scroll_direction = CLUTTER_SCROLL_RIGHT;

            translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);
            state = translate_state (&xev->buttons, &xev->mods, &xev->group);
            tool = meta_input_device_x11_get_current_tool (source_device);

            event = clutter_event_scroll_discrete_new (CLUTTER_EVENT_NONE,
                                                       ms2us (xev->time),
                                                       source_device,
                                                       tool,
                                                       state,
                                                       GRAPHENE_POINT_INIT (x, y),
                                                       scroll_direction);

            g_debug ("scroll: win:0x%x, device:%d '%s', time:%d "
                     "(direction:%s, "
                     "x:%.2f, y:%.2f, "
                     "emulated:%s)",
                     (unsigned int) stage_x11->xwin,
                     meta_input_device_x11_get_device_id (device),
                     clutter_input_device_get_device_name (device),
                     clutter_event_get_time (event),
                     scroll_direction == CLUTTER_SCROLL_UP ? "up" :
                     scroll_direction == CLUTTER_SCROLL_DOWN ? "down" :
                     scroll_direction == CLUTTER_SCROLL_LEFT ? "left" :
                     scroll_direction == CLUTTER_SCROLL_RIGHT ? "right" :
                     "invalid",
                     x, y,
                     (xev->flags & XIPointerEmulated) ? "yes" : "no");
            break;

          default:
            translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);
            button = xev->detail;
            evdev_code = evdev_button_code (xev->detail);
            state = translate_state (&xev->buttons, &xev->mods, &xev->group);
            tool = meta_input_device_x11_get_current_tool (source_device);
            axes = translate_axes (device, x, y, &xev->valuators);

            /* The XIButtonState sent in the event specifies the
             * state of the buttons before the event. In order to
             * get the current state of the buttons, we need to
             * filter out the current button.
             */
            switch (xi_event->evtype)
              {
              case XI_ButtonPress:
                state |= (get_modifier_for_button (button));
                break;
              case XI_ButtonRelease:
                state &= ~(get_modifier_for_button (button));
                break;
              default:
                break;
              }

            event = clutter_event_button_new ((xi_event->evtype == XI_ButtonPress) ?
                                              CLUTTER_BUTTON_PRESS :
                                              CLUTTER_BUTTON_RELEASE,
                                              (xev->flags & XIPointerEmulated) ?
                                              CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                              CLUTTER_EVENT_NONE,
                                              ms2us (xev->time),
                                              source_device,
                                              tool,
                                              state,
                                              GRAPHENE_POINT_INIT (x, y),
                                              button,
                                              evdev_code,
                                              axes);

            g_debug ("%s: win:0x%x, device:%d '%s', time:%lu "
                     "(button:%d, "
                     "x:%.2f, y:%.2f, "
                     "axes:%s, "
                     "emulated:%s)",
                     (xi_event->evtype == XI_ButtonPress)
                     ? "button press  "
                     : "button release",
                     (unsigned int) stage_x11->xwin,
                     meta_input_device_x11_get_device_id (device),
                     clutter_input_device_get_device_name (device),
                     xev->time,
                     xev->detail,
                     x, y,
                     axes != NULL ? "yes" : "no",
                     (xev->flags & XIPointerEmulated) ? "yes" : "no");
            break;
          }

        if (xi_event->evtype == XI_ButtonPress)
          meta_stage_x11_set_user_time (stage_x11, xev->time);
      }
      break;

    case XI_Motion:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        double delta_x, delta_y;
        ClutterModifierType state;
        ClutterInputDeviceTool *tool;
        float x, y;
        double *axes;

        source_device = get_source_device_checked (seat, xev);
        if (!source_device)
          break;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        if (clutter_input_device_get_device_type (source_device) == CLUTTER_PAD_DEVICE)
          {
            event = translate_pad_axis_event (xev, source_device);
            break;
          }

        translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);
        state = translate_state (&xev->buttons, &xev->mods, &xev->group);
        tool = meta_input_device_x11_get_current_tool (source_device);

        if (scroll_valuators_changed (source_device,
                                      &xev->valuators,
                                      &delta_x, &delta_y))
          {
            event = clutter_event_scroll_smooth_new (CLUTTER_EVENT_NONE,
                                                     ms2us (xev->time),
                                                     source_device,
                                                     tool,
                                                     state,
                                                     GRAPHENE_POINT_INIT (x, y),
                                                     GRAPHENE_POINT_INIT (delta_x, delta_y),
                                                     CLUTTER_SCROLL_SOURCE_UNKNOWN,
                                                     CLUTTER_SCROLL_FINISHED_NONE);

            g_debug ("smooth scroll: win:0x%x device:%d '%s' (x:%.2f, y:%.2f, delta:%f, %f)",
                     (unsigned int) stage_x11->xwin,
                     meta_input_device_x11_get_device_id (source_device),
                     clutter_input_device_get_device_name (source_device),
                     x, y,
                     delta_x, delta_y);
            break;
          }

        axes = translate_axes (device, x, y, &xev->valuators);
        event = clutter_event_motion_new ((xev->flags & XIPointerEmulated) ?
                                          CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                          CLUTTER_EVENT_NONE,
                                          ms2us (xev->time),
                                          source_device,
                                          tool,
                                          state,
                                          GRAPHENE_POINT_INIT (x, y),
                                          GRAPHENE_POINT_INIT (0, 0),
                                          GRAPHENE_POINT_INIT (0, 0),
                                          GRAPHENE_POINT_INIT (0, 0),
                                          axes);

        g_debug ("motion: win:0x%x device:%d '%s' (x:%.2f, y:%.2f, axes:%s)",
                 (unsigned int) stage_x11->xwin,
                 meta_input_device_x11_get_device_id (source_device),
                 clutter_input_device_get_device_name (source_device),
                 x, y,
                 axes != NULL ? "yes" : "no");
      }
      break;

    case XI_TouchBegin:
    case XI_TouchEnd:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        ClutterEventType evtype;
        ClutterModifierType state;
        ClutterEventSequence *sequence;
        float x, y;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        if (xi_event->evtype == XI_TouchBegin)
          evtype = CLUTTER_TOUCH_BEGIN;
        else
          evtype = CLUTTER_TOUCH_END;

        translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);
        state = translate_state (&xev->buttons, &xev->mods, &xev->group);
        /* "NULL" sequences are special cased in clutter */
        sequence = GINT_TO_POINTER (MAX (1, xev->detail + 1));

        if (xi_event->evtype == XI_TouchBegin)
          {
            state |= CLUTTER_BUTTON1_MASK;

            meta_stage_x11_set_user_time (stage_x11, xev->time);
            meta_seat_x11_update_touchpoint (seat,
                                             sequence,
                                             xev->root_x,
                                             xev->root_y);
          }
        else if (xi_event->evtype == XI_TouchEnd)
          {
            meta_seat_x11_remove_touchpoint (seat, sequence);
          }

        event = clutter_event_touch_new (evtype,
                                         (xev->flags & XITouchEmulatingPointer) ?
                                         CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                         CLUTTER_EVENT_NONE,
                                         ms2us (xev->time),
                                         source_device,
                                         sequence,
                                         state,
                                         GRAPHENE_POINT_INIT (x, y));

        g_debug ("touch %s: win:0x%x device:%d '%s' (seq:%d, x:%.2f, y:%.2f)",
                 evtype == CLUTTER_TOUCH_BEGIN ? "begin" : "end",
                 (unsigned int) stage_x11->xwin,
                 meta_input_device_x11_get_device_id (device),
                 clutter_input_device_get_device_name (device),
                 GPOINTER_TO_UINT (sequence),
                 x, y);
      }
      break;

    case XI_TouchUpdate:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) xi_event;
        ClutterEventSequence *sequence;
        ClutterModifierType state;
        float x, y;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));
        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        /* "NULL" sequences are special cased in clutter */
        sequence = GINT_TO_POINTER (MAX (1, xev->detail + 1));
        translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);
        state = translate_state (&xev->buttons, &xev->mods, &xev->group);
        state |= CLUTTER_BUTTON1_MASK;

        meta_seat_x11_update_touchpoint (seat,
                                         sequence,
                                         xev->root_x,
                                         xev->root_y);

        event = clutter_event_touch_new (CLUTTER_TOUCH_UPDATE,
                                         (xev->flags & XITouchEmulatingPointer) ?
                                         CLUTTER_EVENT_FLAG_POINTER_EMULATED :
                                         CLUTTER_EVENT_NONE,
                                         ms2us (xev->time),
                                         source_device,
                                         sequence,
                                         state,
                                         GRAPHENE_POINT_INIT (x, y));

        g_debug ("touch update: win:0x%x device:%d '%s' (seq:%d, x:%.2f, y:%.2f)",
                 (unsigned int) stage_x11->xwin,
                 meta_input_device_x11_get_device_id (device),
                 clutter_input_device_get_device_name (device),
                 GPOINTER_TO_UINT (sequence),
                 x, y);
      }
      break;

    case XI_Enter:
    case XI_Leave:
      {
        XIEnterEvent *xev = (XIEnterEvent *) xi_event;
        float x, y;

        device = g_hash_table_lookup (seat->devices_by_id,
                                      GINT_TO_POINTER (xev->deviceid));

        source_device = g_hash_table_lookup (seat->devices_by_id,
                                             GINT_TO_POINTER (xev->sourceid));

        translate_coords (stage_x11, xev->event_x, xev->event_y, &x, &y);

        event = clutter_event_crossing_new ((xi_event->evtype == XI_Enter) ?
                                            CLUTTER_ENTER : CLUTTER_LEAVE,
                                            CLUTTER_EVENT_NONE,
                                            ms2us (xev->time),
                                            device,
                                            NULL,
                                            GRAPHENE_POINT_INIT (x, y),
                                            CLUTTER_ACTOR (stage), NULL);

        if (xev->deviceid == seat->pointer_id)
          seat->has_pointer_focus = (xi_event->evtype == XI_Enter);

        meta_input_device_x11_reset_scroll_info (source_device);
      }
      break;

    case XI_FocusIn:
    case XI_FocusOut:
      break;
    case XI_PropertyEvent:
      translate_property_event (seat, xi_event);
      break;
    }

  return event;
}

void
meta_seat_x11_select_stage_events (MetaSeatX11  *seat,
                                   ClutterStage *stage)
{
  Display *xdisplay = xdisplay_from_seat (seat);
  MetaStageX11 *stage_x11;
  XIEventMask xi_event_mask;
  unsigned char *mask;
  int len;

  stage_x11 = META_STAGE_X11 (_clutter_stage_get_window (stage));

  len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (unsigned char, len);

  XISetMask (mask, XI_Motion);
  XISetMask (mask, XI_ButtonPress);
  XISetMask (mask, XI_ButtonRelease);
  XISetMask (mask, XI_KeyPress);
  XISetMask (mask, XI_KeyRelease);
  XISetMask (mask, XI_Enter);
  XISetMask (mask, XI_Leave);

  XISetMask (mask, XI_TouchBegin);
  XISetMask (mask, XI_TouchUpdate);
  XISetMask (mask, XI_TouchEnd);

  xi_event_mask.deviceid = XIAllMasterDevices;
  xi_event_mask.mask = mask;
  xi_event_mask.mask_len = len;

  XISelectEvents (xdisplay, stage_x11->xwin, &xi_event_mask, 1);

  g_free (mask);
}
