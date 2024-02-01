/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright 2018 Red Hat, Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "backends/meta-input-device-private.h"
#include "backends/meta-input-mapper-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-backend-private.h"

#include "meta-dbus-input-mapping.h"

#define META_INPUT_MAPPING_DBUS_SERVICE "org.gnome.Mutter.InputMapping"
#define META_INPUT_MAPPING_DBUS_PATH "/org/gnome/Mutter/InputMapping"

#define MAX_SIZE_MATCH_DIFF 0.05

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaMapperInputInfo MetaMapperInputInfo;
typedef struct _MetaMapperOutputInfo MetaMapperOutputInfo;
typedef struct _MappingHelper MappingHelper;
typedef struct _DeviceCandidates DeviceCandidates;
typedef struct _DeviceMatch DeviceMatch;

struct _MetaInputMapper
{
  MetaDBusInputMappingSkeleton parent_instance;

  MetaBackend *backend;
  MetaMonitorManager *monitor_manager;
  ClutterSeat *seat;
  GHashTable *input_devices; /* ClutterInputDevice -> MetaMapperInputInfo */
  GHashTable *output_devices; /* MetaLogicalMonitor -> MetaMapperOutputInfo */
  guint dbus_name_id;
};

typedef enum
{
  META_MATCH_EDID_VENDOR,  /* EDID vendor match, eg. "WAC" for Wacom */
  META_MATCH_EDID_PARTIAL, /* Partial EDID model match, eg. "Cintiq" */
  META_MATCH_EDID_FULL,    /* Full EDID model match, eg. "Cintiq 12WX" */
  META_MATCH_SIZE,         /* Size from input device and output match */
  META_MATCH_IS_BUILTIN,   /* Output is builtin, applies mainly to system-integrated devices */
  META_MATCH_CONFIG,       /* Specified by config */
  N_OUTPUT_MATCHES
} MetaOutputMatchType;

struct _MetaMapperInputInfo
{
  ClutterInputDevice *device;
  MetaInputMapper *mapper;
  MetaMapperOutputInfo *output;
  GSettings *settings;
  guint builtin : 1;
};

struct _MetaMapperOutputInfo
{
  MetaLogicalMonitor *logical_monitor;
  GList *input_devices;
};

struct _MappingHelper
{
  GArray *device_maps;
};

struct _DeviceMatch
{
  MetaMonitor *monitor;
  uint32_t score;
};

struct _DeviceCandidates
{
  MetaMapperInputInfo *input;

  GArray *matches; /* Array of DeviceMatch */

  int best;
};

enum
{
  DEVICE_MAPPED,
  DEVICE_ENABLED,
  DEVICE_ASPECT_RATIO,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0, };

static void mapper_output_info_remove_input (MetaMapperOutputInfo *output,
                                             MetaMapperInputInfo  *input);

static void mapper_recalculate_input (MetaInputMapper     *mapper,
                                      MetaMapperInputInfo *input);

static void meta_input_mapping_init_iface (MetaDBusInputMappingIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaInputMapper, meta_input_mapper,
                         META_DBUS_TYPE_INPUT_MAPPING_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_INPUT_MAPPING,
                                                meta_input_mapping_init_iface))

static GSettings *
get_device_settings (ClutterInputDevice *device)
{
  const char *group, *schema, *vendor, *product;
  ClutterInputDeviceType type;
  GSettings *settings;
  char *path;

  type = clutter_input_device_get_device_type (device);

  if (type == CLUTTER_TOUCHSCREEN_DEVICE)
    {
      group = "touchscreens";
      schema = "org.gnome.desktop.peripherals.touchscreen";
    }
  else if (type == CLUTTER_TABLET_DEVICE ||
           type == CLUTTER_PEN_DEVICE ||
           type == CLUTTER_ERASER_DEVICE ||
           type == CLUTTER_CURSOR_DEVICE ||
           type == CLUTTER_PAD_DEVICE)
    {
      group = "tablets";
      schema = "org.gnome.desktop.peripherals.tablet";
    }
  else
    {
      return NULL;
    }

  vendor = clutter_input_device_get_vendor_id (device);
  product = clutter_input_device_get_product_id (device);
  path = g_strdup_printf ("/org/gnome/desktop/peripherals/%s/%s:%s/",
                          group, vendor, product);

  settings = g_settings_new_with_path (schema, path);
  g_free (path);

  return settings;
}

