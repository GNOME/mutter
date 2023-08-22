/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-eis-client.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-viewport-info.h"

#include "clutter/clutter-mutter.h"
#include "core/meta-anonymous-file.h"

#define MAX_BUTTON 128
#define MAX_KEY 0x2ff /* KEY_MAX as of 5.13 */

typedef struct _MetaEisDevice MetaEisDevice;

struct _MetaEisDevice
{
  struct eis_device *eis_device;
  ClutterVirtualInputDevice *device;

  guchar button_state[(MAX_BUTTON + 7) / 8];
  guchar key_state[(MAX_KEY + 7) / 8];
};

struct _MetaEisClient
{
  GObject parent_instance;
  MetaEis *meta_eis;

  MetaViewportInfo *viewports;
  struct eis_client *eis_client;
  struct eis_seat *eis_seat;

  GHashTable *eis_devices; /* eis_device => MetaEisDevice*/
};

G_DEFINE_TYPE (MetaEisClient, meta_eis_client, G_TYPE_OBJECT)

typedef void (* MetaEisDeviceConfigFunc) (MetaEisClient     *meta_eis_client,
                                          struct eis_device *device);

static bool
bit_is_set (const guchar *array,
            int           bit)
{
  return !!(array[bit / 8] & (1 << (bit % 8)));
}

static void
bit_set (guchar *array,
         int     bit)
{
  array[bit / 8] |= (1 << (bit % 8));
}

static void
bit_clear (guchar *array,
           int     bit)
{
  array[bit / 8] &= ~(1 << (bit % 8));
}

static void
notify_key (MetaEisDevice *meta_eis_device,
            uint32_t       key,
            gboolean       is_press)
{
  ClutterKeyState state;

  state = is_press ?  CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;
  clutter_virtual_input_device_notify_key (meta_eis_device->device,
                                           g_get_monotonic_time (),
                                           key,
                                           state);
}

static void
notify_button (MetaEisDevice *meta_eis_device,
               uint32_t       button,
               gboolean       is_press)
{
  ClutterButtonState state;

  state = is_press ? CLUTTER_BUTTON_STATE_PRESSED : CLUTTER_BUTTON_STATE_RELEASED;
  clutter_virtual_input_device_notify_button (meta_eis_device->device,
                                              g_get_monotonic_time (),
                                              button,
                                              state);
}

static void
remove_device (MetaEisClient     *meta_eis_client,
               struct eis_device *eis_device,
               gboolean           remove_from_hashtable)
{
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  struct eis_keymap *eis_keymap = eis_device_keyboard_get_keymap (eis_device);

  if (eis_keymap)
    {
      MetaAnonymousFile *f = eis_keymap_get_user_data (eis_keymap);
      if (f)
        meta_anonymous_file_free (f);
    }

  eis_device_pause (eis_device);
  eis_device_remove (eis_device);
  g_clear_pointer (&meta_eis_device->eis_device, eis_device_unref);
  g_clear_object (&meta_eis_device->device);

  if (remove_from_hashtable)
    g_hash_table_remove (meta_eis_client->eis_devices, eis_device);
}

static gboolean
drop_device (gpointer htkey,
             gpointer value,
             gpointer data)
{
  MetaEisClient *meta_eis_client = data;
  struct eis_device *eis_device = htkey;
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  uint32_t key, button;

  for (key = 0; key < MAX_KEY; key++)
    {
      if (bit_is_set (meta_eis_device->key_state, key))
        notify_key (meta_eis_device, key, FALSE);
    }

  for (button = 0; button < MAX_BUTTON; button++)
    {
      if (bit_is_set (meta_eis_device->button_state, key))
        notify_button (meta_eis_device, button, FALSE);
    }

  remove_device (meta_eis_client, eis_device, FALSE);
  return TRUE;
}

static void
meta_eis_device_free (MetaEisDevice *device)
{
  eis_device_unref (device->eis_device);
  free (device);
}

static void
configure_rel (MetaEisClient     *meta_eis_client,
               struct eis_device *eis_device)
{
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_POINTER);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_BUTTON);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_SCROLL);
}

