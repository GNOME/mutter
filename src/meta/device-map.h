/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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

#ifndef META_DEVICE_MAP_H
#define META_DEVICE_MAP_H

#include <glib-object.h>
#include <meta/types.h>
#include <meta/device.h>

#define META_TYPE_DEVICE_MAP            (meta_device_map_get_type ())
#define META_DEVICE_MAP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_MAP, MetaDeviceMap))
#define META_DEVICE_MAP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_MAP, MetaDeviceMapClass))
#define META_IS_DEVICE_MAP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_MAP))
#define META_IS_DEVICE_MAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_MAP))
#define META_DEVICE_MAP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_MAP, MetaDeviceMapClass))

typedef struct _MetaDeviceMapClass   MetaDeviceMapClass;

GType           meta_device_map_get_type (void) G_GNUC_CONST;

MetaDevice *    meta_device_map_lookup      (MetaDeviceMap *device_map,
					     gint           device_id);

MetaDisplay *   meta_device_map_get_display (MetaDeviceMap *device_map);

#endif