static void
settings_output_changed_cb (GSettings           *settings,
                            const char          *key,
                            MetaMapperInputInfo *info)
{
  if (info->output != NULL)
    mapper_output_info_remove_input (info->output, info);

  mapper_recalculate_input (info->mapper, info);
}

static MetaMapperInputInfo *
mapper_input_info_new (ClutterInputDevice *device,
                       MetaInputMapper    *mapper)
{
  MetaMapperInputInfo *info;

  info = g_new0 (MetaMapperInputInfo, 1);
  info->mapper = mapper;
  info->device = device;
  info->settings = get_device_settings (device);

  g_signal_connect (info->settings, "changed::output",
                    G_CALLBACK (settings_output_changed_cb), info);

  return info;
}

static void
mapper_input_info_free (MetaMapperInputInfo *info)
{
  g_signal_handlers_disconnect_by_func (info->settings, settings_output_changed_cb, info);
  g_object_unref (info->settings);
  g_free (info);
}

static MetaMapperOutputInfo *
mapper_output_info_new (MetaLogicalMonitor *logical_monitor)
{
  MetaMapperOutputInfo *info;

  info = g_new0 (MetaMapperOutputInfo, 1);
  info->logical_monitor = logical_monitor;

  return info;
}

static void
mapper_output_info_free (MetaMapperOutputInfo *info)
{
  g_free (info);
}

static void
mapper_input_info_set_output (MetaMapperInputInfo  *input,
                              MetaMapperOutputInfo *output,
                              MetaMonitor          *monitor)
{
  MetaInputMapper *mapper = input->mapper;
  float matrix[6] = { 1, 0, 0, 0, 1, 0 };
  double aspect_ratio;
  int width, height;

  if (input->output == output)
    return;

  input->output = output;

  /* These devices don't require emission about mapping/ratio */
  if (clutter_input_device_get_device_type (input->device) == CLUTTER_PAD_DEVICE)
    return;

  if (output && monitor)
    {
      meta_monitor_manager_get_monitor_matrix (mapper->monitor_manager,
                                               monitor,
                                               output->logical_monitor,
                                               matrix);
      meta_monitor_get_current_resolution (monitor, &width, &height);
    }
  else
    {
      meta_monitor_manager_get_screen_size (mapper->monitor_manager,
                                            &width, &height);
    }

  aspect_ratio = (double) width / height;

  g_signal_emit (input->mapper, signals[DEVICE_MAPPED], 0,
                 input->device, matrix);
  g_signal_emit (input->mapper, signals[DEVICE_ASPECT_RATIO], 0,
                 input->device, aspect_ratio);
}

static void
mapper_output_info_add_input (MetaMapperOutputInfo *output,
                              MetaMapperInputInfo  *input,
                              MetaMonitor          *monitor)
{
  g_assert (input->output == NULL);

  output->input_devices = g_list_prepend (output->input_devices, input);
  mapper_input_info_set_output (input, output, monitor);
}

static void
mapper_output_info_remove_input (MetaMapperOutputInfo *output,
                                 MetaMapperInputInfo  *input)
{
  g_assert (input->output == output);

  output->input_devices = g_list_remove (output->input_devices, input);
  mapper_input_info_set_output (input, NULL, NULL);
}

static void
mapper_output_info_clear_inputs (MetaMapperOutputInfo *output)
{
  while (output->input_devices)
    {
      MetaMapperInputInfo *input = output->input_devices->data;

      mapper_input_info_set_output (input, NULL, NULL);
      output->input_devices = g_list_remove (output->input_devices, input);
    }
}

static void
clear_candidates (DeviceCandidates *candidates)
{
  g_clear_pointer (&candidates->matches, g_array_unref);
}

static void
mapping_helper_init (MappingHelper *helper)
{
  helper->device_maps = g_array_new (FALSE, FALSE, sizeof (DeviceCandidates));
  g_array_set_clear_func (helper->device_maps,
                          (GDestroyNotify) clear_candidates);
}

