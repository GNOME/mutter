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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-eis-client.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/native/meta-keymap-native.h"
#include "clutter/clutter-mutter.h"
#include "mtk/mtk.h"

#define MAX_BUTTON 128
#define MAX_KEY 0x2ff /* KEY_MAX as of 5.13 */
#define MAX_SLOTS 64

typedef struct _MetaEisDevice MetaEisDevice;

struct _MetaEisDevice
{
  struct eis_device *eis_device;
  ClutterVirtualInputDevice *device;

  MetaEisViewport *viewport;

  guchar button_state[(MAX_BUTTON + 7) / 8];
  guchar key_state[(MAX_KEY + 7) / 8];

  struct {
    gboolean is_acquired;
    uint32_t eis_touch_id;
  } slots[MAX_SLOTS];
  GHashTable *slot_map;
};

struct _MetaEisClient
{
  GObject parent_instance;
  MetaEis *eis;

  struct eis_client *eis_client;
  struct eis_seat *eis_seat;

  GHashTable *eis_devices; /* eis_device => MetaEisDevice*/
  MetaEisDevice *pointer_device;
  MetaEisDevice *keyboard_device;
  gulong keymap_changed_handler_id;
  gulong keymap_state_changed_handler_id;
  gboolean have_abs_pointer_devices;
  gboolean have_touch_devices;

  gulong viewports_changed_handler_id;
};

G_DEFINE_TYPE (MetaEisClient, meta_eis_client, G_TYPE_OBJECT)

typedef void (* MetaEisDeviceConfigFunc) (MetaEisClient     *client,
                                          struct eis_device *device,
                                          gpointer           user_data);

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
notify_key (MetaEisDevice *device,
            uint32_t       key,
            gboolean       is_press)
{
  ClutterKeyState state;

  state = is_press ?  CLUTTER_KEY_STATE_PRESSED : CLUTTER_KEY_STATE_RELEASED;
  clutter_virtual_input_device_notify_key (device->device,
                                           g_get_monotonic_time (),
                                           key,
                                           state);
}

static void
notify_button (MetaEisDevice *device,
               uint32_t       button,
               gboolean       is_press)
{
  ClutterButtonState state;

  state = is_press ? CLUTTER_BUTTON_STATE_PRESSED : CLUTTER_BUTTON_STATE_RELEASED;
  clutter_virtual_input_device_notify_button (device->device,
                                              g_get_monotonic_time (),
                                              button,
                                              state);
}

static void
remove_device (MetaEisClient     *client,
               struct eis_device *eis_device,
               gboolean           remove_from_hashtable)
{
  struct eis_keymap *eis_keymap = eis_device_keyboard_get_keymap (eis_device);

  if (eis_keymap)
    {
      MtkAnonymousFile *f = eis_keymap_get_user_data (eis_keymap);
      if (f)
        mtk_anonymous_file_free (f);
    }

  if (remove_from_hashtable)
    g_hash_table_remove (client->eis_devices, eis_device);
}

static gboolean
drop_device (gpointer htkey,
             gpointer value,
             gpointer data)
{
  MetaEisClient *client = data;
  struct eis_device *eis_device = htkey;
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  uint32_t key, button;

  for (key = 0; key < MAX_KEY; key++)
    {
      if (bit_is_set (device->key_state, key))
        notify_key (device, key, FALSE);
    }

  for (button = 0; button < MAX_BUTTON; button++)
    {
      if (bit_is_set (device->button_state, button))
        notify_button (device, button, FALSE);
    }

  remove_device (client, eis_device, FALSE);
  return TRUE;
}

static void
remove_abs_devices (gpointer key,
                    gpointer value,
                    gpointer data)
{
  struct eis_device *eis_device = key;

  if (!eis_device_has_capability (eis_device, EIS_DEVICE_CAP_POINTER_ABSOLUTE))
    return;

  eis_device_remove (eis_device);
}

