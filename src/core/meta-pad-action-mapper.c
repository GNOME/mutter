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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

typedef enum
{
  META_PAD_DIRECTION_NONE = -1,
  META_PAD_DIRECTION_UP = 0,
  META_PAD_DIRECTION_DOWN,
  META_PAD_DIRECTION_CW,
  META_PAD_DIRECTION_CCW,
} MetaPadDirection;

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
    MetaPadActionType action;
    guint number;
    double value;
  } last_pad_action_info;
};

G_DEFINE_TYPE (MetaPadActionMapper, meta_pad_action_mapper, G_TYPE_OBJECT)

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
device_added (ClutterSeat         *seat,
              ClutterInputDevice  *device,
              MetaPadActionMapper *mapper)
{
  PadMappingInfo *info;

  if (clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE)
    {
      info = pad_mapping_info_new (device);
      g_hash_table_insert (mapper->pads, device, info);
    }
}

static void
device_removed (ClutterSeat         *seat,
                ClutterInputDevice  *device,
                MetaPadActionMapper *mapper)
{
  g_hash_table_remove (mapper->pads, device);
}

static void
meta_pad_action_mapper_init (MetaPadActionMapper *mapper)
{
  mapper->pads = g_hash_table_new_full (NULL, NULL, NULL,
                                        (GDestroyNotify) pad_mapping_info_free);

  mapper->seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_connect (mapper->seat, "device-added",
                    G_CALLBACK (device_added), mapper);
  g_signal_connect (mapper->seat, "device-removed",
                    G_CALLBACK (device_removed), mapper);
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
lookup_pad_action_settings (ClutterInputDevice *device,
                            MetaPadActionType   action,
                            guint               number,
                            MetaPadDirection    direction,
                            int                 mode)
{
  const char *vendor, *product, *action_type, *detail_type = NULL;
  GSettings *settings;
  GString *path;
  char action_label;

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);

  action_label = 'A' + number;

  switch (action)
    {
    case META_PAD_ACTION_BUTTON:
      action_type = "button";
      break;
    case META_PAD_ACTION_RING:
      g_assert (direction == META_PAD_DIRECTION_CW ||
                direction == META_PAD_DIRECTION_CCW);
      action_type = "ring";
      detail_type = (direction == META_PAD_DIRECTION_CW) ? "cw" : "ccw";
      break;
    case META_PAD_ACTION_STRIP:
      g_assert (direction == META_PAD_DIRECTION_UP ||
                direction == META_PAD_DIRECTION_DOWN);
      action_type = "strip";
      detail_type = (direction == META_PAD_DIRECTION_UP) ? "up" : "down";
      break;
    default:
      return NULL;
    }

  path = g_string_new (NULL);
  g_string_append_printf (path, "/org/gnome/desktop/peripherals/tablets/%s:%s/%s%c",
                          vendor, product, action_type, action_label);

  if (detail_type)
    g_string_append_printf (path, "-%s", detail_type);

  if (mode >= 0)
    g_string_append_printf (path, "-mode-%d", mode);

  g_string_append_c (path, '/');

  settings = g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet.pad-button",
                                       path->str);
  g_string_free (path, TRUE);

  return settings;
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

  settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                         button, META_PAD_DIRECTION_NONE, -1);
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
  g_return_if_fail (clutter_input_device_get_device_type (device) == CLUTTER_TABLET_DEVICE ||
                    clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE);

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

      /* Pick an arbitrary monitor in the logical monitor to represent it. */
      monitor = meta_logical_monitor_get_monitors (logical_monitor)->data;
      edid[0] = meta_monitor_get_vendor (monitor);
      edid[1] = meta_monitor_get_product (monitor);
      edid[2] = meta_monitor_get_serial (monitor);
    }
  else
    {
      edid[0] = "";
      edid[1] = "";
      edid[2] = "";
    }

  g_settings_set_strv (info->settings, "output", edid);
  meta_display_show_tablet_mapping_notification (meta_get_display (),
                                                 device, pretty_name);
}

gboolean
meta_pad_action_mapper_is_button_grabbed (MetaPadActionMapper *mapper,
                                          ClutterInputDevice  *pad,
                                          guint                button)
{
  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), FALSE);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), FALSE);
  g_return_val_if_fail (clutter_input_device_get_device_type (pad) ==
                        CLUTTER_PAD_DEVICE, FALSE);

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
    { CLUTTER_MOD1_MASK, CLUTTER_KEY_Meta_L }
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
  guint key, mods;

  if (!accel || !*accel)
    return;

  /* FIXME: This is appalling */
  gtk_accelerator_parse (accel, &key, &mods);

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
    emulate_modifiers (mapper->virtual_pad_keyboard, mods, state);

  clutter_virtual_input_device_notify_keyval (mapper->virtual_pad_keyboard,
                                              clutter_get_current_event_time (),
                                              key, state);
  if (!is_press)
    emulate_modifiers (mapper->virtual_pad_keyboard, mods, state);
}