static void
mapping_helper_release (MappingHelper *helper)
{
  g_array_unref (helper->device_maps);
}

static gboolean
match_edid (MetaMapperInputInfo  *input,
            MetaMonitor          *monitor,
            MetaOutputMatchType  *match_type)
{
  const char *dev_name;
  const char *vendor;
  const char *serial;

  dev_name = clutter_input_device_get_device_name (input->device);

  vendor = meta_monitor_get_vendor (monitor);
  if (!vendor || strcasestr (dev_name, vendor) == NULL)
    return FALSE;

  *match_type = META_MATCH_EDID_VENDOR;

  serial = meta_monitor_get_serial (monitor);
  if (!serial || strcasestr (dev_name, serial) != NULL)
    {
      *match_type = META_MATCH_EDID_FULL;
    }
  else
    {
      const char *product;

      product = meta_monitor_get_product (monitor);
      if (product)
        {
          g_auto (GStrv) split = NULL;
          int i;

          split = g_strsplit (product, " ", -1);

          for (i = 0; split[i]; i++)
            {
              if (strcasestr (dev_name, split[i]) != NULL)
                {
                  *match_type = META_MATCH_EDID_PARTIAL;
                  break;
                }
            }
        }
    }

  return TRUE;
}

static gboolean
match_size (MetaMapperInputInfo  *input,
            MetaMonitor          *monitor)
{
  double w_diff, h_diff;
  int o_width, o_height;
  unsigned int i_width, i_height;

  if (!clutter_input_device_get_dimensions (input->device, &i_width, &i_height))
    return FALSE;

  meta_monitor_get_physical_dimensions (monitor, &o_width, &o_height);
  w_diff = ABS (1 - ((double) o_width / i_width));
  h_diff = ABS (1 - ((double) o_height / i_height));

  return w_diff < MAX_SIZE_MATCH_DIFF && h_diff < MAX_SIZE_MATCH_DIFF;
}

static gboolean
match_builtin (MetaInputMapper *mapper,
               MetaMonitor     *monitor)
{
  return monitor == meta_monitor_manager_get_laptop_panel (mapper->monitor_manager);
}

