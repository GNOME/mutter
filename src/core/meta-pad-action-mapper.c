/*
 * Copyright (C) 2014-2020 Red Hat
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
 *     Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "core/meta-pad-action-mapper.h"
#include "backends/meta-input-device-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "core/display-private.h"

typedef struct _PadMappingInfo PadMappingInfo;

struct _PadMappingInfo
{
  ClutterInputDevice *device;
  GSettings *settings;
  guint *group_modes;
};

struct _MetaPadActionMapper
{
  GObject parent_class;

  GHashTable *pads;
  ClutterSeat *seat;
  ClutterVirtualInputDevice *virtual_pad_keyboard;
  MetaMonitorManager *monitor_manager;

  /* Pad ring/strip emission */
  struct {
    ClutterInputDevice *pad;
    MetaPadFeatureType feature;
    guint number;
    double value;
  } last_pad_action_info;
};

G_DEFINE_TYPE (MetaPadActionMapper, meta_pad_action_mapper, G_TYPE_OBJECT)

static MetaDisplay *
display_from_mapper (MetaPadActionMapper *mapper)
{
  MetaBackend *backend =
    meta_monitor_manager_get_backend (mapper->monitor_manager);
  MetaContext *context = meta_backend_get_context (backend);

  return meta_context_get_display (context);
}

static void
meta_pad_action_mapper_finalize (GObject *object)
{
  MetaPadActionMapper *mapper = META_PAD_ACTION_MAPPER (object);

  g_hash_table_unref (mapper->pads);
  g_object_unref (mapper->monitor_manager);
  g_clear_object (&mapper->virtual_pad_keyboard);

  G_OBJECT_CLASS (meta_pad_action_mapper_parent_class)->finalize (object);
}

static void
meta_pad_action_mapper_class_init (MetaPadActionMapperClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_pad_action_mapper_finalize;
}

static GSettings *
lookup_device_settings (ClutterInputDevice *device)
{
  const char *vendor, *product;
  GSettings *settings;
  char *path;

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);
  path = g_strdup_printf ("/org/gnome/desktop/peripherals/tablets/%s:%s/",
                          vendor, product);

  settings = g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet",
                                       path);
  g_free (path);

  return settings;
}

static PadMappingInfo *
pad_mapping_info_new (ClutterInputDevice *pad)
{
  PadMappingInfo *info;

  info = g_new0 (PadMappingInfo, 1);
  info->device = pad;
  info->settings = lookup_device_settings (pad);
  info->group_modes =
    g_new0 (guint, clutter_input_device_get_n_mode_groups (pad));

  return info;
}

static void
pad_mapping_info_free (PadMappingInfo *info)
{
  g_object_unref (info->settings);
  g_free (info->group_modes);
  g_free (info);
}

static void
device_added (MetaPadActionMapper *mapper,
              ClutterInputDevice  *device)
{
  PadMappingInfo *info;

  if ((clutter_input_device_get_capabilities (device) &
       CLUTTER_INPUT_CAPABILITY_TABLET_PAD) != 0)
    {
      info = pad_mapping_info_new (device);
      g_hash_table_insert (mapper->pads, device, info);
    }
}

static void
device_removed (MetaPadActionMapper *mapper,
                ClutterInputDevice  *device)
{
  g_hash_table_remove (mapper->pads, device);
}

static void
meta_pad_action_mapper_init (MetaPadActionMapper *mapper)
{
  g_autoptr (GList) devices = NULL;
  GList *l;

  mapper->pads = g_hash_table_new_full (NULL, NULL, NULL,
                                        (GDestroyNotify) pad_mapping_info_free);

  mapper->seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  devices = clutter_seat_list_devices (mapper->seat);

  for (l = devices; l; l = l->next)
    device_added (mapper, l->data);
}

MetaPadActionMapper *
meta_pad_action_mapper_new (MetaMonitorManager *monitor_manager)
{
  MetaPadActionMapper *action_mapper;

  action_mapper = g_object_new (META_TYPE_PAD_ACTION_MAPPER, NULL);
  g_set_object (&action_mapper->monitor_manager, monitor_manager);

  return action_mapper;
}

