/*
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 NVIDIA CORPORATION
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
 */

#include "config.h"

#include "backends/x11/meta-color-manager-x11.h"

#include <colord.h>
#include <X11/Xatom.h>

#include "backends/meta-color-device.h"
#include "backends/meta-color-profile.h"
#include "backends/meta-monitor.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-output-xrandr.h"

struct _MetaColorManagerX11
{
  MetaColorManager parent;

  CdIcc *srgb_cd_icc;
  GBytes *srgb_icc_bytes;
};

G_DEFINE_TYPE (MetaColorManagerX11, meta_color_manager_x11,
               META_TYPE_COLOR_MANAGER)

/* see http://www.oyranos.org/wiki/index.php?title=ICC_Profiles_in_X_Specification_0.3 */
#define ICC_PROFILE_IN_X_VERSION_MAJOR 0
#define ICC_PROFILE_IN_X_VERSION_MINOR 3

static CdIcc *
ensure_srgb_profile (MetaColorManagerX11 *color_manager_x11)
{
  CdIcc *srgb_cd_icc;
  g_autoptr (GError) error = NULL;

  if (color_manager_x11->srgb_cd_icc)
    return color_manager_x11->srgb_cd_icc;

  srgb_cd_icc = cd_icc_new ();
  if (!cd_icc_create_default_full (srgb_cd_icc,
                                   CD_ICC_LOAD_FLAGS_PRIMARIES,
                                   &error))
    {
      g_warning_once ("Failed to create sRGB ICC profile: %s", error->message);
      return NULL;
    }

  color_manager_x11->srgb_cd_icc = srgb_cd_icc;
  return srgb_cd_icc;
}

static GBytes *
ensure_srgb_profile_bytes (MetaColorManagerX11 *color_manager_x11)
{
  CdIcc *srgb_cd_icc;
  g_autoptr (GError) error = NULL;
  GBytes *bytes;

  srgb_cd_icc = ensure_srgb_profile (color_manager_x11);
  if (!srgb_cd_icc)
    return NULL;

  bytes = cd_icc_save_data (srgb_cd_icc,
                            CD_ICC_SAVE_FLAGS_NONE,
                            &error);
  if (!bytes)
    {
      g_warning_once ("Failed to export sRGB ICC profile: %s", error->message);
      return NULL;
    }

  color_manager_x11->srgb_icc_bytes = bytes;
  return bytes;
}

static void
on_color_device_updated (MetaColorManager *color_manager,
                         MetaColorDevice  *color_device)
{
  MetaColorManagerX11 *color_manager_x11 =
    META_COLOR_MANAGER_X11 (color_manager);
  MetaBackend *backend = meta_color_manager_get_backend (color_manager);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend_x11);
  Window xroot = meta_backend_x11_get_root_xwindow (backend_x11);
  Atom icc_profile_atom;
  Atom icc_profile_version_atom;
  MetaMonitor *monitor;
  const uint8_t *profile_contents = NULL;
  size_t profile_size;

  monitor = meta_color_device_get_monitor (color_device);
  if (!meta_monitor_is_primary (monitor))
    return;

  if (meta_monitor_supports_color_transform (monitor))
    {
      GBytes *profile_bytes;

      /*
       * If the output supports color transforms, then
       * applications should use the standard sRGB color profile and
       * the window system will take care of converting colors to
       * match the output device's measured color profile.
       */
      profile_bytes = ensure_srgb_profile_bytes (color_manager_x11);
      if (!profile_bytes)
        g_warning_once ("Failed to generate sRGB ICC profile");
      else
        profile_contents = g_bytes_get_data (profile_bytes, &profile_size);
    }
  else
    {
      MetaColorProfile *color_profile;

      color_profile = meta_color_device_get_assigned_profile (color_device);
      if (color_profile)
        {
          profile_contents = meta_color_profile_get_data (color_profile);
          profile_size = meta_color_profile_get_data_size (color_profile);
        }
    }

  icc_profile_atom = XInternAtom (xdisplay, "_ICC_PROFILE", False);
  icc_profile_version_atom = XInternAtom (xdisplay,
                                          "_ICC_PROFILE_IN_X_VERSION", False);
  if (profile_contents)
    {
      unsigned int version_data;

      XChangeProperty (xdisplay, xroot,
                       icc_profile_atom,
                       XA_CARDINAL, 8,
                       PropModeReplace,
                       profile_contents, profile_size);

      version_data =
        ICC_PROFILE_IN_X_VERSION_MAJOR * 100 +
        ICC_PROFILE_IN_X_VERSION_MINOR * 1;

      XChangeProperty (xdisplay, xroot,
                       icc_profile_version_atom,
                       XA_CARDINAL, 8,
                       PropModeReplace,
                       (uint8_t *) &version_data, 1);
    }
  else
    {
      XDeleteProperty (xdisplay, xroot, icc_profile_atom);
      XDeleteProperty (xdisplay, xroot, icc_profile_version_atom);
    }
}

static void
meta_color_manager_x11_constructed (GObject *object)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);

  g_signal_connect (color_manager, "device-updated",
                    G_CALLBACK (on_color_device_updated), NULL);

  G_OBJECT_CLASS (meta_color_manager_x11_parent_class)->constructed (object);
}

static void
meta_color_manager_x11_finalize (GObject *object)
{
  MetaColorManagerX11 *color_manager_x11 = META_COLOR_MANAGER_X11 (object);

  g_clear_pointer (&color_manager_x11->srgb_icc_bytes, g_bytes_unref);
  g_clear_object (&color_manager_x11->srgb_cd_icc);

  G_OBJECT_CLASS (meta_color_manager_x11_parent_class)->finalize (object);
}

static void
meta_color_manager_x11_class_init (MetaColorManagerX11Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_color_manager_x11_constructed;
  object_class->finalize = meta_color_manager_x11_finalize;
}

static void
meta_color_manager_x11_init (MetaColorManagerX11 *color_manager_x11)
{
}
