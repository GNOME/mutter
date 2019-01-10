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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/native/meta-udev.h"

enum
{
  ADD,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaUdev
{
  GObject parent;

  GUdevClient *gudev_client;

  guint uevent_handler_id;
};

G_DEFINE_TYPE (MetaUdev, meta_udev, G_TYPE_OBJECT)

static void
on_uevent (GUdevClient *client,
           const char  *action,
           GUdevDevice *device,
           gpointer     user_data)
{
  MetaUdev *udev = META_UDEV (user_data);

  if (!g_udev_device_get_device_file (device))
    return;

  if (g_str_equal (action, "add"))
    g_signal_emit (udev, signals[ADD], 0, device);
}

GUdevClient *
meta_udev_get_gudev_client (MetaUdev *udev)
{
  return udev->gudev_client;
}

MetaUdev *
meta_udev_new (void)
{
  return g_object_new (META_TYPE_UDEV, NULL);
}

static void
meta_udev_finalize (GObject *object)
{
  MetaUdev *udev = META_UDEV (object);

  g_signal_handler_disconnect (udev->gudev_client, udev->uevent_handler_id);
  g_clear_object (&udev->gudev_client);

  G_OBJECT_CLASS (meta_udev_parent_class)->finalize (object);
}

static void
meta_udev_init (MetaUdev *udev)
{
  const char *subsystems[] = { "drm", NULL };

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

  signals[ADD] =
    g_signal_new ("add",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 1,
                  G_UDEV_TYPE_DEVICE);
}
