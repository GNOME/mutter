/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Input device map, XInput2 implementation */

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

#include "config.h"
#include "device-map-xi2.h"
#include <X11/extensions/XInput2.h>
#include "devices-xi2.h"

#define XINPUT2_VERSION_MAJOR 2
#define XINPUT2_VERSION_MINOR 2

G_DEFINE_TYPE (MetaDeviceMapXI2, meta_device_map_xi2, META_TYPE_DEVICE_MAP)

static gboolean
meta_device_map_xi2_grab_key (MetaDeviceMap *device_map,
                              Window         xwindow,
                              guint          keycode,
                              guint          modifiers,
                              gboolean       sync)
{
  XIGrabModifiers mods = { modifiers, 0 };
  MetaDisplay *display;
  XIEventMask mask;
  gint retval;

  display = meta_device_map_get_display (device_map);

  mask.deviceid = XIAllMasterDevices;
  mask.mask = meta_device_xi2_translate_event_mask (KeyPressMask |
                                                    KeyReleaseMask,
                                                    &mask.mask_len);
  /* FIXME: Doesn't seem to work with
   * XIAllMasterDevices, use the VCK
   * at the moment
   */
  retval = XIGrabKeycode (display->xdisplay,
                          META_CORE_KEYBOARD_ID,
                          keycode, xwindow,
                          (sync) ? GrabModeSync : GrabModeAsync,
                          GrabModeAsync, /* Never care about the other device */
                          True, &mask, 1, &mods);

  return (retval == Success);
}

static void
meta_device_map_xi2_ungrab_key (MetaDeviceMap *device_map,
                                Window         xwindow,
                                guint          keycode,
                                guint          modifiers)
{
  XIGrabModifiers mods = { modifiers, 0 };
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);
  XIUngrabKeycode (display->xdisplay,
                   META_CORE_KEYBOARD_ID,
                   keycode, xwindow,
                   1, &mods);
}

static gboolean
meta_device_map_xi2_grab_button (MetaDeviceMap *device_map,
                                 Window         xwindow,
                                 guint          n_button,
                                 guint          modifiers,
                                 guint          evmask,
                                 gboolean       sync)
{
  XIGrabModifiers mods = { modifiers, 0 };
  XIEventMask mask;
  MetaDisplay *display;
  int retval;

  display = meta_device_map_get_display (device_map);

  mask.deviceid = XIAllMasterDevices;
  mask.mask = meta_device_xi2_translate_event_mask (evmask, &mask.mask_len);

  retval = XIGrabButton (display->xdisplay,
                         XIAllMasterDevices,
                         n_button, xwindow, None,
                         (sync) ? GrabModeSync : GrabModeAsync,
                         GrabModeAsync, /* Never care about the other device */
                         False, &mask, 1, &mods);

  return (retval == Success);
}

static void
meta_device_map_xi2_ungrab_button (MetaDeviceMap *device_map,
                                   Window         xwindow,
                                   guint          n_button,
                                   guint          modifiers)
{
  XIGrabModifiers mods = { modifiers, 0 };
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);
  XIUngrabButton (display->xdisplay,
                  META_CORE_POINTER_ID,
                  //XIAllMasterDevices,
                  n_button, xwindow, 1, &mods);
}

static void
add_device_from_info (MetaDeviceMap *device_map,
                      gint           use,
                      gint           device_id)
{
  MetaDevice *device;
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);

  if (use == XIMasterPointer)
    device = meta_device_pointer_xi2_new (display, device_id);
  else if (use == XIMasterKeyboard)
    device = meta_device_keyboard_xi2_new (display, device_id);

  if (device)
    {
      meta_device_map_add_device (device_map, device);
      g_object_unref (device);
    }
}

static void
pair_devices (gpointer key,
              gpointer value,
              gpointer user_data)
{
  MetaDevice *device1, *device2;
  MetaDeviceMap *device_map;

  device_map = user_data;
  device1 = meta_device_map_lookup (device_map, GPOINTER_TO_INT (key));
  device2 = meta_device_map_lookup (device_map, GPOINTER_TO_INT (value));

  meta_device_pair_devices (device1, device2);
}

