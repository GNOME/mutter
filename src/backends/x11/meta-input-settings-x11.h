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

#include "backends/meta-input-settings-private.h"

#define META_TYPE_INPUT_SETTINGS_X11             (meta_input_settings_x11_get_type ())

G_DECLARE_FINAL_TYPE (MetaInputSettingsX11,
                      meta_input_settings_x11,
                      META, INPUT_SETTINGS_X11,
                      MetaInputSettings)

typedef struct _MetaInputSettingsX11 MetaInputSettingsX11;