static void
remove_touch_devices (gpointer key,
                      gpointer value,
                      gpointer data)
{
  struct eis_device *eis_device = key;

  if (!eis_device_has_capability (eis_device, EIS_DEVICE_CAP_TOUCH))
    return;

  eis_device_remove (eis_device);
}

static void
remove_viewport_devices (gpointer key,
                         gpointer value,
                         gpointer data)
{
  struct eis_device *eis_device = key;

  if (!eis_device_has_capability (eis_device, EIS_DEVICE_CAP_TOUCH) &&
      !eis_device_has_capability (eis_device, EIS_DEVICE_CAP_POINTER_ABSOLUTE))
    return;

  eis_device_remove (eis_device);
}

static void
meta_eis_device_free (MetaEisDevice *device)
{
  g_clear_object (&device->device);
  eis_device_unref (device->eis_device);
  g_hash_table_unref (device->slot_map);
  free (device);
}

static void
configure_rel (MetaEisClient     *client,
               struct eis_device *eis_device,
               gpointer           user_data)
{
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_POINTER);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_BUTTON);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_SCROLL);
}

static void
configure_keyboard (MetaEisClient     *client,
                    struct eis_device *eis_device,
                    gpointer           user_data)
{
  size_t len;
  MtkAnonymousFile *f;
  int fd = -1;
  char *data;
  struct xkb_keymap *xkb_keymap;

  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_KEYBOARD);

  xkb_keymap =
    meta_backend_get_keymap (meta_eis_get_backend (client->eis));
  if (!xkb_keymap)
    return;

  data = xkb_keymap_get_as_string (xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!data)
    return;

  len = strlen (data);
  f = mtk_anonymous_file_new ("eis-keymap", len, (uint8_t*)data);
  if (f)
    fd = mtk_anonymous_file_open_fd (f, MTK_ANONYMOUS_FILE_MAPMODE_SHARED);

  g_free (data);
  if (fd != -1)
    {
      struct eis_keymap *eis_keymap;

      eis_keymap = eis_device_new_keymap (eis_device, EIS_KEYMAP_TYPE_XKB,
                                          fd, len);
      /* libeis dup()s the fd */
      mtk_anonymous_file_close_fd (fd);
      /* The memfile must be kept alive while the device is alive */
      eis_keymap_set_user_data (eis_keymap, f);
      eis_keymap_add (eis_keymap);
      eis_keymap_unref (eis_keymap);
    }
}

static gboolean
has_region (struct eis_device *eis_device,
            int                x,
            int                y,
            int                width,
            int                height)
{
  size_t i = 0;

  while (TRUE)
    {
      struct eis_region *region;

      region = eis_device_get_region (eis_device, i++);
      if (!region)
        return FALSE;

      if (eis_region_get_x (region) == x &&
          eis_region_get_y (region) == y &&
          eis_region_get_width (region) == width &&
          eis_region_get_height (region) == height)
        return TRUE;
    }
}

static void
add_viewport_region (struct eis_device *eis_device,
                     MetaEisViewport   *viewport)
{
  gboolean has_position;
  int x, y;
  int width, height;
  double scale;
  const char *mapping_id;
  struct eis_region *eis_region;

  has_position = meta_eis_viewport_get_position (viewport, &x, &y);
  meta_eis_viewport_get_size (viewport, &width, &height);
  scale = meta_eis_viewport_get_physical_scale (viewport);

  if (has_region (eis_device, x, y, width, height))
    return;

  eis_region = eis_device_new_region (eis_device);
  if (has_position)
    eis_region_set_offset (eis_region, x, y);
  eis_region_set_size (eis_region, width, height);
  eis_region_set_physical_scale (eis_region, scale);

  mapping_id = meta_eis_viewport_get_mapping_id (viewport);
  if (mapping_id)
    eis_region_set_mapping_id (eis_region, mapping_id);

  eis_region_set_user_data (eis_region, viewport);
  eis_region_add (eis_region);
  eis_region_unref (eis_region);
}

