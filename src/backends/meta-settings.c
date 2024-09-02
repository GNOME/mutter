/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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
 */

#include "config.h"

#include "backends/meta-settings-private.h"

#include <gio/gio.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"

#ifndef XWAYLAND_GRAB_DEFAULT_ACCESS_RULES
# warning "XWAYLAND_GRAB_DEFAULT_ACCESS_RULES is not set"
# define  XWAYLAND_GRAB_DEFAULT_ACCESS_RULES ""
#endif

enum
{
  UI_SCALING_FACTOR_CHANGED,
  GLOBAL_SCALING_FACTOR_CHANGED,
  FONT_DPI_CHANGED,
  EXPERIMENTAL_FEATURES_CHANGED,
  PRIVACY_SCREEN_CHANGED,

  N_SIGNALS
};

static GDebugKey experimental_feature_keys[] = {
  { "scale-monitor-framebuffer", META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER },
  { "kms-modifiers", META_EXPERIMENTAL_FEATURE_KMS_MODIFIERS },
  { "autoclose-xwayland", META_EXPERIMENTAL_FEATURE_AUTOCLOSE_XWAYLAND },
  { "variable-refresh-rate", META_EXPERIMENTAL_FEATURE_VARIABLE_REFRESH_RATE },
  { "xwayland-native-scaling", META_EXPERIMENTAL_FEATURE_XWAYLAND_NATIVE_SCALING },
};

static guint signals[N_SIGNALS];

struct _MetaSettings
{
  GObject parent;

  MetaBackend *backend;

  GSettings *interface_settings;
  GSettings *mutter_settings;
  GSettings *privacy_settings;
  GSettings *wayland_settings;

  int ui_scaling_factor;
  int global_scaling_factor;

  gboolean privacy_screen;

  MetaExperimentalFeature experimental_features;
  gboolean experimental_features_overridden;

  gboolean xwayland_allow_grabs;
  GPtrArray *xwayland_grab_allow_list_patterns;
  GPtrArray *xwayland_grab_deny_list_patterns;

  /* A bitmask of MetaXwaylandExtension enum */
  int xwayland_disable_extensions;

  /* Whether Xwayland should allow X11 clients from different endianness */
  gboolean xwayland_allow_byte_swapped_clients;
};

G_DEFINE_TYPE (MetaSettings, meta_settings, G_TYPE_OBJECT)

static int
calculate_ui_scaling_factor (MetaSettings *settings)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (settings->backend);
  MetaLogicalMonitor *primary_logical_monitor;

  primary_logical_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
  if (!primary_logical_monitor)
    return 1;

  return (int) meta_logical_monitor_get_scale (primary_logical_monitor);
}

