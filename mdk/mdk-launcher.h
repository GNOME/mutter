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

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include "mdk-types.h"

enum _MdkLauncherType
{
  MDK_LAUNCHER_TYPE_DESKTOP,
  MDK_LAUNCHER_TYPE_EXEC,
};

typedef struct _MdkLauncher MdkLauncher;

MdkLauncher * mdk_launcher_new_desktop (MdkContext      *context,
                                        int              id,
                                        GDesktopAppInfo *app_info,
                                        const char      *action);

MdkLauncher * mdk_launcher_new_exec (MdkContext *context,
                                     int         id,
                                     const char *value,
                                     GStrv       argv);

void mdk_launcher_free (MdkLauncher *launcher);

MdkContext * mdk_launcher_get_context (MdkLauncher *launcher);

const char * mdk_launcher_get_name (MdkLauncher *launcher);

char * mdk_launcher_get_action (MdkLauncher *launcher);

void mdk_launcher_activate (MdkLauncher *launcher);