static void
configure_abs (MetaEisClient     *client,
               struct eis_device *eis_device,
               gpointer           user_data)
{
  MetaEisViewport *viewport = META_EIS_VIEWPORT (user_data);

  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_BUTTON);
  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_SCROLL);

  add_viewport_region (eis_device, viewport);
}

static void
configure_touch (MetaEisClient     *client,
                 struct eis_device *eis_device,
                 gpointer           user_data)
{
  MetaEisViewport *viewport = META_EIS_VIEWPORT (user_data);

  eis_device_configure_capability (eis_device, EIS_DEVICE_CAP_TOUCH);

  add_viewport_region (eis_device, viewport);
}

static MetaEisDevice *
create_device (MetaEisClient           *client,
               struct eis_seat         *eis_seat,
               ClutterInputDeviceType   type,
               const char              *name_suffix,
               MetaEisDeviceConfigFunc  extra_config_func,
               gpointer                 extra_config_user_data)
{
  MetaBackend *backend = meta_eis_get_backend (client->eis);
  MetaEisDevice *device;
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterVirtualInputDevice *virtual_device;
  struct eis_device *eis_device;
  gchar *name;

  meta_topic (META_DEBUG_EIS,
              "Creating device '%s'",
              name_suffix);

  virtual_device = clutter_seat_create_virtual_device (seat, type);
  eis_device = eis_seat_new_device (eis_seat);
  name = g_strdup_printf ("%s %s", eis_client_get_name (client->eis_client),
                          name_suffix);
  eis_device_configure_name (eis_device, name);
  if (extra_config_func)
    extra_config_func (client, eis_device, extra_config_user_data);

  device = g_new0 (MetaEisDevice, 1);
  device->eis_device = eis_device_ref (eis_device);
  device->device = virtual_device;
  device->slot_map = g_hash_table_new (NULL, NULL);
  eis_device_set_user_data (eis_device, device);

  g_hash_table_insert (client->eis_devices,
                       eis_device, /* owns the initial ref now */
                       device);

  g_free (name);

  return device;
}

static void
propagate_device (MetaEisDevice *device)
{
  eis_device_add (device->eis_device);
  eis_device_resume (device->eis_device);
}

static MetaEisDevice *
add_device (MetaEisClient           *client,
            struct eis_seat         *eis_seat,
            ClutterInputDeviceType   type,
            const char              *name_suffix,
            MetaEisDeviceConfigFunc  extra_config_func,
            gpointer                 extra_config_user_data)
{
  MetaEisDevice *device;

  device = create_device (client,
                          eis_seat,
                          type,
                          name_suffix,
                          extra_config_func, extra_config_user_data);
  propagate_device (device);

  return device;
}

static void
handle_motion_relative (MetaEisClient	 *client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  double dx, dy;

  dx = eis_event_pointer_get_dx (event);
  dy = eis_event_pointer_get_dy (event);

  clutter_virtual_input_device_notify_relative_motion (device->device,
                                                       g_get_monotonic_time (),
                                                       dx, dy);
}

static MetaEisViewport *
find_viewport (MetaEisDevice *device,
               double         x,
               double         y)
{
  struct eis_region *region;

  if (device->viewport)
    return device->viewport;

  region = eis_device_get_region_at (device->eis_device, x, y);
  if (!region)
    return NULL;

  return META_EIS_VIEWPORT (eis_region_get_user_data (region));
}

static void
handle_motion_absolute (MetaEisClient    *client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  MetaEisViewport *viewport;
  double x, y;

  x = eis_event_pointer_get_absolute_x (event);
  y = eis_event_pointer_get_absolute_y (event);

  viewport = find_viewport (device, x, y);
  if (!viewport)
    return;

  if (!meta_eis_viewport_transform_coordinate (viewport, x, y, &x, &y))
    return;

  clutter_virtual_input_device_notify_absolute_motion (device->device,
                                                       g_get_monotonic_time (),
                                                       x, y);
}