static gboolean
monitor_has_twin (MetaMonitor *monitor,
                  GList       *monitors)
{
  GList *l;

  for (l = monitors; l; l = l->next)
    {
      if (l->data == monitor)
        continue;

      if (g_strcmp0 (meta_monitor_get_vendor (monitor),
                     meta_monitor_get_vendor (l->data)) == 0 &&
          g_strcmp0 (meta_monitor_get_product (monitor),
                     meta_monitor_get_product (l->data)) == 0 &&
          g_strcmp0 (meta_monitor_get_serial (monitor),
                     meta_monitor_get_serial (l->data)) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
match_config (MetaMapperInputInfo *info,
              MetaMonitor         *monitor,
              GList               *monitors)
{
  gboolean match = FALSE;
  char **edid;
  guint n_values;

  edid = g_settings_get_strv (info->settings, "output");
  n_values = g_strv_length (edid);

  if (n_values < 3)
    {
      g_warning ("EDID configuration for device '%s' "
                 "is incorrect, must have at least 3 values",
                 clutter_input_device_get_device_name (info->device));
      goto out;
    }

  if (!*edid[0] && !*edid[1] && !*edid[2])
    goto out;

  match = (g_strcmp0 (meta_monitor_get_vendor (monitor), edid[0]) == 0 &&
           g_strcmp0 (meta_monitor_get_product (monitor), edid[1]) == 0 &&
           g_strcmp0 (meta_monitor_get_serial (monitor), edid[2]) == 0);

  if (match && n_values >= 4 && monitor_has_twin (monitor, monitors))
    {
      /* The 4th value if set contains the ID (e.g. HDMI-1), use it
       * for disambiguation if multiple monitors with the same
       * EDID data are found.
       */
      MetaOutput *output;
      output = meta_monitor_get_main_output (monitor);
      match = g_strcmp0 (meta_output_get_name (output), edid[3]) == 0;
    }

 out:
  g_strfreev (edid);

  return match;
}

static int
sort_by_score (DeviceMatch *match1,
               DeviceMatch *match2)
{
  return (int) match2->score - match1->score;
}

static void
guess_candidates (MetaInputMapper     *mapper,
                  MetaMapperInputInfo *input,
                  DeviceCandidates    *info)
{
  GList *monitors, *l;
  gboolean builtin = FALSE;
  gboolean integrated = TRUE;
  gboolean automatic;
  g_autoptr (GVariant) user_value = NULL;

#ifdef HAVE_LIBWACOM
  if (clutter_input_device_get_device_type (input->device) != CLUTTER_TOUCHSCREEN_DEVICE)
    {
      WacomDevice *wacom_device;
      WacomIntegrationFlags flags = 0;

      wacom_device =
        meta_input_device_get_wacom_device (META_INPUT_DEVICE (input->device));

      if (wacom_device)
        {
          flags = libwacom_get_integration_flags (wacom_device);

          integrated = (flags & (WACOM_DEVICE_INTEGRATED_SYSTEM |
                                 WACOM_DEVICE_INTEGRATED_DISPLAY)) != 0;
          builtin = (flags & WACOM_DEVICE_INTEGRATED_SYSTEM) != 0;
        }
    }
#endif

  user_value = g_settings_get_user_value (input->settings, "output");
  automatic = user_value == NULL;

  monitors = meta_monitor_manager_get_monitors (mapper->monitor_manager);

  for (l = monitors; l; l = l->next)
    {
      MetaOutputMatchType edid_match;
      DeviceMatch match = { l->data, 0 };

      g_assert (META_IS_MONITOR (l->data));

      if (automatic && integrated && match_edid (input, l->data, &edid_match))
        match.score |= 1 << edid_match;

      if (automatic && integrated && match_size (input, l->data))
        match.score |= 1 << META_MATCH_SIZE;

      if (automatic && builtin && match_builtin (mapper, l->data))
        match.score |= 1 << META_MATCH_IS_BUILTIN;

      if (!automatic && match_config (input, l->data, monitors))
        match.score |= 1 << META_MATCH_CONFIG;

      if (match.score > 0)
        g_array_append_val (info->matches, match);
    }

  if (info->matches->len == 0)
    {
      if (clutter_input_device_get_device_type (input->device) ==
          CLUTTER_TOUCHSCREEN_DEVICE)
        {
          DeviceMatch match = { 0 };

          match.monitor =
            meta_monitor_manager_get_laptop_panel (mapper->monitor_manager);

          if (match.monitor != NULL)
            g_array_append_val (info->matches, match);
        }

      info->best = 0;
    }
  else
    {
      DeviceMatch *best;

      g_array_sort (info->matches, (GCompareFunc) sort_by_score);
      best = &g_array_index (info->matches, DeviceMatch, 0);
      info->best = best->score;
    }
}

static void
mapping_helper_add (MappingHelper       *helper,
		    MetaMapperInputInfo *input,
                    MetaInputMapper     *mapper)
{
  DeviceCandidates info = { 0, };
  guint i, pos = 0;

  info.input = input;
  info.matches = g_array_new (FALSE, TRUE, sizeof (DeviceMatch));

  guess_candidates (mapper, input, &info);

  for (i = 0; i < helper->device_maps->len; i++)
    {
      DeviceCandidates *elem;

      elem = &g_array_index (helper->device_maps, DeviceCandidates, i);

      if (elem->best > info.best)
        pos = i;
    }

  if (pos >= helper->device_maps->len)
    g_array_append_val (helper->device_maps, info);
  else
    g_array_insert_val (helper->device_maps, pos, info);
}

static void
mapping_helper_apply (MappingHelper   *helper,
                      MetaInputMapper *mapper)
{
  guint i, j;

  /* Now, decide which input claims which output */
  for (i = 0; i < helper->device_maps->len; i++)
    {
      DeviceCandidates *info;

      info = &g_array_index (helper->device_maps, DeviceCandidates, i);
      g_debug ("Applying mapping %d to input device '%s', type %d", i,
               clutter_input_device_get_device_name (info->input->device),
               clutter_input_device_get_device_type (info->input->device));

      for (j = 0; j < info->matches->len; j++)
        {
          MetaLogicalMonitor *logical_monitor;
          MetaMapperOutputInfo *output;
          MetaMonitor *monitor;
          DeviceMatch *match;

          match = &g_array_index (info->matches, DeviceMatch, j);
          g_debug ("Output candidate '%s', score %x",
                   meta_monitor_get_display_name (match->monitor),
                   match->score);

          monitor = match->monitor;
          logical_monitor = meta_monitor_get_logical_monitor (monitor);
          output = g_hash_table_lookup (mapper->output_devices,
                                        logical_monitor);

          if (!output)
            continue;

          g_debug ("Matched input '%s' with output '%s'",
                   clutter_input_device_get_device_name (info->input->device),
                   meta_monitor_get_display_name (match->monitor));
          mapper_output_info_add_input (output, info->input, monitor);
          break;
        }
    }
}

static void
mapper_recalculate_candidates (MetaInputMapper *mapper)
{
  MetaMapperInputInfo *input;
  MappingHelper helper;
  GHashTableIter iter;

  mapping_helper_init (&helper);
  g_hash_table_iter_init (&iter, mapper->input_devices);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &input))
    mapping_helper_add (&helper, input, mapper);

  mapping_helper_apply (&helper, mapper);
  mapping_helper_release (&helper);
}

static void
mapper_recalculate_input (MetaInputMapper     *mapper,
                          MetaMapperInputInfo *input)
{
  MappingHelper helper;

  mapping_helper_init (&helper);
  mapping_helper_add (&helper, input, mapper);
  mapping_helper_apply (&helper, mapper);
  mapping_helper_release (&helper);
}

static void
mapper_update_outputs (MetaInputMapper *mapper)
{
  MetaMapperOutputInfo *output;
  GList *logical_monitors, *l;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, mapper->output_devices);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &output))
    {
      mapper_output_info_clear_inputs (output);
      g_hash_table_iter_remove (&iter);
    }

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (mapper->monitor_manager);

  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaMapperOutputInfo *info;

      info = mapper_output_info_new (logical_monitor);
      g_hash_table_insert (mapper->output_devices, logical_monitor, info);
    }

  mapper_recalculate_candidates (mapper);
}

