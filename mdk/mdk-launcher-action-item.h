/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

#include "mdk-types.h"

#define MDK_TYPE_LAUNCHER_ACTION_ITEM (mdk_launcher_action_item_get_type ())
G_DECLARE_FINAL_TYPE (MdkLauncherActionItem, mdk_launcher_action_item,
                      MDK, LAUNCHER_ACTION_ITEM, GObject)

MdkLauncherActionItem * mdk_launcher_action_item_new (const char        *name,
                                                      MdkLauncherAction *action);

const char * mdk_launcher_action_item_get_name (MdkLauncherActionItem *item);

MdkLauncherAction * mdk_launcher_action_item_get_action (MdkLauncherActionItem *item);