static GSettings *
get_pad_feature_gsettings (ClutterInputDevice *device,
                           const char         *feature,
                           int                 feature_number,
                           const char         *suffix)
{
  GSettings *settings;
  g_autofree char *path = NULL;
  const gchar *vendor, *product;
  char tag;

  tag = 'A' + feature_number;
  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);

  path = g_strdup_printf ("/org/gnome/desktop/peripherals/tablets/%s:%s/%s%c%s/",
                          vendor, product, feature, tag,
                          suffix ? suffix : "");
  settings = g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet.pad-button",
                                       path);

  return settings;
}

static GSettings *
lookup_pad_button_settings (ClutterInputDevice *device,
                            int                 button)
{
  return get_pad_feature_gsettings (device, "button", button, NULL);
}

static GSettings *
lookup_pad_feature_settings (ClutterInputDevice *device,
                             MetaPadFeatureType  feature,
                             guint               number,
                             MetaPadDirection    direction,
                             int                 mode)
{
  g_autofree char *suffix = NULL;
  const char *feature_type, *detail_type;

  switch (feature)
    {
    case META_PAD_FEATURE_RING:
      g_assert (direction == META_PAD_DIRECTION_CW ||
                direction == META_PAD_DIRECTION_CCW);
      feature_type = "ring";
      detail_type = (direction == META_PAD_DIRECTION_CW) ? "cw" : "ccw";
      break;
    case META_PAD_FEATURE_STRIP:
      g_assert (direction == META_PAD_DIRECTION_UP ||
                direction == META_PAD_DIRECTION_DOWN);
      feature_type = "strip";
      detail_type = (direction == META_PAD_DIRECTION_UP) ? "up" : "down";
      break;
    default:
      return NULL;
    }

  if (mode >= 0)
    suffix = g_strdup_printf ("-%s-mode-%d", detail_type, mode);
  else
    suffix = g_strdup_printf ("-%s", detail_type);

  return get_pad_feature_gsettings (device, feature_type, number, suffix);
}

static GDesktopPadButtonAction
meta_pad_action_mapper_get_button_action (MetaPadActionMapper *mapper,
                                          ClutterInputDevice  *pad,
                                          guint                button)
{
  GDesktopPadButtonAction action;
  GSettings *settings;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper),
                        G_DESKTOP_PAD_BUTTON_ACTION_NONE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad),
                        G_DESKTOP_PAD_BUTTON_ACTION_NONE);

  settings = lookup_pad_button_settings (pad, button);
  action = g_settings_get_enum (settings, "action");
  g_object_unref (settings);

  return action;
}

static gboolean
cycle_logical_monitors (MetaPadActionMapper *mapper,
                        gboolean             skip_all_monitors,
                        MetaLogicalMonitor  *current_logical_monitor,
                        MetaLogicalMonitor **next_logical_monitor)
{
  MetaMonitorManager *monitor_manager = mapper->monitor_manager;
  GList *logical_monitors;

  /* We cycle between:
   * - the span of all monitors (current_output = NULL), only for
   *   non-integrated devices.
   * - each monitor individually.
   */

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);

  if (!current_logical_monitor)
    {
      *next_logical_monitor = logical_monitors->data;
    }
  else
    {
      GList *l;

      l = g_list_find (logical_monitors, current_logical_monitor);
      if (l->next)
        *next_logical_monitor = l->next->data;
      else if (skip_all_monitors)
        *next_logical_monitor = logical_monitors->data;
      else
        *next_logical_monitor = NULL;
    }

  return TRUE;
}

static MetaMonitor *
logical_monitor_find_monitor (MetaLogicalMonitor *logical_monitor,
                              const char         *vendor,
                              const char         *product,
                              const char         *serial)
{
  GList *monitors;
  GList *l;

  monitors = meta_logical_monitor_get_monitors (logical_monitor);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *monitor = l->data;

      if (g_strcmp0 (meta_monitor_get_vendor (monitor), vendor) == 0 &&
          g_strcmp0 (meta_monitor_get_product (monitor), product) == 0 &&
          g_strcmp0 (meta_monitor_get_serial (monitor), serial) == 0)
        return monitor;
    }

  return NULL;
}