static void
input_mapper_monitors_changed_cb (MetaMonitorManager *monitor_manager,
                                  MetaInputMapper    *mapper)
{
  mapper_update_outputs (mapper);
}

static void
input_mapper_power_save_mode_changed_cb (MetaMonitorManager        *monitor_manager,
                                         MetaPowerSaveChangeReason  reason,
                                         MetaInputMapper           *mapper)
{
  ClutterInputDevice *device;
  MetaLogicalMonitor *logical_monitor;
  MetaMonitor *builtin;
  MetaPowerSave power_save_mode;
  gboolean on;

  power_save_mode =
    meta_monitor_manager_get_power_save_mode (mapper->monitor_manager);
  on = power_save_mode == META_POWER_SAVE_ON;

  builtin = meta_monitor_manager_get_laptop_panel (monitor_manager);
  if (!builtin)
    return;

  logical_monitor = meta_monitor_get_logical_monitor (builtin);
  if (!logical_monitor)
    return;

  device =
    meta_input_mapper_get_logical_monitor_device (mapper,
                                                  logical_monitor,
                                                  CLUTTER_TOUCHSCREEN_DEVICE);
  if (!device)
    return;

  g_signal_emit (mapper, signals[DEVICE_ENABLED], 0, device, on);
}

static void
input_mapper_device_removed_cb (ClutterSeat        *seat,
                                ClutterInputDevice *device,
                                MetaInputMapper    *mapper)
{
  meta_input_mapper_remove_device (mapper, device);
}

static void
meta_input_mapper_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      mapper->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_mapper_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, mapper->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_mapper_finalize (GObject *object)
{
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);

  g_clear_handle_id (&mapper->dbus_name_id, g_bus_unown_name);

  g_signal_handlers_disconnect_by_func (mapper->monitor_manager,
                                        input_mapper_monitors_changed_cb,
                                        mapper);
  g_signal_handlers_disconnect_by_func (mapper->seat,
                                        input_mapper_device_removed_cb,
                                        mapper);

  g_hash_table_unref (mapper->input_devices);
  g_hash_table_unref (mapper->output_devices);

  G_OBJECT_CLASS (meta_input_mapper_parent_class)->finalize (object);
}

