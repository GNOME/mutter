/*
 * Copyright (C) 2021 Red Hat Inc.
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

#ifndef META_COLOR_STORE_H
#define META_COLOR_STORE_H

#include <colord.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "backends/meta-backend-types.h"

#define META_TYPE_COLOR_STORE (meta_color_store_get_type ())
G_DECLARE_FINAL_TYPE (MetaColorStore, meta_color_store,
                      META, COLOR_STORE,
                      GObject)

MetaColorStore * meta_color_store_new (MetaColorManager *color_manager);

gboolean meta_color_store_ensure_device_profile (MetaColorStore      *color_store,
                                                 MetaColorDevice     *color_device,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data);

MetaColorProfile * meta_color_store_ensure_device_profile_finish (MetaColorStore  *color_store,
                                                                  GAsyncResult    *res,
                                                                  GError         **error);

void meta_color_store_ensure_colord_profile (MetaColorStore      *color_store,
                                             CdProfile           *cd_profile,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data);

MetaColorProfile * meta_color_store_ensure_colord_profile_finish (MetaColorStore  *color_store,
                                                                  GAsyncResult    *res,
                                                                  GError         **error);

#endif /* META_COLOR_STORE_H */
