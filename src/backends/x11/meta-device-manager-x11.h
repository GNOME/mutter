/*
 * Copyright © 2011  Intel Corp.
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
 *
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifndef META_DEVICE_MANAGER_X11_H
#define META_DEVICE_MANAGER_X11_H

#include <clutter/clutter.h>

#ifdef HAVE_LIBWACOM
#include <libwacom/libwacom.h>
#endif

G_BEGIN_DECLS

#define META_TYPE_DEVICE_MANAGER_X11 (meta_device_manager_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaDeviceManagerX11, meta_device_manager_x11,
                      META, DEVICE_MANAGER_X11, ClutterDeviceManager)

struct _MetaDeviceManagerX11
{
  ClutterDeviceManager parent_instance;

  GHashTable *devices_by_id;
  GHashTable *tools_by_serial;

  GSList *all_devices;

  GList *master_devices;
  GList *slave_devices;

  int opcode;

#ifdef HAVE_LIBWACOM
  WacomDeviceDatabase *wacom_db;
#endif
};

gboolean meta_device_manager_x11_translate_event (MetaDeviceManagerX11 *manager_xi2,
                                                  XEvent               *xevent,
                                                  ClutterEvent         *event);

G_END_DECLS

#endif /* META_DEVICE_MANAGER_X11_H */
