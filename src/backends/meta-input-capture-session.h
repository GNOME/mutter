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

#include <glib-object.h>

#include "backends/meta-input-capture.h"
#include "backends/meta-viewport-info.h"
#include "meta/meta-remote-access-controller.h"

#define META_TYPE_INPUT_CAPTURE_SESSION (meta_input_capture_session_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputCaptureSession, meta_input_capture_session,
                      META, INPUT_CAPTURE_SESSION,
                      MetaDBusInputCaptureSessionSkeleton)

#define META_TYPE_INPUT_CAPTURE_SESSION_HANDLE (meta_input_capture_session_handle_get_type ())
G_DECLARE_FINAL_TYPE (MetaInputCaptureSessionHandle,
                      meta_input_capture_session_handle,
                      META, INPUT_CAPTURE_SESSION_HANDLE,
                      MetaRemoteAccessHandle)

char *meta_input_capture_session_get_object_path (MetaInputCaptureSession *session);

gboolean meta_input_capture_session_process_event (MetaInputCaptureSession *session,
                                                   const ClutterEvent      *event);

void meta_input_capture_session_notify_cancelled (MetaInputCaptureSession *session);