static void
configure_keyboard (MetaEisClient     *meta_eis_client,
                    struct eis_device *eis_device)
{
  size_t len;
  MetaAnonymousFile *f;
  int fd = -1;
  char *data;
  struct xkb_keymap *xkb_keymap;

  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_KEYBOARD);

  xkb_keymap =
    meta_backend_get_keymap (meta_eis_get_backend (meta_eis_client->meta_eis));
  if (!xkb_keymap)
    return;

  data = xkb_keymap_get_as_string (xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!data)
    return;

  len = strlen (data);
  f = meta_anonymous_file_new (len, (uint8_t*)data);
  if (f)
    fd = meta_anonymous_file_open_fd (f, META_ANONYMOUS_FILE_MAPMODE_SHARED);

  g_free (data);
  if (fd != -1)
    {
      struct eis_keymap *eis_keymap;

      eis_keymap = eis_device_new_keymap (eis_device, EIS_KEYMAP_TYPE_XKB,
                                          fd, len);
      /* libeis dup()s the fd */
      meta_anonymous_file_close_fd (fd);
      /* The memfile must be kept alive while the device is alive */
      eis_keymap_set_user_data (eis_keymap, f);
      eis_keymap_add (eis_keymap);
      eis_keymap_unref (eis_keymap);
    }
}

static void
configure_abs (MetaEisClient     *meta_eis_client,
               struct eis_device *eis_device)
{
  int idx = 0;
  cairo_rectangle_int_t rect;
  float scale;

  if (!meta_eis_client->viewports)
    return; /* FIXME: should be an error */

  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_BUTTON);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_SCROLL);

  while (meta_viewport_info_get_view_info (meta_eis_client->viewports, idx++, &rect, &scale))
    {
      struct eis_region *r = eis_device_new_region (eis_device);
      eis_region_set_offset (r, rect.x, rect.y);
      eis_region_set_size (r, rect.width, rect.height);
      eis_region_set_physical_scale (r, scale);
      eis_region_add (r);
      eis_region_unref (r);
    }
}

static void
add_device (MetaEisClient          *meta_eis_client,
            struct eis_seat        *eis_seat,
            ClutterInputDeviceType  type,
            const char             *name_suffix,
            MetaEisDeviceConfigFunc extra_config_func)
{
  MetaBackend *backend = meta_eis_get_backend (meta_eis_client->meta_eis);
  MetaEisDevice *meta_eis_device;
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterVirtualInputDevice *device;
  struct eis_device *eis_device;
  gchar *name;

  device = clutter_seat_create_virtual_device (seat, type);
  eis_device = eis_seat_new_device (eis_seat);
  name = g_strdup_printf ("%s %s", eis_client_get_name (meta_eis_client->eis_client),
                          name_suffix);
  eis_device_configure_name (eis_device, name);
  if (extra_config_func)
    extra_config_func (meta_eis_client, eis_device);

  meta_eis_device = g_new0 (MetaEisDevice, 1);
  meta_eis_device->eis_device = eis_device_ref (eis_device);
  meta_eis_device->device = device;
  eis_device_set_user_data (eis_device, meta_eis_device);

  g_hash_table_insert (meta_eis_client->eis_devices,
                       eis_device, /* owns the initial ref now */
                       meta_eis_device);

  eis_device_add (eis_device);
  eis_device_resume (eis_device);
  g_free (name);
}

static void
handle_motion_relative (MetaEisClient	 *meta_eis_client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  double dx, dy;

  dx = eis_event_pointer_get_dx (event);
  dy = eis_event_pointer_get_dy (event);

  clutter_virtual_input_device_notify_relative_motion (meta_eis_device->device,
                                                       g_get_monotonic_time (),
                                                       dx, dy);
}

static void
handle_motion_absolute (MetaEisClient    *meta_eis_client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  double x, y;

  x = eis_event_pointer_get_absolute_x (event);
  y = eis_event_pointer_get_absolute_y (event);

  clutter_virtual_input_device_notify_absolute_motion (meta_eis_device->device,
                                                       g_get_monotonic_time (),
                                                       x, y);
}

static void
handle_scroll (MetaEisClient    *meta_eis_client,
               struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  double dx, dy;

  dx = eis_event_scroll_get_dx (event);
  dy = eis_event_scroll_get_dy (event);

  clutter_virtual_input_device_notify_scroll_continuous (meta_eis_device->device,
                                                         g_get_monotonic_time (),
                                                         dx, dy,
                                                         CLUTTER_SCROLL_SOURCE_UNKNOWN,
                                                         CLUTTER_SCROLL_FINISHED_NONE);
}

