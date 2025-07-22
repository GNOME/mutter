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

#include "core/meta-tablet-action-mapper.h"
#include "backends/meta-input-device-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "core/display-private.h"

typedef struct _TabletMappingInfo TabletMappingInfo;

enum
{
  PROP_MONITOR_MANAGER = 1,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

enum
{
  DEVICE_ADDED,
  DEVICE_REMOVED,
  INPUT_EVENT,

  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _TabletMappingInfo
{
  ClutterInputDevice *device;
  GSettings *settings;
};

struct _MetaTabletActionMapperPrivate
{
  GObject parent_class;

  GHashTable *tablets;
  ClutterSeat *seat;
  ClutterVirtualInputDevice *virtual_tablet_keyboard;
  MetaMonitorManager *monitor_manager;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaTabletActionMapper, meta_tablet_action_mapper, G_TYPE_OBJECT)

static void
meta_tablet_action_mapper_constructed (GObject *object);

static void
meta_tablet_action_mapper_cycle_tablet_output (MetaTabletActionMapper *mapper,
                                               ClutterInputDevice     *device);

static void
meta_tablet_action_mapper_emulate_keybinding (MetaTabletActionMapper *mapper,
                                              const char             *accel,
                                              gboolean                is_press);

static MetaDisplay *
meta_tablet_action_mapper_get_display (MetaTabletActionMapper *mapper)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);
  MetaBackend *backend =
    meta_monitor_manager_get_backend (priv->monitor_manager);
  MetaContext *context = meta_backend_get_context (backend);

  return meta_context_get_display (context);
}

static void
meta_tablet_action_mapper_finalize (GObject *object)
{
  MetaTabletActionMapper *mapper = META_TABLET_ACTION_MAPPER (object);
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);

  g_hash_table_unref (priv->tablets);
  g_object_unref (priv->monitor_manager);
  g_clear_object (&priv->virtual_tablet_keyboard);

  G_OBJECT_CLASS (meta_tablet_action_mapper_parent_class)->finalize (object);
}

static void
meta_tablet_action_mapper_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaTabletActionMapper *mapper = META_TABLET_ACTION_MAPPER (object);
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);

  switch (property_id)
    {
    case PROP_MONITOR_MANAGER:
      g_value_set_object (value, priv->monitor_manager);
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
meta_tablet_action_mapper_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaTabletActionMapper *mapper = META_TABLET_ACTION_MAPPER (object);
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);

  switch (property_id)
    {
    case PROP_MONITOR_MANAGER:
      if (priv->monitor_manager)
        g_object_unref (priv->monitor_manager);
      priv->monitor_manager = g_object_ref (g_value_get_object (value));
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
meta_tablet_action_mapper_class_init (MetaTabletActionMapperClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_tablet_action_mapper_get_property;
  object_class->set_property = meta_tablet_action_mapper_set_property;
  object_class->constructed = meta_tablet_action_mapper_constructed;
  object_class->finalize = meta_tablet_action_mapper_finalize;

  klass->get_display = meta_tablet_action_mapper_get_display;
  klass->emulate_keybinding = meta_tablet_action_mapper_emulate_keybinding;
  klass->cycle_tablet_output = meta_tablet_action_mapper_cycle_tablet_output;

  signals[DEVICE_ADDED] = g_signal_new ("device-added",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL, NULL, NULL,
                                        G_TYPE_NONE, 1,
                                        CLUTTER_TYPE_INPUT_DEVICE);
  signals[DEVICE_REMOVED] = g_signal_new ("device-removed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL, NULL,
                                          G_TYPE_NONE, 1,
                                          CLUTTER_TYPE_INPUT_DEVICE);

  signals[INPUT_EVENT] = g_signal_new ("input-event",
                                       G_TYPE_FROM_CLASS (klass),
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL, NULL, NULL,
                                       G_TYPE_BOOLEAN, 1,
                                       CLUTTER_TYPE_EVENT);

  obj_properties[PROP_MONITOR_MANAGER] =
    g_param_spec_object ("monitor-manager", NULL, NULL,
                         META_TYPE_MONITOR_MANAGER,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class,
                                     N_PROPERTIES,
                                     obj_properties);
}

static GSettings *
lookup_device_settings (ClutterInputDevice *device)
{
  guint vendor, product;
  GSettings *settings;
  char *path;

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);
  path = g_strdup_printf ("/org/gnome/desktop/peripherals/tablets/%.4x:%.4x/",
                          vendor, product);

  settings = g_settings_new_with_path ("org.gnome.desktop.peripherals.tablet",
                                       path);
  g_free (path);

  return settings;
}

static TabletMappingInfo *
tablet_mapping_info_new (ClutterInputDevice *tablet)
{
  TabletMappingInfo *info;

  info = g_new0 (TabletMappingInfo, 1);
  info->device = tablet;
  info->settings = lookup_device_settings (tablet);

  return info;
}

static void
tablet_mapping_info_free (TabletMappingInfo *info)
{
  g_object_unref (info->settings);
  g_free (info);
}

static void
device_added (MetaTabletActionMapper *mapper,
              ClutterInputDevice     *device)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);
  TabletMappingInfo *info;

  if ((clutter_input_device_get_capabilities (device) &
       (CLUTTER_INPUT_CAPABILITY_TABLET_TOOL |
        CLUTTER_INPUT_CAPABILITY_TABLET_PAD)) != 0)
    {
      info = tablet_mapping_info_new (device);
      g_hash_table_insert (priv->tablets, device, info);
    }
}