static void
meta_input_mapper_constructed (GObject *object)
{
  MetaInputMapper *mapper = META_INPUT_MAPPER (object);

  G_OBJECT_CLASS (meta_input_mapper_parent_class)->constructed (object);

  mapper->seat = clutter_backend_get_default_seat (clutter_get_default_backend ());
  g_signal_connect (mapper->seat, "device-removed",
                    G_CALLBACK (input_mapper_device_removed_cb), mapper);

  mapper->monitor_manager = meta_backend_get_monitor_manager (mapper->backend);
  g_signal_connect (mapper->monitor_manager, "monitors-changed-internal",
                    G_CALLBACK (input_mapper_monitors_changed_cb), mapper);
  g_signal_connect (mapper->monitor_manager, "power-save-mode-changed",
                    G_CALLBACK (input_mapper_power_save_mode_changed_cb),
                    mapper);

  mapper_update_outputs (mapper);
}

static void
meta_input_mapper_class_init (MetaInputMapperClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_input_mapper_constructed;
  object_class->finalize = meta_input_mapper_finalize;
  object_class->set_property = meta_input_mapper_set_property;
  object_class->get_property = meta_input_mapper_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[DEVICE_MAPPED] =
    g_signal_new ("device-mapped",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  G_TYPE_POINTER);
  signals[DEVICE_ENABLED] =
    g_signal_new ("device-enabled",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  G_TYPE_BOOLEAN);
  signals[DEVICE_ASPECT_RATIO] =
    g_signal_new ("device-aspect-ratio",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  CLUTTER_TYPE_INPUT_DEVICE,
                  G_TYPE_DOUBLE);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaRemoteDesktop *remote_desktop = user_data;
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (remote_desktop);
  g_autoptr (GError) error = NULL;

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_INPUT_MAPPING_DBUS_PATH,
                                         &error))
    g_warning ("Failed to export input mapping object: %s", error->message);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_info ("Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_info ("Lost or failed to acquire name %s", name);
}

static void
meta_input_mapper_init (MetaInputMapper *mapper)
{
  mapper->input_devices =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) mapper_input_info_free);
  mapper->output_devices =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) mapper_output_info_free);

  mapper->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_INPUT_MAPPING_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    mapper,
                    NULL);
}

static gboolean
handle_get_device_mapping (MetaDBusInputMapping  *skeleton,
                           GDBusMethodInvocation *invocation,
                           const char            *device_node)
{
  MetaInputMapper *input_mapper = META_INPUT_MAPPER (skeleton);
  ClutterInputDevice *device = NULL;
  g_autoptr (GList) devices = NULL;
  MetaLogicalMonitor *logical_monitor;
  GList *l;

  devices = clutter_seat_list_devices (input_mapper->seat);

  for (l = devices; l; l = l->next)
    {
      if (g_strcmp0 (clutter_input_device_get_device_node (l->data),
                     device_node) == 0)
        {
          device = l->data;
          break;
        }
    }

  if (!device)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_IO_ERROR,
                                             G_IO_ERROR_INVALID_DATA,
                                             "Device does not exist");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  logical_monitor =
    meta_input_mapper_get_device_logical_monitor (input_mapper, device);

  if (logical_monitor)
    {
      MtkRectangle rect;

      rect = meta_logical_monitor_get_layout (logical_monitor);
      g_dbus_method_invocation_return_value (invocation,
                                             g_variant_new ("((iiii))",
                                                            rect.x,
                                                            rect.y,
                                                            rect.width,
                                                            rect.height));
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_IO_ERROR,
                                             G_IO_ERROR_NOT_FOUND,
                                             "Device is not mapped to any output");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
}

static void
meta_input_mapping_init_iface (MetaDBusInputMappingIface *iface)
{
  iface->handle_get_device_mapping = handle_get_device_mapping;
}