static void
handle_scroll_stop (MetaEisClient    *meta_eis_client,
                    struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (eis_event_scroll_get_stop_x (event))
    finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
  if (eis_event_scroll_get_stop_y (event))
    finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;

  if (finish_flags != CLUTTER_SCROLL_FINISHED_NONE)
    clutter_virtual_input_device_notify_scroll_continuous (meta_eis_device->device,
                                                           g_get_monotonic_time (),
                                                           0.0, 0.0,
                                                           CLUTTER_SCROLL_SOURCE_UNKNOWN,
                                                           finish_flags);
}

static void
handle_scroll_cancel (MetaEisClient    *meta_eis_client,
                    struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  double dx = 0.0, dy = 0.0;

  /* There's no real match for the EIS scroll cancel event, so let's just send a
   * really small scroll event that hopefully resets the scroll speed to
   * something where kinetic scrolling is not viable.
   */
  if (eis_event_scroll_get_stop_x (event))
    dx = 0.01;
  if (eis_event_scroll_get_stop_y (event))
    dy = 0.01;

  if (dx != 0.0 || dy != 0.0)
    clutter_virtual_input_device_notify_scroll_continuous (meta_eis_device->device,
                                                           g_get_monotonic_time (),
                                                           dx, dy,
                                                           CLUTTER_SCROLL_SOURCE_UNKNOWN,
                                                           CLUTTER_SCROLL_FINISHED_NONE);
}

static void
handle_scroll_discrete (MetaEisClient    *meta_eis_client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  int dx, dy;

  /* FIXME: need to handle the remainders here for high-resolution scrolling */
  dx = eis_event_scroll_get_discrete_dx (event) / 120;
  dy = eis_event_scroll_get_discrete_dy (event) / 120;

  /* Intentionally interleaved */
  while (dx || dy)
    {
      if (dx > 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (meta_eis_device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_RIGHT,
                                                               CLUTTER_SCROLL_SOURCE_UNKNOWN);
          dx--;
        }
      else if (dx < 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (meta_eis_device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_LEFT,
                                                               CLUTTER_SCROLL_SOURCE_UNKNOWN);
          dx++;
        }

      if (dy > 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (meta_eis_device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_DOWN,
                                                               CLUTTER_SCROLL_SOURCE_UNKNOWN);
          dy--;
        }
      else if (dy < 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (meta_eis_device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_UP,
                                                               CLUTTER_SCROLL_SOURCE_UNKNOWN);
          dy++;
        }
    }
}

static void
handle_button (MetaEisClient    *meta_eis_client,
               struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  uint32_t button;
  gboolean is_press = eis_event_button_get_is_press (event);

  button = eis_event_button_get_button (event);
  switch (button)
    {
    case 0x110: /* BTN_LEFT */
      button = CLUTTER_BUTTON_PRIMARY;
      break;
    case 0x111: /* BTN_RIGHT */
      button = CLUTTER_BUTTON_SECONDARY;
      break;
    case 0x112: /* BTN_MIDDLE */
      button = CLUTTER_BUTTON_MIDDLE;
      break;
    default:
      if (button > 0x110)
        button -= 0x110;
      break;
    }

  if (button > MAX_BUTTON)
    return;

  if (is_press && !bit_is_set (meta_eis_device->button_state, button))
    bit_set (meta_eis_device->button_state, button);
  else if (!is_press && bit_is_set (meta_eis_device->button_state, button))
    bit_clear (meta_eis_device->button_state, button);
  else
    return; /* Duplicate press/release, should've been filtered by libeis */

  notify_button (meta_eis_device,
                 button,
                 eis_event_button_get_is_press (event));
}

static void
handle_key (MetaEisClient    *meta_eis_client,
            struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *meta_eis_device = eis_device_get_user_data (eis_device);
  uint32_t key;
  gboolean is_press = eis_event_keyboard_get_key_is_press (event);

  key = eis_event_keyboard_get_key (event);

  if (key > MAX_KEY)
    return;

  if (is_press && !bit_is_set (meta_eis_device->key_state, key))
    bit_set (meta_eis_device->key_state, key);
  else if (!is_press && bit_is_set (meta_eis_device->key_state, key))
    bit_clear (meta_eis_device->key_state, key);
  else
    return; /* Duplicate press/release, should've been filtered by libeis */

  notify_key (meta_eis_device, key, is_press);
}