static gboolean
meta_pad_action_mapper_handle_button (MetaPadActionMapper         *mapper,
                                      ClutterInputDevice          *pad,
                                      const ClutterPadButtonEvent *event)
{
  GDesktopPadButtonAction action;
  int button, group, mode;
  gboolean is_press;
  GSettings *settings;
  char *accel;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), FALSE);
  g_return_val_if_fail (event->type == CLUTTER_PAD_BUTTON_PRESS ||
                        event->type == CLUTTER_PAD_BUTTON_RELEASE, FALSE);

  button = event->button;
  mode = event->mode;
  group = clutter_input_device_get_mode_switch_button_group (pad, button);
  is_press = event->type == CLUTTER_PAD_BUTTON_PRESS;

  if (is_press && group >= 0)
    {
      guint n_modes = clutter_input_device_get_group_n_modes (pad, group);
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
      meta_display_notify_pad_group_switch (meta_get_display (), pad,
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
        meta_display_request_pad_osd (meta_get_display (), pad, FALSE);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                             button, META_PAD_DIRECTION_NONE, -1);
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
meta_pad_action_mapper_handle_action (MetaPadActionMapper *mapper,
                                      ClutterInputDevice  *pad,
                                      MetaPadActionType    action,
                                      guint                number,
                                      MetaPadDirection     direction,
                                      guint                mode)
{
  GSettings *settings;
  gboolean handled = FALSE;
  char *accel;

  settings = lookup_pad_action_settings (pad, action, number, direction, mode);
  accel = g_settings_get_string (settings, "keybinding");

  if (accel && *accel)
    {
      meta_pad_action_mapper_emulate_keybinding (mapper, accel, TRUE);
      meta_pad_action_mapper_emulate_keybinding (mapper, accel, FALSE);
      handled = TRUE;
    }

  g_object_unref (settings);
  g_free (accel);

  return handled;
}

static gboolean
meta_pad_action_mapper_get_action_direction (MetaPadActionMapper *mapper,
                                             const ClutterEvent  *event,
                                             MetaPadDirection    *direction)
{
  ClutterInputDevice *pad = clutter_event_get_device (event);
  MetaPadActionType pad_action;
  gboolean has_direction = FALSE;
  MetaPadDirection inc_dir, dec_dir;
  guint number;
  double value;

  *direction = META_PAD_DIRECTION_NONE;

  switch (event->type)
    {
    case CLUTTER_PAD_RING:
      pad_action = META_PAD_ACTION_RING;
      number = event->pad_ring.ring_number;
      value = event->pad_ring.angle;
      inc_dir = META_PAD_DIRECTION_CW;
      dec_dir = META_PAD_DIRECTION_CCW;
      break;
    case CLUTTER_PAD_STRIP:
      pad_action = META_PAD_ACTION_STRIP;
      number = event->pad_strip.strip_number;
      value = event->pad_strip.value;
      inc_dir = META_PAD_DIRECTION_DOWN;
      dec_dir = META_PAD_DIRECTION_UP;
      break;
    default:
      return FALSE;
    }

  if (mapper->last_pad_action_info.pad == pad &&
      mapper->last_pad_action_info.action == pad_action &&
      mapper->last_pad_action_info.number == number &&
      value >= 0 && mapper->last_pad_action_info.value >= 0)
    {
      *direction = (value - mapper->last_pad_action_info.value) > 0 ?
        inc_dir : dec_dir;
      has_direction = TRUE;
    }

  mapper->last_pad_action_info.pad = pad;
  mapper->last_pad_action_info.action = pad_action;
  mapper->last_pad_action_info.number = number;
  mapper->last_pad_action_info.value = value;
  return has_direction;
}

gboolean
meta_pad_action_mapper_handle_event (MetaPadActionMapper *mapper,
                                     const ClutterEvent  *event)
{
  ClutterInputDevice *pad;
  MetaPadDirection direction = META_PAD_DIRECTION_NONE;

  pad = clutter_event_get_source_device ((ClutterEvent *) event);

  switch (event->type)
    {
    case CLUTTER_PAD_BUTTON_PRESS:
    case CLUTTER_PAD_BUTTON_RELEASE:
      return meta_pad_action_mapper_handle_button (mapper, pad,
                                                   &event->pad_button);
    case CLUTTER_PAD_RING:
      if (!meta_pad_action_mapper_get_action_direction (mapper,
                                                        event, &direction))
        return FALSE;
      return meta_pad_action_mapper_handle_action (mapper, pad,
                                                   META_PAD_ACTION_RING,
                                                   event->pad_ring.ring_number,
                                                   direction,
                                                   event->pad_ring.mode);
    case CLUTTER_PAD_STRIP:
      if (!meta_pad_action_mapper_get_action_direction (mapper,
                                                        event, &direction))
        return FALSE;
      return meta_pad_action_mapper_handle_action (mapper, pad,
                                                   META_PAD_ACTION_STRIP,
                                                   event->pad_strip.strip_number,
                                                   direction,
                                                   event->pad_strip.mode);
    default:
      return FALSE;
    }
}


static char *
compose_directional_action_label (GSettings *direction1,
                                  GSettings *direction2)
{
  char *accel1, *accel2, *str = NULL;

  accel1 = g_settings_get_string (direction1, "keybinding");
  accel2 = g_settings_get_string (direction2, "keybinding");

  if (accel1 && *accel1 && accel2 && *accel2)
    str = g_strdup_printf ("%s / %s", accel1, accel2);

  g_free (accel1);
  g_free (accel2);

  return str;
}

static char *
meta_pad_action_mapper_get_ring_label (MetaPadActionMapper *mapper,
                                       ClutterInputDevice  *pad,
                                       guint                number,
                                       guint                mode)
{
  GSettings *settings1, *settings2;
  char *label;

  /* We only allow keybinding actions with those */
  settings1 = lookup_pad_action_settings (pad, META_PAD_ACTION_RING, number,
                                          META_PAD_DIRECTION_CW, mode);
  settings2 = lookup_pad_action_settings (pad, META_PAD_ACTION_RING, number,
                                          META_PAD_DIRECTION_CCW, mode);
  label = compose_directional_action_label (settings1, settings2);
  g_object_unref (settings1);
  g_object_unref (settings2);

  return label;
}

static char *
meta_pad_action_mapper_get_strip_label (MetaPadActionMapper *mapper,
                                        ClutterInputDevice  *pad,
                                        guint                number,
                                        guint                mode)
{
  GSettings *settings1, *settings2;
  char *label;

  /* We only allow keybinding actions with those */
  settings1 = lookup_pad_action_settings (pad, META_PAD_ACTION_STRIP, number,
                                          META_PAD_DIRECTION_UP, mode);
  settings2 = lookup_pad_action_settings (pad, META_PAD_ACTION_STRIP, number,
                                          META_PAD_DIRECTION_DOWN, mode);
  label = compose_directional_action_label (settings1, settings2);
  g_object_unref (settings1);
  g_object_unref (settings2);

  return label;
}

static char *
meta_pad_action_mapper_get_button_label (MetaPadActionMapper *mapper,
                                         ClutterInputDevice *pad,
                                         guint               button)
{
  GDesktopPadButtonAction action;
  int group;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (pad), NULL);
  g_return_val_if_fail (clutter_input_device_get_device_type (pad) ==
                        CLUTTER_PAD_DEVICE, NULL);

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

        settings = lookup_pad_action_settings (pad, META_PAD_ACTION_BUTTON,
                                               button, META_PAD_DIRECTION_NONE, -1);
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
                      MetaPadActionType    action_type,
                      guint                number)
{
  PadMappingInfo *info;
  guint group = 0, n_groups;

  info = g_hash_table_lookup (mapper->pads, pad);
  n_groups = clutter_input_device_get_n_mode_groups (pad);

  if (!info->group_modes || n_groups == 0)
    return 0;

  if (action_type == META_PAD_ACTION_RING ||
      action_type == META_PAD_ACTION_STRIP)
    {
      /* Assume features are evenly distributed in groups */
      group = number % n_groups;
    }

  return info->group_modes[group];
}

char *
meta_pad_action_mapper_get_action_label (MetaPadActionMapper *mapper,
                                         ClutterInputDevice  *pad,
                                         MetaPadActionType    action_type,
                                         guint                number)
{
  guint mode;

  switch (action_type)
    {
    case META_PAD_ACTION_BUTTON:
      return meta_pad_action_mapper_get_button_label (mapper, pad, number);
    case META_PAD_ACTION_RING:
      mode = get_current_pad_mode (mapper, pad, action_type, number);
      return meta_pad_action_mapper_get_ring_label (mapper, pad, number, mode);
    case META_PAD_ACTION_STRIP:
      mode = get_current_pad_mode (mapper, pad, action_type, number);
      return meta_pad_action_mapper_get_strip_label (mapper, pad, number, mode);
    }

  return NULL;
}