static void
handle_scroll (MetaEisClient    *client,
               struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  double dx, dy;

  dx = eis_event_scroll_get_dx (event);
  dy = eis_event_scroll_get_dy (event);

  clutter_virtual_input_device_notify_scroll_continuous (device->device,
                                                         g_get_monotonic_time (),
                                                         dx, dy,
                                                         CLUTTER_SCROLL_SOURCE_WHEEL,
                                                         CLUTTER_SCROLL_FINISHED_NONE);
}

static void
handle_scroll_stop (MetaEisClient    *client,
                    struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;

  if (eis_event_scroll_get_stop_x (event))
    finish_flags |= CLUTTER_SCROLL_FINISHED_HORIZONTAL;
  if (eis_event_scroll_get_stop_y (event))
    finish_flags |= CLUTTER_SCROLL_FINISHED_VERTICAL;

  if (finish_flags != CLUTTER_SCROLL_FINISHED_NONE)
    clutter_virtual_input_device_notify_scroll_continuous (device->device,
                                                           g_get_monotonic_time (),
                                                           0.0, 0.0,
                                                           CLUTTER_SCROLL_SOURCE_WHEEL,
                                                           finish_flags);
}

static void
handle_scroll_cancel (MetaEisClient    *client,
                      struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
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
    clutter_virtual_input_device_notify_scroll_continuous (device->device,
                                                           g_get_monotonic_time (),
                                                           dx, dy,
                                                           CLUTTER_SCROLL_SOURCE_WHEEL,
                                                           CLUTTER_SCROLL_FINISHED_NONE);
}

static void
handle_scroll_discrete (MetaEisClient    *client,
                        struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  int dx, dy;

  /* FIXME: need to handle the remainders here for high-resolution scrolling */
  dx = eis_event_scroll_get_discrete_dx (event) / 120;
  dy = eis_event_scroll_get_discrete_dy (event) / 120;

  /* Intentionally interleaved */
  while (dx || dy)
    {
      if (dx > 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_RIGHT,
                                                               CLUTTER_SCROLL_SOURCE_WHEEL);
          dx--;
        }
      else if (dx < 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_LEFT,
                                                               CLUTTER_SCROLL_SOURCE_WHEEL);
          dx++;
        }

      if (dy > 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_DOWN,
                                                               CLUTTER_SCROLL_SOURCE_WHEEL);
          dy--;
        }
      else if (dy < 0)
        {
          clutter_virtual_input_device_notify_discrete_scroll (device->device,
                                                               g_get_monotonic_time (),
                                                               CLUTTER_SCROLL_UP,
                                                               CLUTTER_SCROLL_SOURCE_WHEEL);
          dy++;
        }
    }
}

static void
handle_button (MetaEisClient    *client,
               struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  uint32_t button;
  gboolean is_press = eis_event_button_get_is_press (event);

  button = eis_event_button_get_button (event);
  button = meta_evdev_button_to_clutter (button);

  if (button > MAX_BUTTON)
    return;

  if (is_press && !bit_is_set (device->button_state, button))
    bit_set (device->button_state, button);
  else if (!is_press && bit_is_set (device->button_state, button))
    bit_clear (device->button_state, button);
  else
    return; /* Duplicate press/release, should've been filtered by libeis */

  notify_button (device,
                 button,
                 eis_event_button_get_is_press (event));
}

static void
handle_key (MetaEisClient    *client,
            struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  uint32_t key;
  gboolean is_press = eis_event_keyboard_get_key_is_press (event);

  key = eis_event_keyboard_get_key (event);

  if (key > MAX_KEY)
    return;

  if (is_press && !bit_is_set (device->key_state, key))
    bit_set (device->key_state, key);
  else if (!is_press && bit_is_set (device->key_state, key))
    bit_clear (device->key_state, key);
  else
    return; /* Duplicate press/release, should've been filtered by libeis */

  notify_key (device, key, is_press);
}

static int
acquire_slot (MetaEisDevice *device,
              uint32_t       eis_touch_id)
{
  int slot;

  for (slot = 0; slot < MAX_SLOTS; slot++)
    {
      if (device->slots[slot].is_acquired)
        continue;

      device->slots[slot].is_acquired = TRUE;
      device->slots[slot].eis_touch_id = eis_touch_id;
      g_hash_table_insert (device->slot_map,
                           GUINT_TO_POINTER (eis_touch_id),
                           GINT_TO_POINTER (slot));
      return slot;
    }

  return -1;
}