static void
meta_pad_action_mapper_find_monitor (MetaPadActionMapper  *mapper,
                                     GSettings            *settings,
                                     ClutterInputDevice   *device,
                                     MetaMonitor         **out_monitor,
                                     MetaLogicalMonitor  **out_logical_monitor)
{
  MetaMonitorManager *monitor_manager;
  MetaMonitor *monitor;
  guint n_values;
  GList *logical_monitors;
  GList *l;
  char **edid;

  edid = g_settings_get_strv (settings, "output");
  n_values = g_strv_length (edid);

  if (n_values != 3)
    {
      g_warning ("EDID configuration for device '%s' "
                 "is incorrect, must have 3 values",
                 clutter_input_device_get_device_name (device));
      goto out;
    }

  if (!*edid[0] && !*edid[1] && !*edid[2])
    goto out;

  monitor_manager = mapper->monitor_manager;
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;

      monitor = logical_monitor_find_monitor (logical_monitor,
                                              edid[0], edid[1], edid[2]);
      if (monitor)
        {
          if (out_monitor)
            *out_monitor = monitor;
          if (out_logical_monitor)
            *out_logical_monitor = logical_monitor;
          break;
        }
    }

out:
  g_strfreev (edid);
}

static void
meta_pad_action_mapper_cycle_tablet_output (MetaPadActionMapper *mapper,
                                            ClutterInputDevice  *device)
{
  PadMappingInfo *info;
  MetaLogicalMonitor *logical_monitor = NULL;
  const char *edid[4] = { 0 }, *pretty_name = NULL;
  gboolean is_integrated_device = FALSE;
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device;
#endif

  g_return_if_fail (META_IS_PAD_ACTION_MAPPER (mapper));
  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail ((clutter_input_device_get_capabilities (device) &
                     (CLUTTER_INPUT_CAPABILITY_TABLET_TOOL |
                      CLUTTER_INPUT_CAPABILITY_TABLET_PAD)) != 0);

  info = g_hash_table_lookup (mapper->pads, device);
  g_return_if_fail (info != NULL);

#ifdef HAVE_LIBWACOM
  wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (device));

  if (wacom_device)
    {
      pretty_name = libwacom_get_name (wacom_device);
      is_integrated_device =
        libwacom_get_integration_flags (wacom_device) != WACOM_DEVICE_INTEGRATED_NONE;
    }
#endif

  meta_pad_action_mapper_find_monitor (mapper, info->settings, device,
                                       NULL, &logical_monitor);

  if (!cycle_logical_monitors (mapper,
                               is_integrated_device,
                               logical_monitor,
                               &logical_monitor))
    return;

  if (logical_monitor)
    {
      MetaMonitor *monitor;
      const char *vendor;
      const char *product;
      const char *serial;

      /* Pick an arbitrary monitor in the logical monitor to represent it. */
      monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
      vendor = meta_monitor_get_vendor (monitor);
      product = meta_monitor_get_product (monitor);
      serial = meta_monitor_get_serial (monitor);
      edid[0] = vendor ? vendor : "";
      edid[1] = product ? product : "";
      edid[2] = serial ? serial : "";
    }
  else
    {
      edid[0] = "";
      edid[1] = "";
      edid[2] = "";
    }

  g_settings_set_strv (info->settings, "output", edid);
  meta_display_show_tablet_mapping_notification (display_from_mapper (mapper),
                                                 device, pretty_name);
}

gboolean
meta_pad_action_mapper_is_button_grabbed (MetaPadActionMapper *mapper,
                                          ClutterInputDevice  *pad,
                                          guint                button)
{
  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), FALSE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), FALSE);
  g_return_val_if_fail ((clutter_input_device_get_capabilities (pad) &
                         CLUTTER_INPUT_CAPABILITY_TABLET_PAD) != 0, FALSE);

  return (meta_pad_action_mapper_get_button_action (mapper, pad, button) !=
          G_DESKTOP_PAD_BUTTON_ACTION_NONE);
}

static void
emulate_modifiers (ClutterVirtualInputDevice *device,
                   ClutterModifierType        mods,
                   ClutterKeyState            state)
{
  guint i;
  struct {
    ClutterModifierType mod;
    guint keyval;
  } mod_map[] = {
    { CLUTTER_SHIFT_MASK, CLUTTER_KEY_Shift_L },
    { CLUTTER_CONTROL_MASK, CLUTTER_KEY_Control_L },
    { CLUTTER_META_MASK, CLUTTER_KEY_Meta_L }
  };

  for (i = 0; i < G_N_ELEMENTS (mod_map); i++)
    {
      if ((mods & mod_map[i].mod) == 0)
        continue;

      clutter_virtual_input_device_notify_keyval (device,
                                                  clutter_get_current_event_time (),
                                                  mod_map[i].keyval, state);
    }
}

