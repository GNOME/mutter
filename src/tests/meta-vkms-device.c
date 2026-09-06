/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include "tests/meta-vkms-device.h"

#include <gio/gio.h>
#include <sys/stat.h>

#define VKMS_CONFIGFS_BASE "/sys/kernel/config/vkms"

struct _MetaVkmsDevice
{
  const MetaVkmsDeviceConfig *config; /* (unowned) */
  gboolean enabled;
};

const MetaVkmsDeviceConfig meta_vkms_default_device_config = {
  .name = "mutter-test",
  .crtcs = {
    { .name = "crtc0", .writeback = TRUE },
  },
  .planes = {
    {
      .name = "primary",
      .type = META_VKMS_PLANE_TYPE_PRIMARY,
      .possible_crtcs = { "crtc0" },
    },
    {
      .name = "cursor",
      .type = META_VKMS_PLANE_TYPE_CURSOR,
      .possible_crtcs = { "crtc0" },
    },
  },
  .encoders = {
    { .name = "encoder0", .possible_crtcs = { "crtc0" } },
  },
  .connectors = {
    { .name = "connector0", .possible_encoders = { "encoder0" } },
  },
};

static gboolean
write_configfs_file (const char  *path,
                     const char  *value,
                     GError     **error)
{
  return g_file_set_contents_full (path, value, -1,
                                   G_FILE_SET_CONTENTS_NONE, 0644,
                                   error);
}

static gboolean
unlink_symlinks_recursively (GFile   *dir,
                             GError **error)
{
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator =
    g_file_enumerate_children (dir,
                               G_FILE_ATTRIBUTE_STANDARD_NAME ","
                               G_FILE_ATTRIBUTE_STANDARD_TYPE,
                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                               NULL, NULL);
  if (!enumerator)
    return TRUE;

  for (;;)
    {
      GFileInfo *info;
      GFile *child;

      if (!g_file_enumerator_iterate (enumerator, &info, &child, NULL, error))
        return FALSE;
      if (!info)
        break;

      switch (g_file_info_get_file_type (info))
        {
        case G_FILE_TYPE_SYMBOLIC_LINK:
          if (!g_file_delete (child, NULL, error))
            return FALSE;
          break;
        case G_FILE_TYPE_DIRECTORY:
          if (!unlink_symlinks_recursively (child, error))
            return FALSE;
          break;
        default:
          break;
        }
    }

  return TRUE;
}

