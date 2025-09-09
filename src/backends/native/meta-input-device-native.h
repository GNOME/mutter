/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010 Intel Corp.
 * Copyright (C) 2014 Jonas Ådahl
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 * Author: Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#ifndef META_INPUT_THREAD_H_INSIDE
#error "This header cannot be included directly. Use "backends/native/meta-input-thread.h""
#endif /* META_INPUT_THREAD_H_INSIDE */

#include <glib-object.h>

#include "backends/meta-input-device-private.h"
#include "backends/meta-input-settings-private.h"
#include "backends/native/meta-seat-native.h"
#include "clutter/clutter-mutter.h"

#define META_TYPE_INPUT_DEVICE_NATIVE meta_input_device_native_get_type()

#define META_INPUT_DEVICE_NATIVE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNative))

#define META_INPUT_DEVICE_NATIVE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNativeClass))

#define META_IS_INPUT_DEVICE_NATIVE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE))

#define META_IS_INPUT_DEVICE_NATIVE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  META_TYPE_INPUT_DEVICE_NATIVE))

#define META_INPUT_DEVICE_NATIVE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  META_TYPE_INPUT_DEVICE_NATIVE, MetaInputDeviceNativeClass))

typedef enum
{
  META_INPUT_DEVICE_MAPPING_ABSOLUTE,
  META_INPUT_DEVICE_MAPPING_RELATIVE,
} MetaInputDeviceMapping;

typedef struct _MetaInputDeviceNative MetaInputDeviceNative;
typedef struct _MetaInputDeviceNativeClass MetaInputDeviceNativeClass;

struct _MetaInputDeviceNative
{
  ClutterInputDevice parent;

  struct libinput_device *libinput_device;
  MetaSeatImpl *seat_impl;
  ClutterInputDeviceTool *last_tool;
  GArray *pad_features;
  GArray *modes;
  intptr_t group;

  graphene_matrix_t device_matrix;
  int width;
  int height;
  double device_aspect_ratio; /* w:h */
  double output_ratio;        /* w:h */
  MetaInputDeviceMapping mapping_mode;

  ClutterModifierType button_state;

  /* When the client doesn't support high-resolution scroll, accumulate deltas
   * until we can notify a discrete event.
   * Some mice have a free spinning wheel, making possible to lock the wheel
   * when the accumulator value is not 0. To avoid synchronization issues
   * between the mouse wheel and the accumulators, store the last delta and when
   * the scroll direction changes, reset the accumulator. */
  struct {
    int32_t acc_dx;
    int32_t acc_dy;
    int32_t last_dx;
    int32_t last_dy;
  } value120;
};

struct _MetaInputDeviceNativeClass
{
  ClutterInputDeviceClass parent_class;
};

GType                     meta_input_device_native_get_type        (void) G_GNUC_CONST;

ClutterInputDevice *      meta_input_device_native_new_in_impl     (MetaSeatImpl            *seat_impl,
                                                                    struct libinput_device  *libinput_device);

ClutterInputDevice *      meta_input_device_native_new_virtual_in_impl (MetaSeatImpl           *seat_impl,
                                                                        ClutterInputDeviceType  type,
                                                                        ClutterInputMode        mode);

void                      meta_input_device_native_update_leds_in_impl (MetaInputDeviceNative   *device,
                                                                        enum libinput_led        leds);

ClutterInputDeviceType    meta_input_device_native_determine_type_in_impl  (struct libinput_device  *libinput_device);


void                      meta_input_device_native_translate_coordinates_in_impl (ClutterInputDevice *device,
                                                                                  MetaViewportInfo   *viewports,
                                                                                  float              *x,
                                                                                  float              *y);

MetaInputDeviceMapping    meta_input_device_native_get_mapping_mode_in_impl (ClutterInputDevice     *device);
void                      meta_input_device_native_set_mapping_mode_in_impl (ClutterInputDevice     *device,
                                                                             MetaInputDeviceMapping  mapping);

struct libinput_device * meta_input_device_native_get_libinput_device (ClutterInputDevice *device);

void                     meta_input_device_native_detach_libinput_in_impl (MetaInputDeviceNative *device_native);

gboolean                 meta_input_device_native_has_scroll_inverted (MetaInputDeviceNative *device_native);