static void
meta_pad_action_mapper_emulate_keybinding (MetaPadActionMapper *mapper,
                                           const char          *accel,
                                           gboolean             is_press)
{
  ClutterKeyState state;
  MetaKeyCombo combo = { 0 };

  if (!accel || !*accel)
    return;

  if (!meta_parse_accelerator (accel, &combo))
    {
      g_warning ("\"%s\" is not a valid accelerator", accel);
      return;
    }

  if (!mapper->virtual_pad_keyboard)
    {
      ClutterBackend *backend;
      ClutterSeat *seat;

      backend = clutter_get_default_backend ();
      seat = clutter_backend_get_default_seat (backend);

      mapper->virtual_pad_keyboard =
        clutter_seat_create_virtual_device (seat,
                                            CLUTTER_KEYBOARD_DEVICE);
    }

  state = is_press ? CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;

  if (is_press)
    emulate_modifiers (mapper->virtual_pad_keyboard, combo.modifiers, state);

  clutter_virtual_input_device_notify_keyval (mapper->virtual_pad_keyboard,
                                              clutter_get_current_event_time (),
                                              combo.keysym, state);
  if (!is_press)
    emulate_modifiers (mapper->virtual_pad_keyboard, combo.modifiers, state);
}

static gboolean
meta_pad_action_mapper_handle_button (MetaPadActionMapper *mapper,
                                      ClutterInputDevice  *pad,
                                      const ClutterEvent  *event)
{
  GDesktopPadButtonAction action;
  int group, n_modes = 0;
  gboolean is_press;
  GSettings *settings;
  char *accel;
  uint32_t button, mode;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), FALSE);
  g_return_val_if_fail (clutter_event_type (event) == CLUTTER_PAD_BUTTON_PRESS ||
                        clutter_event_type (event) == CLUTTER_PAD_BUTTON_RELEASE, FALSE);

  clutter_event_get_pad_details (event, &button, &mode, NULL, NULL);
  group = clutter_input_device_get_mode_switch_button_group (pad, button);
  is_press = clutter_event_type (event) == CLUTTER_PAD_BUTTON_PRESS;

  if (group >= 0)
    n_modes = clutter_input_device_get_group_n_modes (pad, group);

  if (is_press && n_modes > 0)
    {
      const char *pretty_name = NULL;
      PadMappingInfo *info;
#ifdef HAVE_LIBWACOM
      WacomDevice *wacom_device;
#endif

      info = g_hash_table_lookup (mapper->pads, pad);

#ifdef HAVE_LIBWACOM
      wacom_device = meta_input_device_get_wacom_device (META_INPUT_DEVICE (pad));

      if (wacom_device)
        pretty_name = libwacom_get_name (wacom_device);
#endif
      meta_display_notify_pad_group_switch (display_from_mapper (mapper), pad,
                                            pretty_name, group, mode, n_modes);
      info->group_modes[group] = mode;
    }

  action = meta_pad_action_mapper_get_button_action (mapper, pad, button);

  switch (action)
    {
    case G_DESKTOP_PAD_BUTTON_ACTION_SWITCH_MONITOR:
      if (is_press)
        meta_pad_action_mapper_cycle_tablet_output (mapper, pad);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_HELP:
      if (is_press)
        meta_display_request_pad_osd (display_from_mapper (mapper), pad, FALSE);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      settings = lookup_pad_button_settings (pad, button);
      accel = g_settings_get_string (settings, "keybinding");
      meta_pad_action_mapper_emulate_keybinding (mapper, accel, is_press);
      g_object_unref (settings);
      g_free (accel);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_NONE:
    default:
      return FALSE;
    }
}

