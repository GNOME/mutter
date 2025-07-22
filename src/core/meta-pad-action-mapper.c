/*
 * Copyright (C) 2014-2024 Red Hat
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
#include <gdesktop-enums.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

#include "core/meta-pad-action-mapper.h"
#include "backends/meta-input-device-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "core/display-private.h"

typedef struct _PadMappingInfo PadMappingInfo;

struct _PadMappingInfo
{
  ClutterInputDevice *device;
  guint *group_modes;
};

struct _MetaPadActionMapper
{
  MetaTabletActionMapper parent_instance;

  GHashTable *pads;

  /* Pad ring/strip emission */
  struct {
    ClutterInputDevice *pad;
    MetaPadFeatureType feature;
    guint number;
    double value;
  } last_pad_action_info;
};

G_DEFINE_TYPE (MetaPadActionMapper, meta_pad_action_mapper, META_TYPE_TABLET_ACTION_MAPPER);

static gboolean
meta_pad_action_mapper_handle_event (MetaTabletActionMapper *mapper,
                                     const ClutterEvent     *event);
static void
device_added (MetaTabletActionMapper *mapper,
              ClutterInputDevice     *device);
static void
device_removed (MetaTabletActionMapper *mapper,
                ClutterInputDevice     *device);

static void
meta_pad_action_mapper_finalize (GObject *object)
{
  MetaPadActionMapper *mapper = META_PAD_ACTION_MAPPER (object);

  g_hash_table_unref (mapper->pads);

  G_OBJECT_CLASS (meta_pad_action_mapper_parent_class)->finalize (object);
}

static void
meta_pad_action_mapper_class_init (MetaPadActionMapperClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_pad_action_mapper_finalize;
}

static PadMappingInfo *
pad_mapping_info_new (ClutterInputDevice *pad)
{
  PadMappingInfo *info;

  info = g_new0 (PadMappingInfo, 1);
  info->device = pad;
  info->group_modes =
    g_new0 (guint, clutter_input_device_get_n_mode_groups (pad));

  return info;
}

static void
pad_mapping_info_free (PadMappingInfo *info)
{
  g_free (info->group_modes);
  g_free (info);
}

static void
device_added (MetaTabletActionMapper *tablet_mapper,
              ClutterInputDevice     *device)
{
  MetaPadActionMapper *mapper = META_PAD_ACTION_MAPPER (tablet_mapper);
  PadMappingInfo *info;

  if ((clutter_input_device_get_capabilities (device) &
       CLUTTER_INPUT_CAPABILITY_TABLET_PAD) != 0)
    {
      info = pad_mapping_info_new (device);
      g_hash_table_insert (mapper->pads, device, info);
    }
}

static void
device_removed (MetaTabletActionMapper *tablet_mapper,
                ClutterInputDevice     *device)
{
  MetaPadActionMapper *mapper = META_PAD_ACTION_MAPPER (tablet_mapper);

  g_hash_table_remove (mapper->pads, device);
}

static void
meta_pad_action_mapper_init (MetaPadActionMapper *mapper)
{
  g_signal_connect (mapper, "device-added", G_CALLBACK (device_added), NULL);
  g_signal_connect (mapper, "device-removed", G_CALLBACK (device_removed), NULL);
  g_signal_connect (mapper, "input-event", G_CALLBACK (meta_pad_action_mapper_handle_event), NULL);

  mapper->pads = g_hash_table_new_full (NULL, NULL, NULL,
                                        (GDestroyNotify) pad_mapping_info_free);
}