static int
get_slot (MetaEisDevice *device,
          uint32_t       touch_id)
{
  return GPOINTER_TO_INT (g_hash_table_lookup (device->slot_map,
                                               GUINT_TO_POINTER (touch_id)));
}

static void
release_slot (MetaEisDevice *device,
              int            slot)
{
  g_assert (device->slots[slot].is_acquired);

  device->slots[slot].is_acquired = FALSE;
  g_hash_table_remove (device->slot_map,
                       GUINT_TO_POINTER (device->slots[slot].eis_touch_id));
  device->slots[slot].eis_touch_id = 0;
}

static void
handle_touch_down (MetaEisClient    *client,
                   struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  MetaEisViewport *viewport;
  double x, y;
  int slot;

  x = eis_event_touch_get_x (event);
  y = eis_event_touch_get_y (event);

  viewport = find_viewport (device, x, y);
  if (!viewport)
    return;

  if (!meta_eis_viewport_transform_coordinate (viewport, x, y, &x, &y))
    return;

  slot = acquire_slot (device, eis_event_touch_get_id (event));
  clutter_virtual_input_device_notify_touch_down (device->device,
                                                  g_get_monotonic_time (),
                                                  slot,
                                                  x, y);
}

static void
handle_touch_motion (MetaEisClient    *client,
                     struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  MetaEisViewport *viewport;
  double x, y;
  int slot;

  x = eis_event_touch_get_x (event);
  y = eis_event_touch_get_y (event);

  viewport = find_viewport (device, x, y);
  if (!viewport)
    return;

  if (!meta_eis_viewport_transform_coordinate (viewport, x, y, &x, &y))
    return;

  slot = get_slot (device, eis_event_touch_get_id (event));

  clutter_virtual_input_device_notify_touch_motion (device->device,
                                                    g_get_monotonic_time (),
                                                    slot,
                                                    x, y);
}

static void
handle_touch_up (MetaEisClient    *client,
                 struct eis_event *event)
{
  struct eis_device *eis_device = eis_event_get_device (event);
  MetaEisDevice *device = eis_device_get_user_data (eis_device);
  int slot;

  slot = get_slot (device, eis_event_touch_get_id (event));
  release_slot (device, slot);

  clutter_virtual_input_device_notify_touch_up (device->device,
                                                g_get_monotonic_time (),
                                                slot);
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer     data)
{
  MetaEisClient *client = data;

  /* Changing the keymap means we have to remove our device and recreate it
   * with the new keymap.
   */

  meta_topic (META_DEBUG_EIS,
              "Recreating keyboard device with new keyboard");

  eis_device_remove (client->keyboard_device->eis_device);

  client->keyboard_device = add_device (client,
                                        client->eis_seat,
                                        CLUTTER_KEYBOARD_DEVICE,
                                        "virtual keyboard",
                                        configure_keyboard, NULL);
}

static void
on_keymap_state_changed (ClutterKeymap *keymap,
                         MetaEisClient *client)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
  xkb_layout_index_t group;

  clutter_keymap_get_modifier_state (keymap,
                                     &depressed_mods,
                                     &latched_mods,
                                     &locked_mods);
  group = clutter_keymap_get_layout_index (keymap);

  eis_device_keyboard_send_xkb_modifiers (client->keyboard_device->eis_device,
                                          depressed_mods,
                                          latched_mods,
                                          locked_mods,
                                          group);
}

