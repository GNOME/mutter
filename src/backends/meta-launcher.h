/*
 * Copyright (C) 2013 Red Hat, Inc.
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

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

#define META_TYPE_LAUNCHER (meta_launcher_get_type ())
G_DECLARE_FINAL_TYPE (MetaLauncher,
                      meta_launcher,
                      META, LAUNCHER,
                      GObject)

typedef struct _MetaDBusLogin1Session MetaDBusLogin1Session;

MetaLauncher *meta_launcher_new (MetaBackend  *backend,
                                 GError      **error);

gboolean meta_launcher_activate_vt (MetaLauncher  *self,
                                    signed char    vt,
                                    GError       **error);

gboolean meta_launcher_is_session_active (MetaLauncher *launcher);

gboolean meta_launcher_take_control (MetaLauncher  *launcher,
                                     GError       **error);

const char * meta_launcher_get_seat_id (MetaLauncher *launcher);

META_EXPORT_TEST
MetaDBusLogin1Session * meta_launcher_get_session_proxy (MetaLauncher *launcher);

MetaBackend * meta_launcher_get_backend (MetaLauncher *launcher);
