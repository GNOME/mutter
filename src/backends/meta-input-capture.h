/*
 * Copyright (C) 2022 Red Hat Inc.
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
 */

#pragma once

#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-viewport-info.h"
#include "clutter/clutter.h"

#include "meta-dbus-input-capture.h"

typedef void (* MetaInputCaptureEnable) (MetaInputCapture *input_capture,
                                         gpointer          user_data);
typedef void (* MetaInputCaptureDisable) (MetaInputCapture *input_capture,
                                          gpointer          user_data);

#define META_TYPE_INPUT_CAPTURE (meta_input_capture_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputCapture, meta_input_capture,
                      META, INPUT_CAPTURE,
                      MetaDbusSessionManager)

MetaInputCapture *meta_input_capture_new (MetaBackend *backend);

void meta_input_capture_set_event_router (MetaInputCapture        *input_capture,
                                          MetaInputCaptureEnable   enable,
                                          MetaInputCaptureDisable  disable,
                                          gpointer                 user_data);

void meta_input_capture_notify_cancelled (MetaInputCapture *input_capture);

gboolean meta_input_capture_process_event (MetaInputCapture   *input_capture,
                                           const ClutterEvent *event);
