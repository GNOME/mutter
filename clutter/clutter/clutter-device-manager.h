/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corp.
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

#ifndef __CLUTTER_DEVICE_MANAGER_H__
#define __CLUTTER_DEVICE_MANAGER_H__

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include <clutter/clutter-input-device.h>
#include <clutter/clutter-stage.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_DEVICE_MANAGER (clutter_device_manager_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterDeviceManager, clutter_device_manager,
                          CLUTTER, DEVICE_MANAGER, GObject)

typedef struct _ClutterDeviceManagerPrivate     ClutterDeviceManagerPrivate;

/**
 * ClutterDeviceManagerClass:
 *
 * The #ClutterDeviceManagerClass structure contains only private data
 *
 * Since: 1.2
 */
struct _ClutterDeviceManagerClass
{
  /*< private >*/
  GObjectClass parent_class;

  const GSList *      (* get_devices)     (ClutterDeviceManager   *device_manager);
  ClutterInputDevice *(* get_core_device) (ClutterDeviceManager   *device_manager,
                                           ClutterInputDeviceType  device_type);
  ClutterInputDevice *(* get_device)      (ClutterDeviceManager   *device_manager,
                                           gint                    device_id);

  void                (* add_device)      (ClutterDeviceManager   *manager,
                                           ClutterInputDevice     *device);
  void                (* remove_device)   (ClutterDeviceManager   *manager,
                                           ClutterInputDevice     *device);
  void                (* select_stage_events) (ClutterDeviceManager *manager,
                                               ClutterStage       *stage);

  /* padding */
  gpointer _padding[4];
};

CLUTTER_EXPORT
ClutterDeviceManager *clutter_device_manager_get_default     (void);
CLUTTER_EXPORT
GSList *              clutter_device_manager_list_devices    (ClutterDeviceManager   *device_manager);
CLUTTER_EXPORT
const GSList *        clutter_device_manager_peek_devices    (ClutterDeviceManager   *device_manager);

CLUTTER_EXPORT
ClutterInputDevice *  clutter_device_manager_get_device      (ClutterDeviceManager   *device_manager,
                                                              gint                    device_id);
CLUTTER_EXPORT
ClutterInputDevice *  clutter_device_manager_get_core_device (ClutterDeviceManager   *device_manager,
                                                              ClutterInputDeviceType  device_type);

G_END_DECLS

#endif /* __CLUTTER_DEVICE_MANAGER_H__ */