static void
add_viewport_devices (MetaEisClient           *client,
                      ClutterInputDeviceType   type,
                      const char              *name_suffix,
                      MetaEisDeviceConfigFunc  extra_config_func)
{
  MetaEisDevice *shared_device = NULL;
  GList *viewports;
  GList *l;

  g_return_if_fail (eis_seat_has_capability (client->eis_seat,
                                             EIS_DEVICE_CAP_POINTER_ABSOLUTE));

  viewports = meta_eis_peek_viewports (client->eis);
  if (!viewports)
    return; /* FIXME: should be an error */

  for (l = viewports; l; l = l->next)
    {
      MetaEisViewport *viewport = l->data;

      if (meta_eis_viewport_is_standalone (viewport))
        {
          MetaEisDevice *device;
          g_autofree char *name = NULL;

          name = g_strdup_printf ("standalone %s", name_suffix);
          device = add_device (client,
                               client->eis_seat,
                               type,
                               name,
                               extra_config_func,
                               viewport);
          device->viewport = viewport;
        }
      else
        {
          if (!shared_device)
            {
              g_autofree char *name = NULL;

              name = g_strdup_printf ("shared %s", name_suffix);
              shared_device = create_device (client,
                                             client->eis_seat,
                                             type,
                                             name,
                                             extra_config_func,
                                             viewport);
            }
          else
            {
              add_viewport_region (shared_device->eis_device, viewport);
            }
        }
    }

  if (shared_device)
    propagate_device (shared_device);
}

static void
add_abs_pointer_devices (MetaEisClient *client)
{
  add_viewport_devices (client,
                        CLUTTER_POINTER_DEVICE,
                        "virtual absolute pointer",
                        configure_abs);
}

static void
add_touch_devices (MetaEisClient *client)
{
  add_viewport_devices (client,
                        CLUTTER_TOUCHSCREEN_DEVICE,
                        "virtual touch screen",
                        configure_touch);
}

gboolean
meta_eis_client_process_event (MetaEisClient    *client,
                               struct eis_event *event)
{
  enum eis_event_type type = eis_event_get_type (event);

  meta_topic (META_DEBUG_EIS,
              "Processing %s event", eis_event_type_to_string (type));

  switch (type)
    {
    case EIS_EVENT_SEAT_BIND:
      {
        struct eis_seat *eis_seat;
        gboolean wants_pointer_device;
        gboolean wants_keyboard_device;
        gboolean wants_abs_pointer_devices;
        gboolean wants_touch_devices;

        eis_seat = eis_event_get_seat (event);

        wants_pointer_device =
          eis_event_seat_has_capability (event, EIS_DEVICE_CAP_POINTER);
        wants_keyboard_device =
          eis_event_seat_has_capability (event, EIS_DEVICE_CAP_KEYBOARD);
        wants_abs_pointer_devices =
          eis_event_seat_has_capability (event, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
        wants_touch_devices =
          eis_event_seat_has_capability (event, EIS_DEVICE_CAP_TOUCH);

        if (wants_pointer_device && !client->pointer_device)
          {
            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, creating pointer device",
                        eis_seat_get_name (eis_seat));
            client->pointer_device = add_device (client,
                                                 eis_seat,
                                                 CLUTTER_POINTER_DEVICE,
                                                 "virtual pointer",
                                                 configure_rel, NULL);
          }
        else if (!wants_pointer_device && client->pointer_device)
          {
            MetaEisDevice *pointer;

            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, destroying pointer device",
                        eis_seat_get_name (eis_seat));
            pointer = g_steal_pointer (&client->pointer_device);
            eis_device_remove (pointer->eis_device);
          }

        if (wants_keyboard_device && !client->keyboard_device)
          {
            MetaBackend *backend = meta_eis_get_backend (client->eis);
            ClutterSeat *seat = meta_backend_get_default_seat (backend);
            ClutterKeymap *keymap = clutter_seat_get_keymap (seat);

            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, creating keyboard device",
                        eis_seat_get_name (eis_seat));

            client->keyboard_device = add_device (client,
                                                  eis_seat,
                                                  CLUTTER_KEYBOARD_DEVICE,
                                                  "virtual keyboard",
                                                  configure_keyboard, NULL);

            client->keymap_changed_handler_id =
              g_signal_connect (backend, "keymap-changed",
                                G_CALLBACK (on_keymap_changed),
                                client);
            client->keymap_state_changed_handler_id =
              g_signal_connect (keymap, "state-changed",
                                G_CALLBACK (on_keymap_state_changed),
                                client);
          }
        else if (!wants_keyboard_device && client->keyboard_device)
          {
            MetaBackend *backend = meta_eis_get_backend (client->eis);
            ClutterSeat *seat = meta_backend_get_default_seat (backend);
            ClutterKeymap *keymap = clutter_seat_get_keymap (seat);
            MetaEisDevice *keyboard;

            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, destroying keyboard device",
                        eis_seat_get_name (eis_seat));