MetaInputMapper *
meta_input_mapper_new (MetaBackend *backend)
{
  return g_object_new (META_TYPE_INPUT_MAPPER,
                       "backend", backend,
                       NULL);
}

void
meta_input_mapper_add_device (MetaInputMapper    *mapper,
                              ClutterInputDevice *device)
{
  MetaMapperInputInfo *info;

  g_return_if_fail (mapper != NULL);
  g_return_if_fail (device != NULL);

  if (g_hash_table_contains (mapper->input_devices, device))
    return;

  info = mapper_input_info_new (device, mapper);
  g_hash_table_insert (mapper->input_devices, device, info);
  mapper_recalculate_input (mapper, info);
}

void
meta_input_mapper_remove_device (MetaInputMapper    *mapper,
                                 ClutterInputDevice *device)
{
  MetaMapperInputInfo *input;

  g_return_if_fail (mapper != NULL);
  g_return_if_fail (device != NULL);

  input = g_hash_table_lookup (mapper->input_devices, device);

  if (input)
    {
      if (input->output)
        mapper_output_info_remove_input (input->output, input);
      g_hash_table_remove (mapper->input_devices, device);
    }
}

ClutterInputDevice *
meta_input_mapper_get_logical_monitor_device (MetaInputMapper        *mapper,
                                              MetaLogicalMonitor     *logical_monitor,
                                              ClutterInputDeviceType  device_type)
{
  MetaMapperOutputInfo *output;
  GList *l;

  output = g_hash_table_lookup (mapper->output_devices, logical_monitor);
  if (!output)
    return NULL;

  for (l = output->input_devices; l; l = l->next)
    {
      MetaMapperInputInfo *input = l->data;

      if (clutter_input_device_get_device_type (input->device) == device_type)
        return input->device;
    }

  return NULL;
}

static ClutterInputDevice *
find_grouped_pen (ClutterInputDevice *device)
{
  GList *l, *devices;
  ClutterInputDeviceType device_type;
  ClutterInputDevice *pen = NULL;
  ClutterSeat *seat;

  device_type = clutter_input_device_get_device_type (device);

  if (device_type == CLUTTER_TABLET_DEVICE ||
      device_type == CLUTTER_PEN_DEVICE)
    return device;

  seat = clutter_input_device_get_seat (device);
  devices = clutter_seat_list_devices (seat);

  for (l = devices; l; l = l->next)
    {
      ClutterInputDevice *other_device = l->data;

      device_type = clutter_input_device_get_device_type (other_device);

      if ((device_type == CLUTTER_TABLET_DEVICE ||
           device_type == CLUTTER_PEN_DEVICE) &&
          clutter_input_device_is_grouped (device, other_device))
        {
          pen = other_device;
          break;
        }
    }

  g_list_free (devices);

  return pen;
}

MetaLogicalMonitor *
meta_input_mapper_get_device_logical_monitor (MetaInputMapper    *mapper,
                                              ClutterInputDevice *device)
{
  MetaMapperOutputInfo *output;
  MetaLogicalMonitor *logical_monitor;
  GHashTableIter iter;
  GList *l;

  if (clutter_input_device_get_device_type (device) == CLUTTER_PAD_DEVICE)
    {
      device = find_grouped_pen (device);
      if (!device)
        return NULL;
    }

  g_hash_table_iter_init (&iter, mapper->output_devices);

  while (g_hash_table_iter_next (&iter, (gpointer *) &logical_monitor,
                                 (gpointer *) &output))
    {
      for (l = output->input_devices; l; l = l->next)
        {
          MetaMapperInputInfo *input = l->data;

          if (input->device == device)
            return logical_monitor;
        }
    }

  return NULL;
}

GSettings *
meta_input_mapper_get_tablet_settings (MetaInputMapper    *mapper,
                                       ClutterInputDevice *device)
{
  MetaMapperInputInfo *input;

  g_return_val_if_fail (META_IS_INPUT_MAPPER (mapper), NULL);
  g_return_val_if_fail (CLUTTER_IS_INPUT_DEVICE (device), NULL);

  input = g_hash_table_lookup (mapper->input_devices, device);
  if (!input)
    return NULL;

  return input->settings;
}