static gboolean
meta_pad_action_mapper_get_action_direction (MetaPadActionMapper *mapper,
                                             const ClutterEvent  *event,
                                             MetaPadDirection    *direction)
{
  ClutterInputDevice *pad = clutter_event_get_device (event);
  MetaPadFeatureType pad_feature;
  gboolean has_direction = FALSE;
  MetaPadDirection inc_dir, dec_dir;
  uint32_t number;
  double value;

  switch (clutter_event_type (event))
    {
    case CLUTTER_PAD_RING:
      pad_feature = META_PAD_FEATURE_RING;
      clutter_event_get_pad_details (event, &number, NULL, NULL, &value);
      inc_dir = META_PAD_DIRECTION_CW;
      dec_dir = META_PAD_DIRECTION_CCW;
      break;
    case CLUTTER_PAD_STRIP:
      pad_feature = META_PAD_FEATURE_STRIP;
      clutter_event_get_pad_details (event, &number, NULL, NULL, &value);
      inc_dir = META_PAD_DIRECTION_DOWN;
      dec_dir = META_PAD_DIRECTION_UP;
      break;
    default:
      return FALSE;
    }

  if (mapper->last_pad_action_info.pad == pad &&
      mapper->last_pad_action_info.feature == pad_feature &&
      mapper->last_pad_action_info.number == number &&
      value >= 0 && mapper->last_pad_action_info.value >= 0)
    {
      *direction = (value - mapper->last_pad_action_info.value) > 0 ?
        inc_dir : dec_dir;
      has_direction = TRUE;
    }

  mapper->last_pad_action_info.pad = pad;
  mapper->last_pad_action_info.feature = pad_feature;
  mapper->last_pad_action_info.number = number;
  mapper->last_pad_action_info.value = value;
  return has_direction;
}

static gboolean
meta_pad_action_mapper_handle_action (MetaPadActionMapper *mapper,
                                      ClutterInputDevice  *pad,
                                      const ClutterEvent  *event,
                                      MetaPadFeatureType   feature,
                                      guint                number,
                                      guint                mode)
{
  MetaPadDirection direction;
  g_autoptr (GSettings) settings1 = NULL, settings2 = NULL;
  g_autofree char *accel1 = NULL, *accel2 = NULL;
  gboolean handled;

  if (feature == META_PAD_FEATURE_RING)
    {
      settings1 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_CW, mode);
      settings2 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_CCW, mode);
    }
  else if (feature == META_PAD_FEATURE_STRIP)
    {
      settings1 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_UP, mode);
      settings2 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_DOWN, mode);
    }
  else
    {
      return FALSE;
    }

  accel1 = g_settings_get_string (settings1, "keybinding");
  accel2 = g_settings_get_string (settings2, "keybinding");
  handled = ((accel1 && *accel1) || (accel2 && *accel2));

  if (meta_pad_action_mapper_get_action_direction (mapper, event, &direction))
    {
      const gchar *accel = NULL;

      if (direction == META_PAD_DIRECTION_UP ||
          direction == META_PAD_DIRECTION_CW)
        accel = accel1;
      else if (direction == META_PAD_DIRECTION_DOWN ||
               direction == META_PAD_DIRECTION_CCW)
        accel = accel2;

      if (accel && *accel)
        {
          meta_pad_action_mapper_emulate_keybinding (mapper, accel, TRUE);
          meta_pad_action_mapper_emulate_keybinding (mapper, accel, FALSE);
        }
    }

  return handled;
}

