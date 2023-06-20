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

#pragma once

#include <glib-object.h>

#include "meta/meta-settings.h"
#include "meta/types.h"
#include "core/util-private.h"

typedef enum _MetaExperimentalFeature
{
  META_EXPERIMENTAL_FEATURE_NONE = 0,
  META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER = (1 << 0),
  META_EXPERIMENTAL_FEATURE_KMS_MODIFIERS  = (1 << 1),
  META_EXPERIMENTAL_FEATURE_AUTOCLOSE_XWAYLAND  = (1 << 2),
  META_EXPERIMENTAL_FEATURE_VARIABLE_REFRESH_RATE = (1 << 3),
} MetaExperimentalFeature;

typedef enum _MetaXwaylandExtension
{
  META_XWAYLAND_EXTENSION_SECURITY = (1 << 0),
  META_XWAYLAND_EXTENSION_XTEST = (1 << 1),
} MetaXwaylandExtension;

#define META_TYPE_SETTINGS (meta_settings_get_type ())
G_DECLARE_FINAL_TYPE (MetaSettings, meta_settings,
                      META, SETTINGS, GObject)

MetaSettings * meta_settings_new (MetaBackend *backend);

void meta_settings_post_init (MetaSettings *settings);

void meta_settings_update_ui_scaling_factor (MetaSettings *settings);

gboolean meta_settings_get_global_scaling_factor (MetaSettings *settings,
                                                  int          *scaing_factor);

META_EXPORT_TEST
gboolean meta_settings_is_experimental_feature_enabled (MetaSettings           *settings,
                                                        MetaExperimentalFeature feature);

META_EXPORT_TEST
void meta_settings_override_experimental_features (MetaSettings *settings);

META_EXPORT_TEST
void meta_settings_enable_experimental_feature (MetaSettings           *settings,
                                                MetaExperimentalFeature feature);

void meta_settings_get_xwayland_grab_patterns (MetaSettings  *settings,
                                               GPtrArray    **allow_list_patterns,
                                               GPtrArray    **deny_list_patterns);

gboolean meta_settings_are_xwayland_grabs_allowed (MetaSettings *settings);

int meta_settings_get_xwayland_disable_extensions (MetaSettings *settings);

gboolean meta_settings_are_xwayland_byte_swapped_clients_allowed (MetaSettings *settings);

gboolean meta_settings_is_privacy_screen_enabled (MetaSettings *settings);

void meta_settings_set_privacy_screen_enabled (MetaSettings *settings,
                                               gboolean      enabled);