static gboolean
remove_configfs_dirs (GFile   *dir,
                      GError **error)
{
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator =
    g_file_enumerate_children (dir,
                               G_FILE_ATTRIBUTE_STANDARD_NAME ","
                               G_FILE_ATTRIBUTE_STANDARD_TYPE,
                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                               NULL, NULL);
  if (!enumerator)
    return TRUE;

  for (;;)
    {
      GFileInfo *info;
      GFile *child;

      if (!g_file_enumerator_iterate (enumerator, &info, &child, NULL, error))
        return FALSE;
      if (!info)
        break;

      if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
        continue;

      if (!remove_configfs_dirs (child, error))
        return FALSE;
    }

  {
    g_autoptr (GError) local_error = NULL;

    if (!g_file_delete (dir, NULL, &local_error))
      {
        if (g_error_matches (local_error,
                             G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
          return TRUE;

        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
      }
  }

  return TRUE;
}

static gboolean
teardown_configfs_device (const char  *name,
                          GError     **error)
{
  g_autofree char *dev_path = NULL;
  g_autofree char *enabled_path = NULL;
  g_autoptr (GFile) dev_file = NULL;

  dev_path = g_build_filename (VKMS_CONFIGFS_BASE, name, NULL);
  enabled_path = g_build_filename (dev_path, "enabled", NULL);

  write_configfs_file (enabled_path, "0", NULL);

  dev_file = g_file_new_for_path (dev_path);

  /* Symlinks must go first: ConfigFS returns EBUSY on items still referenced.
   * Kernel-managed dirs return EPERM on rmdir and are cleaned up automatically
   * when their parent config item is removed. */

  if (!unlink_symlinks_recursively (dev_file, error))
    return FALSE;

  if (!remove_configfs_dirs (dev_file, error))
    return FALSE;

  return TRUE;
}

MetaVkmsDevice *
meta_vkms_device_new (const MetaVkmsDeviceConfig *config)
{
  MetaVkmsDevice *device;

  device = g_new0 (MetaVkmsDevice, 1);
  device->config = config;

  return device;
}

static const char *
plane_type_to_string (MetaVkmsPlaneType type)
{
  switch (type)
    {
    case META_VKMS_PLANE_TYPE_PRIMARY:
      return "1";
    case META_VKMS_PLANE_TYPE_CURSOR:
      return "2";
    case META_VKMS_PLANE_TYPE_OVERLAY:
      return "0";
    }

  g_assert_not_reached ();
}

gboolean
meta_vkms_device_enable (MetaVkmsDevice  *device,
                         GError         **error)
{
  const MetaVkmsDeviceConfig *config = device->config;
  g_autofree char *dev_path = NULL;
  g_autofree char *enabled_path = NULL;

  g_return_val_if_fail (!device->enabled, FALSE);

  dev_path = g_build_filename (VKMS_CONFIGFS_BASE, config->name, NULL);

  teardown_configfs_device (config->name, NULL);

  if (mkdir (dev_path, 0755) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to create VKMS device '%s': %s",
                   config->name, g_strerror (errno));
      return FALSE;
    }

  for (size_t i = 0; config->crtcs[i].name; i++)
    {
      g_autofree char *crtc_path = NULL;

      crtc_path = g_build_filename (dev_path, "crtcs",
                                    config->crtcs[i].name, NULL);

      if (mkdir (crtc_path, 0755) < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to create CRTC '%s': %s",
                       config->crtcs[i].name, g_strerror (errno));
          return FALSE;
        }

      if (config->crtcs[i].writeback)
        {
          g_autofree char *wb_path = NULL;

          wb_path = g_build_filename (crtc_path, "writeback", NULL);
          if (!write_configfs_file (wb_path, "1", error))
            return FALSE;
        }
    }

  for (size_t i = 0; config->planes[i].name; i++)
    {
      g_autofree char *plane_path = NULL;
      g_autofree char *type_path = NULL;

      plane_path = g_build_filename (dev_path, "planes",
                                     config->planes[i].name, NULL);
      type_path = g_build_filename (plane_path, "type", NULL);

      if (mkdir (plane_path, 0755) < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to create plane '%s': %s",
                       config->planes[i].name, g_strerror (errno));
          return FALSE;
        }

      if (!write_configfs_file (type_path,
                                plane_type_to_string (config->planes[i].type),
                                error))
        return FALSE;

      for (size_t j = 0; config->planes[i].possible_crtcs[j]; j++)
        {
          const char *crtc_name = config->planes[i].possible_crtcs[j];
          g_autofree char *crtc_target = NULL;
          g_autofree char *link_path = NULL;

          crtc_target = g_build_filename (dev_path, "crtcs", crtc_name, NULL);
          link_path = g_build_filename (plane_path, "possible_crtcs",
                                        crtc_name, NULL);

          if (symlink (crtc_target, link_path) < 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to link plane '%s' to CRTC '%s': %s",
                           config->planes[i].name, crtc_name,
                           g_strerror (errno));
              return FALSE;
            }
        }
    }

  for (size_t i = 0; config->encoders[i].name; i++)
    {
      g_autofree char *encoder_path = NULL;

      encoder_path = g_build_filename (dev_path, "encoders",
                                       config->encoders[i].name, NULL);

      if (mkdir (encoder_path, 0755) < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to create encoder '%s': %s",
                       config->encoders[i].name, g_strerror (errno));
          return FALSE;
        }

      for (size_t j = 0; config->encoders[i].possible_crtcs[j]; j++)
        {
          const char *crtc_name = config->encoders[i].possible_crtcs[j];
          g_autofree char *crtc_target = NULL;
          g_autofree char *link_path = NULL;

          crtc_target = g_build_filename (dev_path, "crtcs", crtc_name, NULL);
          link_path = g_build_filename (encoder_path, "possible_crtcs",
                                        crtc_name, NULL);

          if (symlink (crtc_target, link_path) < 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to link encoder '%s' to CRTC '%s': %s",
                           config->encoders[i].name, crtc_name,
                           g_strerror (errno));
              return FALSE;
            }
        }
    }

  for (size_t i = 0; config->connectors[i].name; i++)
    {
      g_autofree char *connector_path = NULL;

      connector_path = g_build_filename (dev_path, "connectors",
                                         config->connectors[i].name, NULL);

      if (mkdir (connector_path, 0755) < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to create connector '%s': %s",
                       config->connectors[i].name, g_strerror (errno));
          return FALSE;
        }

      for (size_t j = 0; config->connectors[i].possible_encoders[j]; j++)
        {
          const char *encoder_name = config->connectors[i].possible_encoders[j];
          g_autofree char *encoder_target = NULL;
          g_autofree char *link_path = NULL;

          encoder_target = g_build_filename (dev_path, "encoders",
                                             encoder_name, NULL);
          link_path = g_build_filename (connector_path, "possible_encoders",
                                        encoder_name, NULL);

          if (symlink (encoder_target, link_path) < 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to link connector '%s' to encoder '%s': %s",
                           config->connectors[i].name, encoder_name,
                           g_strerror (errno));
              return FALSE;
            }
        }
    }

  enabled_path = g_build_filename (dev_path, "enabled", NULL);
  if (!write_configfs_file (enabled_path, "1", error))
    return FALSE;

  device->enabled = TRUE;
  return TRUE;
}

gboolean
meta_vkms_device_disable (MetaVkmsDevice  *device,
                          GError         **error)
{
  if (!device->enabled)
    return TRUE;

  if (!teardown_configfs_device (device->config->name, error))
    return FALSE;

  device->enabled = FALSE;
  return TRUE;
}

void
meta_vkms_device_free (MetaVkmsDevice *device)
{
  meta_vkms_device_disable (device, NULL);
  g_free (device);
}