            keyboard = g_steal_pointer (&client->keyboard_device);

            eis_device_remove (keyboard->eis_device);
            g_clear_signal_handler (&client->keymap_changed_handler_id,
                                    backend);
            g_clear_signal_handler (&client->keymap_state_changed_handler_id,
                                    keymap);
          }

        if (wants_abs_pointer_devices && !client->have_abs_pointer_devices)
          {
            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, enabling absolute pointer devices",
                        eis_seat_get_name (eis_seat));

            add_abs_pointer_devices (client);
            client->have_abs_pointer_devices = TRUE;
          }
        else if (!wants_abs_pointer_devices && client->have_abs_pointer_devices)
          {
            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, destroying absolute pointer devices",
                        eis_seat_get_name (eis_seat));

            g_hash_table_foreach (client->eis_devices,
                                  remove_abs_devices,
                                  client);
            client->have_abs_pointer_devices = FALSE;
          }

        if (wants_touch_devices && !client->have_touch_devices)
          {
            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, enabling touch devices",
                        eis_seat_get_name (eis_seat));
            add_touch_devices (client);
            client->have_touch_devices = TRUE;
          }
        else if (!wants_touch_devices && client->have_touch_devices)
          {
            meta_topic (META_DEBUG_EIS,
                        "Seat %s bindings updated, destroying touch devices",
                        eis_seat_get_name (eis_seat));

            g_hash_table_foreach (client->eis_devices,
                                  remove_touch_devices,
                                  client);
            client->have_touch_devices = FALSE;
          }
        break;
      }
    case EIS_EVENT_DEVICE_CLOSED:
      {
        struct eis_device *eis_device = eis_event_get_device (event);
        MetaEisDevice *device = eis_device_get_user_data (eis_device);

        if (client->pointer_device == device)
          {
            client->pointer_device = NULL;
          }
        else if (client->keyboard_device == device)
          {
            MetaBackend *backend = meta_eis_get_backend (client->eis);
            ClutterSeat *seat = meta_backend_get_default_seat (backend);
            ClutterKeymap *keymap = clutter_seat_get_keymap (seat);

            client->keyboard_device = NULL;

            g_clear_signal_handler (&client->keymap_changed_handler_id,
                                    backend);
            g_clear_signal_handler (&client->keymap_state_changed_handler_id,
                                    keymap);
          }

        remove_device (client, eis_device, TRUE);

        break;
      }
    case EIS_EVENT_POINTER_MOTION:
      handle_motion_relative (client, event);
      break;
    case EIS_EVENT_POINTER_MOTION_ABSOLUTE:
      handle_motion_absolute (client, event);
      break;
    case EIS_EVENT_BUTTON_BUTTON:
      handle_button (client, event);
      break;
    case EIS_EVENT_SCROLL_DELTA:
      handle_scroll (client, event);
      break;
    case EIS_EVENT_SCROLL_STOP:
      handle_scroll_stop (client, event);
      break;
    case EIS_EVENT_SCROLL_CANCEL:
      handle_scroll_cancel (client, event);
      break;
    case EIS_EVENT_SCROLL_DISCRETE:
      handle_scroll_discrete (client, event);
      break;
    case EIS_EVENT_KEYBOARD_KEY:
      handle_key (client, event);
      break;
    case EIS_EVENT_TOUCH_DOWN:
      handle_touch_down (client, event);
      break;
    case EIS_EVENT_TOUCH_MOTION:
      handle_touch_motion (client, event);
      break;
    case EIS_EVENT_TOUCH_UP:
      handle_touch_up (client, event);
      break;
    case EIS_EVENT_FRAME:
      /* FIXME: we should be accumulating the above events */
      break;
    case EIS_EVENT_DEVICE_START_EMULATING:
      break;
    case EIS_EVENT_DEVICE_STOP_EMULATING:
      break;
    default:
      meta_topic (META_DEBUG_EIS, "Unhandled EIS event type %d", type);
      return FALSE;
    }

  return TRUE;
}

