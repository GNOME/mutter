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
 * ClutterVirtualDeviceType:
 */
typedef enum _ClutterVirtualDeviceType
{
  CLUTTER_VIRTUAL_DEVICE_TYPE_NONE = 0,
  CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD = 1 << 0,
  CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER = 1 << 1,
  CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN = 1 << 2,
} ClutterVirtualDeviceType;

/**
 * ClutterKbdA11ySettings:
 *
 * The #ClutterKbdA11ySettings structure contains keyboard accessibility
 * settings
 *
 */
typedef struct _ClutterKbdA11ySettings
{
  ClutterKeyboardA11yFlags controls;
  gint slowkeys_delay;
  gint debounce_delay;
  gint timeout_delay;
  gint mousekeys_init_delay;
  gint mousekeys_max_speed;
  gint mousekeys_accel_time;
} ClutterKbdA11ySettings;

/**
 * ClutterPointerA11ySettings:
 *
 * The #ClutterPointerA11ySettings structure contains pointer accessibility
 * settings
 *
 */
typedef struct _ClutterPointerA11ySettings
{
  ClutterPointerA11yFlags controls;
  ClutterPointerA11yDwellClickType dwell_click_type;
  ClutterPointerA11yDwellMode dwell_mode;
  ClutterPointerA11yDwellDirection dwell_gesture_single;
  ClutterPointerA11yDwellDirection dwell_gesture_double;
  ClutterPointerA11yDwellDirection dwell_gesture_drag;
  ClutterPointerA11yDwellDirection dwell_gesture_secondary;
  gint secondary_click_delay;
  gint dwell_delay;
  gint dwell_threshold;
} ClutterPointerA11ySettings;

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
  ClutterVirtualInputDevice *(* create_virtual_device) (ClutterDeviceManager  *device_manager,
                                                        ClutterInputDeviceType device_type);
  ClutterVirtualDeviceType (* get_supported_virtual_device_types) (ClutterDeviceManager *device_manager);
  void                (* compress_motion) (ClutterDeviceManager *device_manger,
                                           ClutterEvent         *event,
                                           const ClutterEvent   *to_discard);
  /* Keyboard accessbility */
  void                (* apply_kbd_a11y_settings) (ClutterDeviceManager   *device_manger,
                                                   ClutterKbdA11ySettings *settings);

  /* Event platform data */
  void (* copy_event_data) (ClutterDeviceManager *device_manager,
                            const ClutterEvent   *src,
                            ClutterEvent         *dest);
  void (* free_event_data) (ClutterDeviceManager *device_manager,
                            ClutterEvent         *event);

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

CLUTTER_EXPORT
ClutterVirtualInputDevice *clutter_device_manager_create_virtual_device (ClutterDeviceManager  *device_manager,
                                                                         ClutterInputDeviceType device_type);

CLUTTER_EXPORT
ClutterVirtualDeviceType clutter_device_manager_get_supported_virtual_device_types (ClutterDeviceManager *device_manager);

CLUTTER_EXPORT
void clutter_device_manager_set_kbd_a11y_settings (ClutterDeviceManager   *device_manager,
                                                   ClutterKbdA11ySettings *settings);

CLUTTER_EXPORT
void clutter_device_manager_get_kbd_a11y_settings (ClutterDeviceManager   *device_manager,
                                                   ClutterKbdA11ySettings *settings);

CLUTTER_EXPORT
void clutter_device_manager_set_pointer_a11y_settings (ClutterDeviceManager       *device_manager,
                                                       ClutterPointerA11ySettings *settings);

CLUTTER_EXPORT
void clutter_device_manager_get_pointer_a11y_settings (ClutterDeviceManager       *device_manager,
                                                       ClutterPointerA11ySettings *settings);

CLUTTER_EXPORT
void clutter_device_manager_set_pointer_a11y_dwell_click_type (ClutterDeviceManager             *device_manager,
                                                               ClutterPointerA11yDwellClickType  click_type);

G_END_DECLS

#endif /* __CLUTTER_DEVICE_MANAGER_H__ */