static void
meta_device_map_xi2_constructed (GObject *object)
{
  MetaDeviceMap *device_map = META_DEVICE_MAP (object);
  MetaDisplay *display;
  XIDeviceInfo *info;
  GHashTable *pairs;
  int n_devices, i;

  display = meta_device_map_get_display (device_map);

  /* We're only interested in master devices,
   * detached slave devices are left for applications
   * to handle.
   */
  info = XIQueryDevice (display->xdisplay, XIAllMasterDevices, &n_devices);
  pairs = g_hash_table_new (NULL, NULL);

  for (i = 0; i < n_devices; i++)
    {
      add_device_from_info (device_map, info[i].use, info[i].deviceid);
      g_hash_table_insert (pairs,
                           GINT_TO_POINTER (info[i].deviceid),
                           GINT_TO_POINTER (info[i].attachment));
    }

  g_hash_table_foreach (pairs, pair_devices, device_map);
  g_hash_table_destroy (pairs);

  XIFreeDeviceInfo (info);
}

static void
meta_device_map_xi2_grab_touch (MetaDeviceMap *device_map,
                                Window         xwindow)
{
  XIGrabModifiers unused = { 0 };
  MetaDisplay *display;
  XIEventMask mask;

  display = meta_device_map_get_display (device_map);

  g_message ("Grabbing passively on touch begin\n");

  mask.deviceid = XIAllMasterDevices;
  mask.mask = meta_device_xi2_translate_event_mask (META_INPUT_TOUCH_EVENTS_MASK |
                                                    ButtonPressMask |
                                                    ButtonReleaseMask |
                                                    PointerMotionMask |
                                                    KeyPressMask |
                                                    KeyReleaseMask,
                                                    &mask.mask_len);
  XIGrabTouchBegin (display->xdisplay,
                    XIAllMasterDevices,
                    xwindow, True,
                    &mask, 1, &unused);
}

static void
meta_device_map_xi2_ungrab_touch (MetaDeviceMap *device_map,
                                  Window         xwindow)
{
  XIGrabModifiers unused = { 0 };
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);
  XIUngrabTouchBegin (display->xdisplay,
                      XIAllMasterDevices,
                      xwindow, 0, &unused);
}

static void
meta_device_map_xi2_class_init (MetaDeviceMapXI2Class *klass)
{
  MetaDeviceMapClass *device_map_class = META_DEVICE_MAP_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_device_map_xi2_constructed;

  device_map_class->grab_key = meta_device_map_xi2_grab_key;
  device_map_class->ungrab_key = meta_device_map_xi2_ungrab_key;
  device_map_class->grab_button = meta_device_map_xi2_grab_button;
  device_map_class->ungrab_button = meta_device_map_xi2_ungrab_button;
  device_map_class->grab_touch = meta_device_map_xi2_grab_touch;
  device_map_class->ungrab_touch = meta_device_map_xi2_ungrab_touch;
}

static void
meta_device_map_xi2_init (MetaDeviceMapXI2 *device_map)
{
}

gboolean
meta_device_map_xi2_handle_hierarchy_event (MetaDeviceMapXI2 *device_map,
                                            XEvent           *ev)
{
  MetaDisplay *display;

  display = meta_device_map_get_display (META_DEVICE_MAP (device_map));

  if (ev->type == GenericEvent &&
      ev->xcookie.extension == display->xinput2_opcode)
    {
      XIHierarchyEvent *xev;
      GHashTable *pairs;
      gint i;

      g_assert (display->have_xinput2 == TRUE);

      xev = (XIHierarchyEvent *) ev->xcookie.data;

      if (xev->evtype != XI_HierarchyChanged)
        return FALSE;

      pairs = g_hash_table_new (NULL, NULL);

      for (i = 0; i < xev->num_info; i++)
        {
          if (xev->info[i].flags & XIMasterAdded)
            {
              add_device_from_info (META_DEVICE_MAP (device_map),
                                    xev->info[i].use,
                                    xev->info[i].deviceid);
              g_hash_table_insert (pairs,
                                   GINT_TO_POINTER (xev->info[i].deviceid),
                                   GINT_TO_POINTER (xev->info[i].attachment));
            }
          else if (xev->info[i].flags & XIMasterRemoved)
            {
              MetaDevice *device;

              device = meta_device_map_lookup (META_DEVICE_MAP (device_map),
                                               xev->info[i].deviceid);

              if (device)
                meta_device_map_remove_device (META_DEVICE_MAP (device_map),
                                               device);
            }
        }

      g_hash_table_foreach (pairs, pair_devices, device_map);
      g_hash_table_destroy (pairs);

      return TRUE;
    }

  return FALSE;
}