static void
device_removed (MetaTabletActionMapper *mapper,
                ClutterInputDevice     *device)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);

  g_hash_table_remove (priv->tablets, device);
}

static void
meta_tablet_action_mapper_init (MetaTabletActionMapper *mapper)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);

  g_signal_connect (mapper, "device-added", G_CALLBACK (device_added), NULL);
  g_signal_connect (mapper, "device-removed", G_CALLBACK (device_removed), NULL);

  priv->tablets = g_hash_table_new_full (NULL, NULL, NULL,
                                         (GDestroyNotify) tablet_mapping_info_free);
}

static void
meta_tablet_action_mapper_constructed (GObject *object)
{
  MetaTabletActionMapper *mapper = META_TABLET_ACTION_MAPPER (object);
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);
  MetaBackend *backend =
    meta_monitor_manager_get_backend (priv->monitor_manager);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  g_autoptr (GList) devices = NULL;
  GList *l;

  priv->seat = clutter_backend_get_default_seat (clutter_backend);
  devices = clutter_seat_list_devices (priv->seat);

  /* FIXME: is this call correct?? */
  for (l = devices; l; l = l->next)
    g_signal_emit (mapper, signals[DEVICE_ADDED], 0, l->data);
}

static gboolean
cycle_logical_monitors (MetaTabletActionMapperPrivate  *mapper,
                        gboolean                        skip_all_monitors,
                        MetaLogicalMonitor             *current_logical_monitor,
                        MetaLogicalMonitor            **next_logical_monitor)
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
meta_tablet_action_mapper_find_monitor (MetaTabletActionMapperPrivate  *mapper,
                                        GSettings                      *settings,
                                        ClutterInputDevice             *device,
                                        MetaMonitor                   **out_monitor,
                                        MetaLogicalMonitor            **out_logical_monitor)
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
meta_tablet_action_mapper_cycle_tablet_output (MetaTabletActionMapper *mapper,
                                               ClutterInputDevice     *device)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);
  TabletMappingInfo *info;
  MetaLogicalMonitor *logical_monitor = NULL;
  const char *edid[4] = { 0 }, *pretty_name = NULL;
  gboolean is_integrated_device = FALSE;
#ifdef HAVE_LIBWACOM
  WacomDevice *wacom_device;
#endif

  g_return_if_fail (META_IS_TABLET_ACTION_MAPPER (mapper));
  g_return_if_fail (CLUTTER_IS_INPUT_DEVICE (device));
  g_return_if_fail ((clutter_input_device_get_capabilities (device) &
                     (CLUTTER_INPUT_CAPABILITY_TABLET_TOOL |
                      CLUTTER_INPUT_CAPABILITY_TABLET_PAD)) != 0);

  info = g_hash_table_lookup (priv->tablets, device);
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

  meta_tablet_action_mapper_find_monitor (priv, info->settings, device,
                                          NULL, &logical_monitor);

  if (!cycle_logical_monitors (priv,
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
  meta_display_show_tablet_mapping_notification (meta_tablet_action_mapper_get_display (mapper),
                                                 device, pretty_name);
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
    { CLUTTER_MOD1_MASK, CLUTTER_KEY_Alt_L },
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
meta_tablet_action_mapper_emulate_keybinding (MetaTabletActionMapper *mapper,
                                              const char             *accel,
                                              gboolean                is_press)
{
  MetaTabletActionMapperPrivate *priv =
    meta_tablet_action_mapper_get_instance_private (mapper);
  MetaBackend *backend =
    meta_monitor_manager_get_backend (priv->monitor_manager);
  ClutterKeyState state;
  MetaKeyCombo combo = { 0 };

  if (!accel || !*accel)
    return;

  if (!meta_parse_accelerator (accel, &combo))
    {
      g_warning ("\"%s\" is not a valid accelerator", accel);
      return;
    }

  if (!priv->virtual_tablet_keyboard)
    {
      ClutterBackend *clutter_backend;
      ClutterSeat *seat;

      clutter_backend = meta_backend_get_clutter_backend (backend);
      seat = clutter_backend_get_default_seat (clutter_backend);

      priv->virtual_tablet_keyboard =
        clutter_seat_create_virtual_device (seat,
                                            CLUTTER_KEYBOARD_DEVICE);
    }

  state = is_press ? CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;

  if (is_press)
    emulate_modifiers (priv->virtual_tablet_keyboard, combo.modifiers, state);

  clutter_virtual_input_device_notify_keyval (priv->virtual_tablet_keyboard,
                                              clutter_get_current_event_time (),
                                              combo.keysym, state);
  if (!is_press)
    emulate_modifiers (priv->virtual_tablet_keyboard, combo.modifiers, state);
}

gboolean
meta_tablet_action_mapper_handle_event (MetaTabletActionMapper *mapper,
                                        const ClutterEvent     *event)
{
  ClutterInputDevice *device;
  gboolean propagate = CLUTTER_EVENT_PROPAGATE;

  switch (clutter_event_type (event))
    {
    case CLUTTER_DEVICE_ADDED:
      device = clutter_event_get_source_device (event);
      g_signal_emit (mapper, signals[DEVICE_ADDED], 0, device);
      break;
    case CLUTTER_DEVICE_REMOVED:
      device = clutter_event_get_source_device (event);
      g_signal_emit (mapper, signals[DEVICE_REMOVED], 0, device);
      break;
    default:
      g_signal_emit (mapper, signals[INPUT_EVENT], 0, event, &propagate);
      break;
    }

  return propagate;
}
