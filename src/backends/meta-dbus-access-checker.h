/*
 * Copyright 2024 GNOME Foundation
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
#include <gio/gio.h>

#include "meta/meta-context.h"

#define META_TYPE_DBUS_ACCESS_CHECKER (meta_dbus_access_checker_get_type())
G_DECLARE_FINAL_TYPE (MetaDbusAccessChecker,
                      meta_dbus_access_checker,
                      META, DBUS_ACCESS_CHECKER,
                      GObject)

MetaDbusAccessChecker * meta_dbus_access_checker_new (GDBusConnection *connection,
                                                      MetaContext     *context);

void meta_dbus_access_checker_allow_sender (MetaDbusAccessChecker *self,
                                            const char            *name);

gboolean meta_dbus_access_checker_is_sender_allowed (MetaDbusAccessChecker *self,
                                                     const char            *sender_name);
