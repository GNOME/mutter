/*
 * Copyright (C) 2021 Red Hat, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_DEVICE_POOL_PRIVATE_H
#define META_DEVICE_POOL_PRIVATE_H

#include <glib-object.h>

#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-launcher.h"

#define META_TYPE_DEVICE_POOL (meta_device_pool_get_type ())
G_DECLARE_FINAL_TYPE (MetaDevicePool, meta_device_pool,
                      META, DEVICE_POOL,
                      GObject)

MetaDevicePool * meta_device_pool_new (MetaLauncher *launcher);

#endif /* META_DEVICE_POOL_PRIVATE_H */
