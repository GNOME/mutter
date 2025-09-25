/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/meta-udev.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-launcher.h"

#define DRM_CARD_UDEV_DEVICE_TYPE "drm_minor"

enum
{
  /* drm */
  HOTPLUG,
  LEASE,
  DEVICE_ADDED,
  DEVICE_REMOVED,

  /* backlight */
  BACKLIGHT_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaUdev
{
  GObject parent;

  MetaBackend *backend;

  GUdevClient *gudev_client;

  gulong uevent_handler_id;
};

G_DEFINE_TYPE (MetaUdev, meta_udev, G_TYPE_OBJECT)

gboolean
meta_is_udev_device_platform_device (GUdevDevice *device)
{
  g_autoptr (GUdevDevice) platform_device = NULL;

  platform_device = g_udev_device_get_parent_with_subsystem (device,
                                                             "platform",
                                                             NULL);
  return !!platform_device;
}

gboolean
meta_is_udev_device_boot_vga (GUdevDevice *device)
{
  g_autoptr (GUdevDevice) pci_device = NULL;

  pci_device = g_udev_device_get_parent_with_subsystem (device, "pci", NULL);
  if (!pci_device)
    return FALSE;

  return g_udev_device_get_sysfs_attr_as_int (pci_device, "boot_vga") == 1;
}

static gboolean
meta_has_udev_device_tag (GUdevDevice *device,
                          const char  *tag)
{
  const char * const * tags;
  g_autoptr (GUdevDevice) platform_device = NULL;

  tags = g_udev_device_get_tags (device);
  if (tags && g_strv_contains (tags, tag))
    return TRUE;

  platform_device = g_udev_device_get_parent_with_subsystem (device,
                                                             "platform",
                                                             NULL);

  if (platform_device)
    return meta_has_udev_device_tag (platform_device, tag);
  else
    return FALSE;
}

gboolean
meta_is_udev_device_disable_modifiers (GUdevDevice *device)
{
  return meta_has_udev_device_tag (device,
                                   "mutter-device-disable-kms-modifiers");
}

gboolean
meta_is_udev_device_disable_vrr (GUdevDevice *device)
{
  return meta_has_udev_device_tag (device,
                                   "mutter-device-disable-vrr");
}

gboolean
meta_is_udev_device_ignore (GUdevDevice *device)
{
  return meta_has_udev_device_tag (device, "mutter-device-ignore");
}

gboolean
meta_is_udev_test_device (GUdevDevice *device)
{
  return g_strcmp0 (g_udev_device_get_property (device, "ID_PATH"),
                    "platform-vkms") == 0;
}

gboolean
meta_is_udev_device_preferred_primary (GUdevDevice *device)
{
  const char * const *tags;

  tags = g_udev_device_get_current_tags (device);
  if (!tags)
    return FALSE;

  return g_strv_contains (tags, "mutter-device-preferred-primary");
}

gboolean
meta_udev_is_drm_device (MetaUdev    *udev,
                         GUdevDevice *device)
{
  const char *device_type;

  /* Filter out devices that are not character device, like card0-VGA-1. */
  if (g_udev_device_get_device_type (device) != G_UDEV_DEVICE_TYPE_CHAR)
    return FALSE;

  device_type = g_udev_device_get_property (device, "DEVTYPE");
  if (g_strcmp0 (device_type, DRM_CARD_UDEV_DEVICE_TYPE) != 0)
    return FALSE;

  /* Skip devices that do not belong to our seat. */
  if (!meta_backend_is_headless (udev->backend))
    {
      MetaLauncher *launcher = meta_backend_get_launcher (udev->backend);
      const char *device_seat;
      const char *seat_id;

      g_return_val_if_fail (launcher, TRUE);

      device_seat = g_udev_device_get_property (device, "ID_SEAT");
      if (!device_seat)
        {
          /* When ID_SEAT is not set, it means seat0. */
          device_seat = "seat0";
        }

      seat_id = meta_launcher_get_seat_id (launcher);
      if (g_strcmp0 (seat_id, device_seat))
        return FALSE;
    }

  return TRUE;
}

GList *
meta_udev_list_drm_devices (MetaUdev            *udev,
                            MetaUdevDeviceType   device_type,
                            GError             **error)
{
  g_autoptr (GUdevEnumerator) enumerator = NULL;
  GList *devices;
  GList *l;

  enumerator = g_udev_enumerator_new (udev->gudev_client);

  switch (device_type)
    {
    case META_UDEV_DEVICE_TYPE_CARD:
      g_udev_enumerator_add_match_name (enumerator, "card*");
      g_udev_enumerator_add_match_tag (enumerator, "seat");
      break;
    case META_UDEV_DEVICE_TYPE_RENDER_NODE:
      g_udev_enumerator_add_match_name (enumerator, "render*");
      break;
    }

  /*
   * We need to explicitly match the subsystem for now.
   * https://bugzilla.gnome.org/show_bug.cgi?id=773224
   */
  g_udev_enumerator_add_match_subsystem (enumerator, "drm");

  devices = g_udev_enumerator_execute (enumerator);
  if (!devices)
    return NULL;

  for (l = devices; l;)
    {
      GUdevDevice *device = l->data;
      GList *l_next = l->next;

      if (!meta_udev_is_drm_device (udev, device))
        {
          g_object_unref (device);
          devices = g_list_delete_link (devices, l);
        }

      l = l_next;
    }

  return devices;
}

static GUdevDevice *
meta_udev_backlight_find_type (GList      *devices,
                               const char *type)
{
  GList *l;

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      const char *device_type;

      device_type = g_udev_device_get_sysfs_attr (device, "type");
      if (g_strcmp0 (device_type, type) == 0)
        return g_object_ref (device);
    }

