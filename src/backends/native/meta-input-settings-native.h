/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2014 Red Hat, Inc.
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

#pragma once

#ifndef META_INPUT_THREAD_H_INSIDE
#error "This header cannot be included directly. Use "backends/native/meta-input-thread.h""
#endif /* META_INPUT_THREAD_H_INSIDE */

#include "backends/meta-input-settings-private.h"

#define META_TYPE_INPUT_SETTINGS_NATIVE             (meta_input_settings_native_get_type ())

typedef struct _MetaInputSettingsNative MetaInputSettingsNative;

G_DECLARE_FINAL_TYPE (MetaInputSettingsNative,
                      meta_input_settings_native,
                      META, INPUT_SETTINGS_NATIVE,
                      MetaInputSettings)

MetaInputSettings * meta_input_settings_native_new_in_impl (MetaSeatImpl *seat_impl);
