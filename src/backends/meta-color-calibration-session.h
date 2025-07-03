/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include "meta-dbus-color-manager.h"

#define META_TYPE_COLOR_CALIBRATION_SESSION (meta_color_calibration_session_get_type ())
G_DECLARE_FINAL_TYPE (MetaColorCalibrationSession, meta_color_calibration_session,
                      META, COLOR_CALIBRATION_SESSION,
                      MetaDBusColorManagerCalibrationSkeleton)

const char * meta_color_calibration_session_get_object_path (MetaColorCalibrationSession *session);