gboolean
meta_pad_action_mapper_handle_event (MetaPadActionMapper *mapper,
                                     const ClutterEvent  *event)
{
  ClutterInputDevice *pad;
  uint32_t number, mode;

  pad = clutter_event_get_source_device ((ClutterEvent *) event);

  switch (clutter_event_type (event))
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      return meta_pad_action_mapper_handle_button (mapper, pad, event);
    case CLUTTER_PAD_RING:
      clutter_event_get_pad_details (event, &number, &mode, NULL, NULL);
      return meta_pad_action_mapper_handle_action (mapper, pad, event,
                                                   META_PAD_FEATURE_RING,
                                                   number, mode);
    case CLUTTER_PAD_STRIP:
      clutter_event_get_pad_details (event, &number, &mode, NULL, NULL);
      return meta_pad_action_mapper_handle_action (mapper, pad, event,
                                                   META_PAD_FEATURE_STRIP,
                                                   number, mode);
    case CLUTTER_DEVICE_ADDED:
      device_added (mapper, clutter_event_get_source_device (event));
      break;
    case CLUTTER_DEVICE_REMOVED:
      device_removed (mapper, clutter_event_get_source_device (event));
      break;
    default:
      break;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static char *
meta_pad_action_mapper_get_ring_label (MetaPadActionMapper *mapper,
                                       ClutterInputDevice  *pad,
                                       int                  number,
                                       unsigned int         mode,
                                       MetaPadDirection     direction)
{
  g_autoptr (GSettings) settings = NULL;
  g_autofree char *action = NULL;

  if (direction != META_PAD_DIRECTION_CW &&
      direction != META_PAD_DIRECTION_CCW)
    return NULL;

  settings = lookup_pad_feature_settings (pad, META_PAD_FEATURE_RING,
                                          number, direction, mode);

  /* We only allow keybinding actions with those */
  action = g_settings_get_string (settings, "keybinding");
  if (action && *action)
    return g_steal_pointer (&action);

  return NULL;
}

static char *
meta_pad_action_mapper_get_strip_label (MetaPadActionMapper *mapper,
                                        ClutterInputDevice  *pad,
                                        int                  number,
                                        unsigned int         mode,
                                        MetaPadDirection     direction)
{
  g_autoptr (GSettings) settings = NULL;
  g_autofree char *action = NULL;

  if (direction != META_PAD_DIRECTION_UP &&
      direction != META_PAD_DIRECTION_DOWN)
    return NULL;

  settings = lookup_pad_feature_settings (pad, META_PAD_FEATURE_STRIP,
                                          number, direction, mode);

  /* We only allow keybinding actions with those */
  action = g_settings_get_string (settings, "keybinding");
  if (action && *action)
    return g_steal_pointer (&action);

  return NULL;
}

char *
meta_pad_action_mapper_get_button_label (MetaPadActionMapper *mapper,
                                         ClutterInputDevice  *pad,
                                         int                  button)
{
  GDesktopPadButtonAction action;
  int group;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), NULL);
  g_return_val_if_fail ((clutter_input_device_get_capabilities (pad) &
                         CLUTTER_INPUT_CAPABILITY_TABLET_PAD) != 0, NULL);

  group = clutter_input_device_get_mode_switch_button_group (pad, button);

  if (group >= 0)
    {
      /* TRANSLATORS: This string refers to a button that switches between
       * different modes.
       */
      return g_strdup_printf (_("Mode Switch (Group %d)"), group);
    }

  action = meta_pad_action_mapper_get_button_action (mapper, pad, button);

  switch (action)
    {
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      {
        GSettings *settings;
        char *accel;

        settings = lookup_pad_button_settings (pad, button);
        accel = g_settings_get_string (settings, "keybinding");
        g_object_unref (settings);

        return accel;
      }
    case G_DESKTOP_PAD_BUTTON_ACTION_SWITCH_MONITOR:
      /* TRANSLATORS: This string refers to an action, cycles drawing tablets'
       * mapping through the available outputs.
       */
      return g_strdup (_("Switch monitor"));
    case G_DESKTOP_PAD_BUTTON_ACTION_HELP:
      return g_strdup (_("Show on-screen help"));
    case G_DESKTOP_PAD_BUTTON_ACTION_NONE:
    default:
      return NULL;
    }
}

static guint
get_current_pad_mode (MetaPadActionMapper *mapper,
                      ClutterInputDevice  *pad,
                      MetaPadFeatureType   feature,
                      guint                number)
{
  PadMappingInfo *info;
  guint group = 0, n_groups;

  info = g_hash_table_lookup (mapper->pads, pad);
  n_groups = clutter_input_device_get_n_mode_groups (pad);

  if (!info->group_modes || n_groups == 0)
    return 0;

  if (feature == META_PAD_FEATURE_RING ||
      feature == META_PAD_FEATURE_STRIP)
    {
      /* Assume features are evenly distributed in groups */
      group = number % n_groups;
    }

  return info->group_modes[group];
}

char *
meta_pad_action_mapper_get_feature_label (MetaPadActionMapper *mapper,
                                          ClutterInputDevice  *pad,
                                          MetaPadFeatureType   feature,
                                          MetaPadDirection     direction,
                                          int                  number)
{
  unsigned int mode;

  switch (feature)
    {
    case META_PAD_FEATURE_RING:
      mode = get_current_pad_mode (mapper, pad, feature, number);
      return meta_pad_action_mapper_get_ring_label (mapper, pad, number, mode, direction);
    case META_PAD_FEATURE_STRIP:
      mode = get_current_pad_mode (mapper, pad, feature, number);
      return meta_pad_action_mapper_get_strip_label (mapper, pad, number, mode, direction);
    }

  return NULL;
}