static gboolean
update_ui_scaling_factor (MetaSettings *settings)
{
  int ui_scaling_factor;

  if (meta_backend_is_stage_views_scaled (settings->backend))
    ui_scaling_factor = 1;
  else
    ui_scaling_factor = calculate_ui_scaling_factor (settings);

  if (settings->ui_scaling_factor != ui_scaling_factor)
    {
      settings->ui_scaling_factor = ui_scaling_factor;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

void
meta_settings_update_ui_scaling_factor (MetaSettings *settings)
{
  if (update_ui_scaling_factor (settings))
    g_signal_emit (settings, signals[UI_SCALING_FACTOR_CHANGED], 0);
}

int
meta_settings_get_ui_scaling_factor (MetaSettings *settings)
{
  g_assert (settings->ui_scaling_factor != 0);

  return settings->ui_scaling_factor;
}

static gboolean
update_global_scaling_factor (MetaSettings *settings)
{
  int global_scaling_factor;

  global_scaling_factor =
    (int) g_settings_get_uint (settings->interface_settings,
                               "scaling-factor");

  if (settings->global_scaling_factor != global_scaling_factor)
    {
      settings->global_scaling_factor = global_scaling_factor;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
meta_settings_get_global_scaling_factor (MetaSettings *settings,
                                         int          *out_scaling_factor)
{
  if (settings->global_scaling_factor == 0)
    return FALSE;

  *out_scaling_factor = settings->global_scaling_factor;
  return TRUE;
}

static void
interface_settings_changed (GSettings    *interface_settings,
                            const char   *key,
                            MetaSettings *settings)
{
  if (g_str_equal (key, "scaling-factor"))
    {
      if (update_global_scaling_factor (settings))
        g_signal_emit (settings, signals[GLOBAL_SCALING_FACTOR_CHANGED], 0);
    }
}

static void
privacy_settings_changed (GSettings    *privacy_settings,
                          const char   *key,
                          MetaSettings *settings)
{
  if (g_str_equal (key, "privacy-screen"))
    {
      gboolean privacy_screen;

      privacy_screen = g_settings_get_boolean (privacy_settings, key);

      if (settings->privacy_screen != privacy_screen)
        {
          settings->privacy_screen = privacy_screen;
          g_signal_emit (settings, signals[PRIVACY_SCREEN_CHANGED], 0);
        }
    }
}

gboolean
meta_settings_is_experimental_feature_enabled (MetaSettings           *settings,
                                               MetaExperimentalFeature feature)
{
  return !!(settings->experimental_features & feature);
}

void
meta_settings_override_experimental_features (MetaSettings *settings)
{
  settings->experimental_features = META_EXPERIMENTAL_FEATURE_NONE;
  settings->experimental_features_overridden = TRUE;
}

void
meta_settings_enable_experimental_feature (MetaSettings           *settings,
                                           MetaExperimentalFeature feature)
{
  g_assert (settings->experimental_features_overridden);

  settings->experimental_features |= feature;
}

static gboolean
experimental_features_handler (GVariant *features_variant,
                               gpointer *result,
                               gpointer  data)
{
  MetaSettings *settings = data;
  GVariantIter features_iter;
  char *feature_str;
  MetaExperimentalFeature features = META_EXPERIMENTAL_FEATURE_NONE;

  if (settings->experimental_features_overridden)
    {
      *result = GINT_TO_POINTER (FALSE);
      return TRUE;
    }

  g_variant_iter_init (&features_iter, features_variant);
  while (g_variant_iter_loop (&features_iter, "s", &feature_str))
    {
      MetaExperimentalFeature feature = META_EXPERIMENTAL_FEATURE_NONE;

      if (g_str_equal (feature_str, "scale-monitor-framebuffer"))
        feature = META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER;
      else if (g_str_equal (feature_str, "kms-modifiers"))
        feature = META_EXPERIMENTAL_FEATURE_KMS_MODIFIERS;
      else if (g_str_equal (feature_str, "autoclose-xwayland"))
        feature = META_EXPERIMENTAL_FEATURE_AUTOCLOSE_XWAYLAND;
      else if (g_str_equal (feature_str, "variable-refresh-rate"))
        feature = META_EXPERIMENTAL_FEATURE_VARIABLE_REFRESH_RATE;
      else if (g_str_equal (feature_str, "xwayland-native-scaling"))
        feature = META_EXPERIMENTAL_FEATURE_XWAYLAND_NATIVE_SCALING;

      if (feature)
        g_message ("Enabling experimental feature '%s'", feature_str);
      else
        g_warning ("Unknown experimental feature '%s'", feature_str);

      features |= feature;
    }

  if (features != settings->experimental_features)
    {
      settings->experimental_features = features;
      *result = GINT_TO_POINTER (TRUE);
    }
  else
    {
      *result = GINT_TO_POINTER (FALSE);
    }

  return TRUE;
}

static gboolean
update_experimental_features (MetaSettings *settings)
{
  return GPOINTER_TO_INT (g_settings_get_mapped (settings->mutter_settings,
                                                 "experimental-features",
                                                 experimental_features_handler,
                                                 settings));
}

static void
mutter_settings_changed (GSettings    *mutter_settings,
                         gchar        *key,
                         MetaSettings *settings)
{
  MetaExperimentalFeature old_experimental_features;

  if (!g_str_equal (key, "experimental-features"))
    return;

  old_experimental_features = settings->experimental_features;
  if (update_experimental_features (settings))
    g_signal_emit (settings, signals[EXPERIMENTAL_FEATURES_CHANGED], 0,
                   (unsigned int) old_experimental_features);
}

static void
xwayland_grab_list_add_item (MetaSettings *settings,
                             char         *item)
{
  /* If first character is '!', it's a denied value */
  if (item[0] != '!')
    g_ptr_array_add (settings->xwayland_grab_allow_list_patterns,
                     g_pattern_spec_new (item));
  else if (item[1] != 0)
    g_ptr_array_add (settings->xwayland_grab_deny_list_patterns,
                     g_pattern_spec_new (&item[1]));
}

static gboolean
xwayland_grab_access_rules_handler (GVariant *variant,
                                    gpointer *result,
                                    gpointer  data)
{
  MetaSettings *settings = data;
  GVariantIter iter;
  char *item;

  /* Create a GPatternSpec for each element */
  g_variant_iter_init (&iter, variant);
  while (g_variant_iter_loop (&iter, "s", &item))
    xwayland_grab_list_add_item (settings, item);

  *result = GINT_TO_POINTER (TRUE);

  return TRUE;
}

static void
update_xwayland_grab_access_rules (MetaSettings *settings)
{
  gchar **system_defaults;
  int i;

  /* Free previous patterns and create new arrays */
  g_clear_pointer (&settings->xwayland_grab_allow_list_patterns,
                   g_ptr_array_unref);
  settings->xwayland_grab_allow_list_patterns =
    g_ptr_array_new_with_free_func ((GDestroyNotify) g_pattern_spec_free);

  g_clear_pointer (&settings->xwayland_grab_deny_list_patterns,
                   g_ptr_array_unref);
  settings->xwayland_grab_deny_list_patterns =
    g_ptr_array_new_with_free_func ((GDestroyNotify) g_pattern_spec_free);

  /* Add system defaults values */
  system_defaults = g_strsplit (XWAYLAND_GRAB_DEFAULT_ACCESS_RULES, ",", -1);
  for (i = 0; system_defaults[i]; i++)
    xwayland_grab_list_add_item (settings, system_defaults[i]);
  g_strfreev (system_defaults);

  /* Then add gsettings values */
  g_settings_get_mapped (settings->wayland_settings,
                         "xwayland-grab-access-rules",
                         xwayland_grab_access_rules_handler,
                         settings);
}

static void
update_xwayland_allow_grabs (MetaSettings *settings)
{
  settings->xwayland_allow_grabs =
    g_settings_get_boolean (settings->wayland_settings,
                            "xwayland-allow-grabs");
}

static void
update_xwayland_disable_extensions (MetaSettings *settings)
{
  settings->xwayland_disable_extensions =
    g_settings_get_flags (settings->wayland_settings,
                          "xwayland-disable-extension");
}

static void
update_privacy_settings (MetaSettings *settings)
{
  privacy_settings_changed (settings->privacy_settings,
                            "privacy-screen",
                            settings);
}

static void
update_xwayland_allow_byte_swapped_clients (MetaSettings *settings)
{

  settings->xwayland_allow_byte_swapped_clients =
    g_settings_get_boolean (settings->wayland_settings,
                            "xwayland-allow-byte-swapped-clients");
}

static void
wayland_settings_changed (GSettings    *wayland_settings,
                          gchar        *key,
                          MetaSettings *settings)
{

  if (g_str_equal (key, "xwayland-allow-grabs"))
    {
      update_xwayland_allow_grabs (settings);
    }
  else if (g_str_equal (key, "xwayland-grab-access-rules"))
    {
      update_xwayland_grab_access_rules (settings);
    }
  else if (g_str_equal (key, "xwayland-disable-extension"))
    {
      update_xwayland_disable_extensions (settings);
    }
  else if (g_str_equal (key, "xwayland-allow-byte-swapped-clients"))
    {
      update_xwayland_allow_byte_swapped_clients (settings);
    }
}

void
meta_settings_get_xwayland_grab_patterns (MetaSettings  *settings,
                                          GPtrArray    **allow_list_patterns,
                                          GPtrArray    **deny_list_patterns)
{
  *allow_list_patterns = settings->xwayland_grab_allow_list_patterns;
  *deny_list_patterns = settings->xwayland_grab_deny_list_patterns;
}

gboolean
meta_settings_are_xwayland_grabs_allowed (MetaSettings *settings)
{
  return (settings->xwayland_allow_grabs);
}

int
meta_settings_get_xwayland_disable_extensions (MetaSettings *settings)
{
  return (settings->xwayland_disable_extensions);
}

gboolean
meta_settings_are_xwayland_byte_swapped_clients_allowed (MetaSettings *settings)
{
  return settings->xwayland_allow_byte_swapped_clients;
}

gboolean
meta_settings_is_privacy_screen_enabled (MetaSettings *settings)
{
  return settings->privacy_screen;
}

void
meta_settings_set_privacy_screen_enabled (MetaSettings *settings,
                                          gboolean      enabled)
{
  if (settings->privacy_screen == enabled)
    return;

  settings->privacy_screen = enabled;
  g_settings_set_boolean (settings->privacy_settings, "privacy-screen",
                          enabled);
}

MetaSettings *
meta_settings_new (MetaBackend *backend)
{
  MetaSettings *settings;

  settings = g_object_new (META_TYPE_SETTINGS, NULL);
  settings->backend = backend;

  return settings;
}

static void
meta_settings_dispose (GObject *object)
{
  MetaSettings *settings = META_SETTINGS (object);

  g_clear_object (&settings->mutter_settings);
  g_clear_object (&settings->interface_settings);
  g_clear_object (&settings->privacy_settings);
  g_clear_object (&settings->wayland_settings);
  g_clear_pointer (&settings->xwayland_grab_allow_list_patterns,
                   g_ptr_array_unref);
  g_clear_pointer (&settings->xwayland_grab_deny_list_patterns,
                   g_ptr_array_unref);

  G_OBJECT_CLASS (meta_settings_parent_class)->dispose (object);
}

static void
meta_settings_init (MetaSettings *settings)
{
  const char *experimental_features_env;

  settings->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect (settings->interface_settings, "changed",
                    G_CALLBACK (interface_settings_changed),
                    settings);
  settings->privacy_settings = g_settings_new ("org.gnome.desktop.privacy");
  g_signal_connect (settings->privacy_settings, "changed",
                    G_CALLBACK (privacy_settings_changed),
                    settings);
  settings->mutter_settings = g_settings_new ("org.gnome.mutter");
  g_signal_connect (settings->mutter_settings, "changed",
                    G_CALLBACK (mutter_settings_changed),
                    settings);
  settings->wayland_settings = g_settings_new ("org.gnome.mutter.wayland");
  g_signal_connect (settings->wayland_settings, "changed",
                    G_CALLBACK (wayland_settings_changed),
                    settings);

  /* Chain up inter-dependent settings. */
  g_signal_connect (settings, "global-scaling-factor-changed",
                    G_CALLBACK (meta_settings_update_ui_scaling_factor), NULL);

  experimental_features_env = getenv ("MUTTER_DEBUG_EXPERIMENTAL_FEATURES");
  if (experimental_features_env)
    {
      MetaExperimentalFeature experimental_features;

      experimental_features =
        g_parse_debug_string (experimental_features_env,
                              experimental_feature_keys,
                              G_N_ELEMENTS (experimental_feature_keys));

      meta_settings_override_experimental_features (settings);
      meta_settings_enable_experimental_feature (settings,
                                                 experimental_features);
    }

  update_global_scaling_factor (settings);
  update_experimental_features (settings);
  update_xwayland_grab_access_rules (settings);
  update_xwayland_allow_grabs (settings);
  update_xwayland_disable_extensions (settings);
  update_privacy_settings (settings);
  update_xwayland_allow_byte_swapped_clients (settings);
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaSettings       *settings)
{
  meta_settings_update_ui_scaling_factor (settings);
}

void
meta_settings_post_init (MetaSettings *settings)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (settings->backend);

  update_ui_scaling_factor (settings);

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           settings, G_CONNECT_AFTER);
}

static void
meta_settings_class_init (MetaSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_settings_dispose;

  signals[UI_SCALING_FACTOR_CHANGED] =
    g_signal_new ("ui-scaling-factor-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[GLOBAL_SCALING_FACTOR_CHANGED] =
    g_signal_new ("global-scaling-factor-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[FONT_DPI_CHANGED] =
    g_signal_new ("font-dpi-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[EXPERIMENTAL_FEATURES_CHANGED] =
    g_signal_new ("experimental-features-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[PRIVACY_SCREEN_CHANGED] =
    g_signal_new ("privacy-screen-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