  return NULL;
}

static GUdevDevice *
meta_udev_backlight_find_for_connector (GList      *devices,
                                        const char *connector_name)
{
  GList *l;
  g_autofree char *connector_suffix = g_strdup_printf ("-%s", connector_name);

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      g_autoptr (GUdevDevice) parent = NULL;
      const char *prop;

      /* Only look for raw backlight interfaces */
      prop = g_udev_device_get_sysfs_attr (device, "type");
      if (g_strcmp0 (prop, "raw") != 0)
        continue;

      parent = g_udev_device_get_parent (device);
      if (!parent)
        continue;

      /* Raw backlight interfaces registered by the drm driver will have the
       * drm-connector as their parent.
       */
      prop = g_udev_device_get_subsystem (parent);
      if (g_strcmp0 (prop, "drm") != 0)
        continue;

      /* The drm-connector name is in the form `card[n]-[connector-name]`, so
       * let's check that the suffix of it matches the connector name to make
       * sure this backlight belongs to the connector.
       */
      prop = g_udev_device_get_name (parent);
      if (!prop || !g_str_has_suffix (prop, connector_suffix))
        continue;

      /* Also make sure the connector is actually enabled */
      prop = g_udev_device_get_sysfs_attr (parent, "enabled");
      if (g_strcmp0 (prop, "enabled") != 0)
        continue;

      return g_object_ref (device);
    }

  return NULL;
}

GUdevDevice *
meta_udev_backlight_find (MetaUdev   *udev,
                          const char *connector_name,
                          gboolean    is_internal)
{
  g_autolist (GUdevDevice) devices = NULL;
  GUdevDevice *device;

  g_warn_if_fail (udev->gudev_client != NULL);

  devices = g_udev_client_query_by_subsystem (udev->gudev_client, "backlight");
  if (!devices)
    return NULL;

  if (is_internal)
    {
      /* For internal monitors, prefer the types firmware -> platform -> raw */
      device = meta_udev_backlight_find_type (devices, "firmware");
      if (device)
        return device;

      device = meta_udev_backlight_find_type (devices, "platform");
      if (device)
        return device;
    }

  /* Try to find a backlight interface matching the connector */
  device = meta_udev_backlight_find_for_connector (devices, connector_name);
  if (device)
    return device;

  /* For internal monitors, fall back to just picking the first raw backlight
   * interface if no other interface was found. */
  if (is_internal)
    return meta_udev_backlight_find_type (devices, "raw");

  return NULL;
}

static void
on_drm_uevent (GUdevClient *client,
               const char  *action,
               GUdevDevice *device,
               gpointer     user_data)
{
  MetaUdev *udev = META_UDEV (user_data);

  if (!g_udev_device_get_device_file (device))
    return;

  if (g_str_equal (action, "add"))
    g_signal_emit (udev, signals[DEVICE_ADDED], 0, device);
  else if (g_str_equal (action, "remove"))
    g_signal_emit (udev, signals[DEVICE_REMOVED], 0, device);

  if (g_udev_device_get_property_as_boolean (device, "HOTPLUG"))
    g_signal_emit (udev, signals[HOTPLUG], 0, device);

  if (g_udev_device_get_property_as_boolean (device, "LEASE"))
    g_signal_emit (udev, signals[LEASE], 0, device);
}

static void
on_backlight_uevent (GUdevClient *client,
                     const char  *action,
                     GUdevDevice *device,
                     gpointer     user_data)
{
  MetaUdev *udev = META_UDEV (user_data);

  if (g_str_equal (action, "change"))
    g_signal_emit (udev, signals[BACKLIGHT_CHANGED], 0, device);
}

static void
on_uevent (GUdevClient *client,
           const char  *action,
           GUdevDevice *device,
           gpointer     user_data)
{
  if (g_str_equal (g_udev_device_get_subsystem (device), "drm"))
    return on_drm_uevent (client, action, device, user_data);

  if (g_str_equal (g_udev_device_get_subsystem (device), "backlight"))
    return on_backlight_uevent (client, action, device, user_data);
}

MetaUdev *
meta_udev_new (MetaBackend *backend)
{
  MetaUdev *udev;

  udev = g_object_new (META_TYPE_UDEV, NULL);
  udev->backend = backend;

  return udev;
}

void
meta_udev_pause (MetaUdev *udev)
{
  g_signal_handler_block (udev->gudev_client, udev->uevent_handler_id);
}

void
meta_udev_resume (MetaUdev *udev)
{
  g_signal_handler_unblock (udev->gudev_client, udev->uevent_handler_id);
}

static void
meta_udev_finalize (GObject *object)
{
  MetaUdev *udev = META_UDEV (object);

  g_clear_signal_handler (&udev->uevent_handler_id, udev->gudev_client);
  g_clear_object (&udev->gudev_client);

  G_OBJECT_CLASS (meta_udev_parent_class)->finalize (object);
}

static void
meta_udev_init (MetaUdev *udev)
{
  const char *subsystems[] = { "drm", "backlight", NULL };

  udev->gudev_client = g_udev_client_new (subsystems);
  udev->uevent_handler_id = g_signal_connect (udev->gudev_client,
                                              "uevent",
                                              G_CALLBACK (on_uevent), udev);
}

static void
meta_udev_class_init (MetaUdevClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_udev_finalize;

  signals[HOTPLUG] =
    g_signal_new ("hotplug",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
  signals[LEASE] =
    g_signal_new ("lease",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
  signals[DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
  signals[DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
  signals[BACKLIGHT_CHANGED] =
    g_signal_new ("backlight-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
}
