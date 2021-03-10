/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2021 Canonical, Ltd.
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
 * Author: Marco Trevisan <marco.trevisan@canonical.com>
 */

#pragma once

#include "backends/meta-input-settings-private.h"

#define META_TYPE_INPUT_SETTINGS_DUMMY (meta_input_settings_dummy_get_type ())

G_DECLARE_DERIVABLE_TYPE (MetaInputSettingsDummy, meta_input_settings_dummy,
                          META, INPUT_SETTINGS_DUMMY, MetaInputSettings);

struct _MetaInputSettingsDummyClass
{
  MetaInputSettingsClass parent_class;
};