MetaPadActionMapper *
meta_pad_action_mapper_new (MetaMonitorManager *monitor_manager)
{
  MetaPadActionMapper *action_mapper;

  action_mapper = g_object_new (META_TYPE_PAD_ACTION_MAPPER,
                                "monitor_manager", monitor_manager,
                                NULL);

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
  guint vendor, product;
  char tag;

  tag = 'A' + feature_number;
  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);

  path = g_strdup_printf ("/org/gnome/desktop/peripherals/tablets/%.4x:%.4x/%s%c%s/",
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
    case META_PAD_FEATURE_DIAL:
      g_assert (direction == META_PAD_DIRECTION_CW ||
                direction == META_PAD_DIRECTION_CCW);
      feature_type = "dial";
      detail_type = (direction == META_PAD_DIRECTION_CW) ? "cw" : "ccw";
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

static gboolean
meta_pad_action_mapper_handle_button (MetaPadActionMapper *mapper,
                                      ClutterInputDevice  *pad,
                                      const ClutterEvent  *event)
{
  MetaTabletActionMapper *tablet_mapper = META_TABLET_ACTION_MAPPER (mapper);
  MetaTabletActionMapperClass *tablet_klass = META_TABLET_ACTION_MAPPER_GET_CLASS (mapper);
  GDesktopPadButtonAction action;
  int group, n_modes = 0;
  gboolean is_press;
  GSettings *settings;
  char *accel;
  uint32_t button, mode;
  MetaDisplay *display;

  g_return_val_if_fail (META_IS_PAD_ACTION_MAPPER (mapper), FALSE);
  g_return_val_if_fail (clutter_event_type (event) == CLUTTER_PAD_BUTTON_PRESS ||
                        clutter_event_type (event) == CLUTTER_PAD_BUTTON_RELEASE, FALSE);

  clutter_event_get_pad_details (event, &button, &mode, NULL, NULL);
  group = clutter_input_device_get_mode_switch_button_group (pad, button);
  is_press = clutter_event_type (event) == CLUTTER_PAD_BUTTON_PRESS;
  display = tablet_klass->get_display (tablet_mapper);

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
      meta_display_notify_pad_group_switch (display, pad,
                                            pretty_name, group, mode, n_modes);
      info->group_modes[group] = mode;
    }

  action = meta_pad_action_mapper_get_button_action (mapper, pad, button);

  switch (action)
    {
    case G_DESKTOP_PAD_BUTTON_ACTION_SWITCH_MONITOR:
      if (is_press)
        tablet_klass->cycle_tablet_output (tablet_mapper, pad);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_HELP:
      if (is_press)
        meta_display_request_pad_osd (display, pad, FALSE);
      return TRUE;
    case G_DESKTOP_PAD_BUTTON_ACTION_KEYBINDING:
      settings = lookup_pad_button_settings (pad, button);
      accel = g_settings_get_string (settings, "keybinding");
      tablet_klass->emulate_keybinding (tablet_mapper, accel, is_press);
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
  gboolean detect_wraparound = FALSE;
  gboolean value_in_range = FALSE;
  gboolean is_relative = FALSE;

  switch (clutter_event_type (event))
    {
    case CLUTTER_PAD_RING:
      pad_feature = META_PAD_FEATURE_RING;
      clutter_event_get_pad_details (event, &number, NULL, NULL, &value);
      inc_dir = META_PAD_DIRECTION_CW;
      dec_dir = META_PAD_DIRECTION_CCW;
      detect_wraparound = TRUE;
      value_in_range = value >= 0.0 && mapper->last_pad_action_info.value >= 0;
      break;
    case CLUTTER_PAD_STRIP:
      pad_feature = META_PAD_FEATURE_STRIP;
      clutter_event_get_pad_details (event, &number, NULL, NULL, &value);
      inc_dir = META_PAD_DIRECTION_DOWN;
      dec_dir = META_PAD_DIRECTION_UP;
      value_in_range = value >= 0.0 && mapper->last_pad_action_info.value >= 0;
      break;
    case CLUTTER_PAD_DIAL:
      pad_feature = META_PAD_FEATURE_DIAL;
      clutter_event_get_pad_details (event, &number, NULL, NULL, &value);
      inc_dir = META_PAD_DIRECTION_CW;
      dec_dir = META_PAD_DIRECTION_CCW;
      is_relative = TRUE;
      value_in_range = value != 0.0;
      break;
    default:
      return FALSE;
    }

  if (mapper->last_pad_action_info.pad == pad &&
      mapper->last_pad_action_info.feature == pad_feature &&
      mapper->last_pad_action_info.number == number &&
      value_in_range)
    {
      double delta;

      if (is_relative)
        {
          delta = value;
        }
      else
        {
          delta = value - mapper->last_pad_action_info.value;

          if (detect_wraparound)
            {
              if (delta < -180.0)
                delta += 360;
              else if (delta > 180.0)
                delta -= 360;
            }
        }
      *direction = delta > 0 ?  inc_dir : dec_dir;
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
  else if (feature == META_PAD_FEATURE_DIAL)
    {
      settings1 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_CW, mode);
      settings2 = lookup_pad_feature_settings (pad, feature, number,
                                               META_PAD_DIRECTION_CCW, mode);
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
          MetaTabletActionMapper *parent = META_TABLET_ACTION_MAPPER (mapper);
          MetaTabletActionMapperClass *klass = META_TABLET_ACTION_MAPPER_GET_CLASS (parent);
          klass->emulate_keybinding (parent, accel, TRUE);
          klass->emulate_keybinding (parent, accel, FALSE);
        }
    }

  return handled;
}

static gboolean
meta_pad_action_mapper_handle_event (MetaTabletActionMapper *tablet_mapper,
                                     const ClutterEvent     *event)
{
  MetaPadActionMapper *mapper = META_PAD_ACTION_MAPPER (tablet_mapper);
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
    case CLUTTER_PAD_DIAL:
      clutter_event_get_pad_details (event, &number, &mode, NULL, NULL);
      return meta_pad_action_mapper_handle_action (mapper, pad, event,
                                                   META_PAD_FEATURE_DIAL,
                                                   number, mode);
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

static char *
meta_pad_action_mapper_get_dial_label (MetaPadActionMapper *mapper,
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

  settings = lookup_pad_feature_settings (pad, META_PAD_FEATURE_DIAL,
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
      int n_groups = clutter_input_device_get_n_mode_groups (pad);
      if (n_groups > 1)
        {
          /* TRANSLATORS: This string refers to a button that switches between
           * different modes in that button group.
           */
          return g_strdup_printf (_("Mode Switch (Group %d)"), group);
        }
      else
        {
          /* TRANSLATORS: This string refers to a button that switches between
           * different modes.
           */
          return g_strdup_printf (_("Mode Switch"));
        }
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
      feature == META_PAD_FEATURE_STRIP ||
      feature == META_PAD_FEATURE_DIAL)
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
    case META_PAD_FEATURE_DIAL:
      mode = get_current_pad_mode (mapper, pad, feature, number);
      return meta_pad_action_mapper_get_dial_label (mapper, pad, number, mode, direction);
    }

  return NULL;
}