static gboolean
drop_kbd_devices (gpointer htkey,
                  gpointer value,
                  gpointer data)
{
  struct eis_device *eis_device = htkey;

  if (!eis_device_has_capability (eis_device, EIS_DEVICE_CAP_KEYBOARD))
    return FALSE;

  return drop_device (htkey, value, data);
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer     data)
{
  MetaEisClient *meta_eis_client = data;

  /* Changing the keymap means we have to remove our device and recreate it
   * with the new keymap.
   */
  g_hash_table_foreach_remove (meta_eis_client->eis_devices,
                               drop_kbd_devices,
                               meta_eis_client);

  add_device (meta_eis_client,
              meta_eis_client->eis_seat,
              CLUTTER_KEYBOARD_DEVICE,
              "virtual keyboard",
              configure_keyboard);
}

gboolean
meta_eis_client_process_event (MetaEisClient    *meta_eis_client,
                               struct eis_event *event)
{
  enum eis_event_type type = eis_event_get_type (event);
  struct eis_seat *eis_seat;

  switch (type)
    {
    case EIS_EVENT_SEAT_BIND:
      eis_seat = eis_event_get_seat (event);
      if (eis_event_seat_has_capability (event, EIS_DEVICE_CAP_POINTER))
        add_device (meta_eis_client,
                    eis_seat,
                    CLUTTER_POINTER_DEVICE,
                    "virtual pointer",
                    configure_rel);
      if (eis_event_seat_has_capability (event, EIS_DEVICE_CAP_KEYBOARD))
        {
          add_device (meta_eis_client,
                      eis_seat,
                      CLUTTER_KEYBOARD_DEVICE,
                      "virtual keyboard",
                      configure_keyboard);

            g_signal_connect (meta_eis_get_backend (meta_eis_client->meta_eis),
                              "keymap-changed",
                              G_CALLBACK (on_keymap_changed),
                              meta_eis_client);
        }
      if (eis_event_seat_has_capability (event, EIS_DEVICE_CAP_POINTER_ABSOLUTE))
          add_device (meta_eis_client,
                      eis_seat,
                      CLUTTER_POINTER_DEVICE,
                      "virtual absolute pointer",
                      configure_abs);
      break;

    /* We only have one seat, so if the client unbinds from that
     * just disconnect it, no point keeping it alive */
    case EIS_EVENT_DEVICE_CLOSED:
      remove_device (meta_eis_client, eis_event_get_device (event), TRUE);
      break;
    case EIS_EVENT_POINTER_MOTION:
      handle_motion_relative (meta_eis_client, event);
      break;
    case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
      handle_motion_absolute (meta_eis_client, event);
      break;
    case EIS_EVENT_BUTTON_BUTTON:
      handle_button (meta_eis_client, event);
      break;
    case EIS_EVENT_SCROLL_DELTA:
      handle_scroll (meta_eis_client, event);
      break;
    case EIS_EVENT_SCROLL_STOP:
      handle_scroll_stop (meta_eis_client, event);
      break;
    case EIS_EVENT_SCROLL_CANCEL:
      handle_scroll_cancel (meta_eis_client, event);
      break;
    case EIS_EVENT_SCROLL_DISCRETE:
      handle_scroll_discrete (meta_eis_client, event);
      break;
    case EIS_EVENT_KEYBOARD_KEY:
      handle_key (meta_eis_client, event);
      break;
    case EIS_EVENT_FRAME:
      /* FIXME: we should be accumulating the above events */
      break;
    case EIS_EVENT_DEVICE_START_EMULATING:
      break;
    case EIS_EVENT_DEVICE_STOP_EMULATING:
      break;
    default:
      g_warning ("Unhandled EIS event type %d", type);
      return FALSE;
    }

  return TRUE;
}

static gboolean
drop_abs_devices (gpointer key,
                  gpointer value,
                  gpointer data)
{
  struct eis_device *eis_device = key;

  if (!eis_device_has_capability (eis_device, EIS_DEVICE_CAP_POINTER_ABSOLUTE))
    return FALSE;

  return drop_device (key, value, data);
}

