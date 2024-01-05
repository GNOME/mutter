/*
 * Copyright (C) 2018 Red Hat Inc.
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

#include "meta/common.h"

#define META_TYPE_REMOTE_ACCESS_HANDLE meta_remote_access_handle_get_type ()

META_EXPORT
G_DECLARE_DERIVABLE_TYPE (MetaRemoteAccessHandle,
                          meta_remote_access_handle,
                          META, REMOTE_ACCESS_HANDLE,
                          GObject)

struct _MetaRemoteAccessHandleClass
{
  GObjectClass parent_class;

  void (*stop) (MetaRemoteAccessHandle *handle);
};

META_EXPORT
void meta_remote_access_handle_stop (MetaRemoteAccessHandle *handle);

META_EXPORT
gboolean meta_remote_access_handle_get_disable_animations (MetaRemoteAccessHandle *handle);

#define META_TYPE_REMOTE_ACCESS_CONTROLLER meta_remote_access_controller_get_type ()

META_EXPORT
G_DECLARE_FINAL_TYPE (MetaRemoteAccessController,
                      meta_remote_access_controller,
                      META, REMOTE_ACCESS_CONTROLLER,
                      GObject)

META_EXPORT
void meta_remote_access_controller_inhibit_remote_access (MetaRemoteAccessController *controller);

META_EXPORT
void meta_remote_access_controller_uninhibit_remote_access (MetaRemoteAccessController *controller);
