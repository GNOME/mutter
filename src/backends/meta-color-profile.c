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

#include "backends/meta-color-profile.h"

#include <colord.h>
#include <gio/gio.h>

struct _MetaColorProfile
{
  GObject parent;

  MetaColorManager *color_manager;

  CdIcc *cd_icc;
  GBytes *bytes;
};

G_DEFINE_TYPE (MetaColorProfile, meta_color_profile,
               G_TYPE_OBJECT)

static void
meta_color_profile_finalize (GObject *object)
{
  MetaColorProfile *color_profile = META_COLOR_PROFILE (object);

  g_clear_object (&color_profile->cd_icc);
  g_clear_pointer (&color_profile->bytes, g_bytes_unref);

  G_OBJECT_CLASS (meta_color_profile_parent_class)->finalize (object);
}

static void
meta_color_profile_class_init (MetaColorProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_color_profile_finalize;
}

static void
meta_color_profile_init (MetaColorProfile *color_profile)
{
}

MetaColorProfile *
meta_color_profile_new_from_icc (MetaColorManager *color_manager,
                                 CdIcc            *cd_icc,
                                 GBytes           *raw_bytes)
{
  MetaColorProfile *color_profile;

  color_profile = g_object_new (META_TYPE_COLOR_PROFILE, NULL);
  color_profile->color_manager = color_manager;
  color_profile->cd_icc = cd_icc;
  color_profile->bytes = raw_bytes;

  return color_profile;
}

gboolean
meta_color_profile_equals_bytes (MetaColorProfile *color_profile,
                                 GBytes           *bytes)
{
  return g_bytes_equal (color_profile->bytes, bytes);
}

const uint8_t *
meta_color_profile_get_data (MetaColorProfile *color_profile)
{
  return g_bytes_get_data (color_profile->bytes, NULL);
}

size_t
meta_color_profile_get_data_size (MetaColorProfile *color_profile)
{
  return g_bytes_get_size (color_profile->bytes);
}

CdIcc *
meta_color_profile_get_cd_icc (MetaColorProfile *color_profile)
{
  return color_profile->cd_icc;
}