static void
update_viewports (MetaEisClient *client)
{
  meta_topic (META_DEBUG_EIS, "Updating viewports");

  g_hash_table_foreach (client->eis_devices,
                        remove_viewport_devices,
                        client);

  if (client->have_abs_pointer_devices)
    add_abs_pointer_devices (client);

  if (client->have_touch_devices)
    add_touch_devices (client);
}

static void
on_viewports_changed (MetaEis       *eis,
                      MetaEisClient *client)
{
  update_viewports (client);
}

static void
meta_eis_client_disconnect (MetaEisClient *client)
{
  g_clear_signal_handler (&client->viewports_changed_handler_id, client->eis);
  g_hash_table_foreach_remove (client->eis_devices, drop_device, client);
  g_clear_pointer (&client->eis_seat, eis_seat_unref);
  if (client->eis_client)
    eis_client_disconnect (client->eis_client);
  g_clear_pointer (&client->eis_client, eis_client_unref);
}

MetaEisClient *
meta_eis_client_new (MetaEis           *eis,
                     struct eis_client *eis_client)
{
  MetaEisClient *client;
  struct eis_seat *eis_seat;

  client = g_object_new (META_TYPE_EIS_CLIENT, NULL);
  client->eis = eis;
  client->eis_client = eis_client_ref (eis_client);
  eis_client_set_user_data (client->eis_client, client);

  /* We're relying on some third party to filter clients for us */
  eis_client_connect (eis_client);

  /* We only support one seat for now. libeis keeps the ref for the
   * seat and we don't need to care about it.
   * The capabilities we define are the maximum capabilities, the client
   * may only bind to a subset of those, reducing the capabilities
   * of the seat in the process.
   */
  eis_seat = eis_client_new_seat (eis_client, "mutter default seat");

  if (meta_eis_get_device_types (eis) & META_EIS_DEVICE_TYPE_KEYBOARD)
    eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_KEYBOARD);

  if (meta_eis_get_device_types (eis) & META_EIS_DEVICE_TYPE_POINTER)
    {
      eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_POINTER);
      eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
      eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_BUTTON);
      eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_SCROLL);
    }

  if (meta_eis_get_device_types (eis) & META_EIS_DEVICE_TYPE_TOUCHSCREEN)
    eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_TOUCH);

  eis_seat_add (eis_seat);
  eis_seat_unref (eis_seat);
  client->eis_seat = eis_seat_ref (eis_seat);

  client->eis_devices = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                               (GDestroyNotify) eis_device_unref,
                                               (GDestroyNotify) meta_eis_device_free);

  client->viewports_changed_handler_id =
    g_signal_connect (eis, "viewports-changed",
                      G_CALLBACK (on_viewports_changed),
                      client);

  return client;
}

static void
meta_eis_client_init (MetaEisClient *client)
{
}

static void
meta_eis_client_finalize (GObject *object)
{
  MetaEisClient *client = META_EIS_CLIENT (object);
  MetaBackend *backend = meta_eis_get_backend (client->eis);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterKeymap *keymap = clutter_seat_get_keymap (seat);

  g_clear_signal_handler (&client->keymap_changed_handler_id, backend);
  g_clear_signal_handler (&client->keymap_state_changed_handler_id, keymap);
  meta_eis_client_disconnect (client);

  G_OBJECT_CLASS (meta_eis_client_parent_class)->finalize (object);
}

static void
meta_eis_client_class_init (MetaEisClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_eis_client_finalize;
}
