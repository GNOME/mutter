/*
 * Copyright (C) 2021 Red Hat Inc.
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
#include <gio/gio.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

#define META_TYPE_COLOR_DEVICE (meta_color_device_get_type ())
G_DECLARE_FINAL_TYPE (MetaColorDevice, meta_color_device,
                      META, COLOR_DEVICE,
                      GObject)

MetaColorDevice * meta_color_device_new (MetaColorManager *color_manager,
                                         MetaMonitor      *monitor);

void meta_color_device_update_monitor (MetaColorDevice *color_device,
                                       MetaMonitor     *monitor);

META_EXPORT_TEST
const char * meta_color_device_get_id (MetaColorDevice *color_device);

META_EXPORT_TEST
MetaMonitor * meta_color_device_get_monitor (MetaColorDevice *color_device);

ClutterColorState * meta_color_device_get_color_state (MetaColorDevice *color_device);

META_EXPORT_TEST
MetaColorProfile * meta_color_device_get_device_profile (MetaColorDevice *color_device);

META_EXPORT_TEST
MetaColorProfile * meta_color_device_get_assigned_profile (MetaColorDevice *color_device);

void meta_color_device_generate_profile (MetaColorDevice     *color_device,
                                         const char          *file_path,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data);

MetaColorProfile * meta_color_device_generate_profile_finish (MetaColorDevice  *color_device,
                                                              GAsyncResult     *res,
                                                              GError          **error);

META_EXPORT_TEST
gboolean meta_color_device_is_ready (MetaColorDevice *color_device);

void meta_color_device_update (MetaColorDevice *color_device);

float meta_color_device_get_reference_luminance_factor (MetaColorDevice *color_device);

void meta_color_device_set_reference_luminance_factor (MetaColorDevice *color_device,
                                                       float            factor);

META_EXPORT_TEST
void meta_set_color_efivar_test_path (const char *path);

gboolean meta_color_device_start_calibration (MetaColorDevice  *color_device,
                                              GError          **error);

void meta_color_device_stop_calibration (MetaColorDevice *color_device);

size_t meta_color_device_get_calibration_lut_size (MetaColorDevice *color_device);

void meta_color_device_set_calibration_lut (MetaColorDevice    *color_device,
                                            const MetaGammaLut *lut);