static void
meta_eis_client_set_viewports (MetaEisClient    *meta_eis_client,
                               MetaViewportInfo *viewports)
{
  /* Updating viewports means we have to recreate our absolute pointer
   * devices. */

  g_hash_table_foreach_remove (meta_eis_client->eis_devices,
                               drop_abs_devices,
                               meta_eis_client);

  g_clear_object (&meta_eis_client->viewports);
  meta_eis_client->viewports = g_object_ref (viewports);

  add_device (meta_eis_client,
              meta_eis_client->eis_seat,
              CLUTTER_POINTER_DEVICE,
              "virtual absolute pointer",
              configure_abs);
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaEisClient      *meta_eis_client)
{
  MetaViewportInfo *viewports;

  viewports = meta_monitor_manager_get_viewports (monitor_manager);
  meta_eis_client_set_viewports (meta_eis_client, viewports);
}

static void
meta_eis_client_disconnect (MetaEisClient *meta_eis_client)
{
  MetaBackend *backend = meta_eis_get_backend (meta_eis_client->meta_eis);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);

  g_signal_handlers_disconnect_by_func (monitor_manager,
                                        on_monitors_changed,
                                        meta_eis_client);
  g_hash_table_foreach_remove (meta_eis_client->eis_devices, drop_device, meta_eis_client);
  g_clear_pointer (&meta_eis_client->eis_seat, eis_seat_unref);
  if (meta_eis_client->eis_client)
    eis_client_disconnect (meta_eis_client->eis_client);
  g_clear_pointer (&meta_eis_client->eis_client, eis_client_unref);
  g_clear_object (&meta_eis_client->viewports);
}

MetaEisClient *
meta_eis_client_new (MetaEis           *meta_eis,
                     struct eis_client *eis_client)
{
  MetaEisClient *meta_eis_client;
  MetaBackend *backend;
  MetaMonitorManager *monitor_manager;
  MetaViewportInfo *viewports;
  struct eis_seat *eis_seat;

  meta_eis_client = g_object_new (META_TYPE_EIS_CLIENT, NULL);
  meta_eis_client->meta_eis = meta_eis;
  meta_eis_client->eis_client = eis_client_ref (eis_client);
  eis_client_set_user_data (meta_eis_client->eis_client, meta_eis_client);

  /* We're relying on some third party to filter clients for us */
  eis_client_connect (eis_client);

  /* We only support one seat for now. libeis keeps the ref for the
   * seat and we don't need to care about it.
   * The capabilities we define are the maximum capabilities, the client
   * may only bind to a subset of those, reducing the capabilities
   * of the seat in the process.
   */
  eis_seat = eis_client_new_seat (eis_client, "mutter default seat");
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_KEYBOARD);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_POINTER);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_BUTTON);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_SCROLL);

  eis_seat_add (eis_seat);
  eis_seat_unref (eis_seat);
  meta_eis_client->eis_seat = eis_seat_ref (eis_seat);

  meta_eis_client->eis_devices = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                        (GDestroyNotify) eis_device_unref,
                                                        (GDestroyNotify) meta_eis_device_free);

  backend = meta_eis_get_backend (meta_eis);
  monitor_manager = meta_backend_get_monitor_manager (backend);
  viewports = meta_monitor_manager_get_viewports (monitor_manager);
  meta_eis_client_set_viewports (meta_eis_client, viewports);
  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed),
                    meta_eis_client);

  return meta_eis_client;
}

static void
meta_eis_client_init (MetaEisClient *meta_eis_client)
{
}

static void
meta_eis_client_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (meta_eis_client_parent_class)->constructed)
    G_OBJECT_CLASS (meta_eis_client_parent_class)->constructed (object);
}

static void
meta_eis_client_finalize (GObject *object)
{
  MetaEisClient *meta_eis_client = META_EIS_CLIENT (object);

  g_signal_handlers_disconnect_by_func (meta_eis_get_backend (meta_eis_client->meta_eis),
                                        on_keymap_changed,
                                        meta_eis_client);
  meta_eis_client_disconnect (meta_eis_client);

  G_OBJECT_CLASS (meta_eis_client_parent_class)->finalize (object);
}

static void
meta_eis_client_class_init (MetaEisClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_eis_client_constructed;
  object_class->finalize = meta_eis_client_finalize;
}
