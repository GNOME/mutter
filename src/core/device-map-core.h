/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file device-map-core.h  device map for core devices
 *
 * Input devices.
 * This file contains the core protocol implementation of the device map
 */

/*
 * Copyright (C) 2011 Carlos Garnacho
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

#ifndef META_DEVICE_MAP_CORE_H
#define META_DEVICE_MAP_CORE_H

typedef struct _MetaDeviceMapCore MetaDeviceMapCore;
typedef struct _MetaDeviceMapCoreClass MetaDeviceMapCoreClass;

#include "device-map-private.h"

#define META_TYPE_DEVICE_MAP_CORE            (meta_device_map_core_get_type ())
#define META_DEVICE_MAP_CORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_MAP_CORE, MetaDeviceMapCore))
#define META_DEVICE_MAP_CORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_MAP_CORE, MetaDeviceMapCoreClass))
#define META_IS_DEVICE_MAP_CORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_MAP_CORE))
#define META_IS_DEVICE_MAP_CORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_MAP_CORE))
#define META_DEVICE_MAP_CORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_MAP_CORE, MetaDeviceMapCoreClass))

struct _MetaDeviceMapCore
{
  MetaDeviceMap parent_instance;
};

struct _MetaDeviceMapCoreClass
{
  MetaDeviceMapClass parent_class;
};

GType           meta_device_map_core_get_type (void) G_GNUC_CONST;

#endif /* META_DEVICE_MAP_CORE_H */
