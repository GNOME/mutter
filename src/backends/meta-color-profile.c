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

#include "backends/meta-color-manager-private.h"

enum
{
  READY,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaColorProfile
{
  GObject parent;

  MetaColorManager *color_manager;

  CdIcc *cd_icc;
  GBytes *bytes;

  char *cd_profile_id;
  CdProfile *cd_profile;
  GCancellable *cancellable;

  gboolean is_ready;
};

G_DEFINE_TYPE (MetaColorProfile, meta_color_profile,
               G_TYPE_OBJECT)

typedef struct
{
  GMainLoop *loop;
  CdProfile *cd_profile;
  GError *error;
} FindProfileData;

static void
on_find_profile (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  CdClient *cd_client = CD_CLIENT (source_object);
  FindProfileData *data = user_data;

  data->cd_profile = cd_client_find_profile_finish (cd_client, res,
                                                    &data->error);
  g_main_loop_quit (data->loop);
}

static CdProfile *
find_profile_sync (CdClient    *cd_client,
                   const char  *cd_profile_id,
                   GError     **error)
{
  g_autoptr (GMainContext) main_context = NULL;
  g_autoptr (GMainLoop) main_loop = NULL;
  FindProfileData data = {};

  main_context = g_main_context_new ();
  main_loop = g_main_loop_new (main_context, FALSE);
  g_main_context_push_thread_default (main_context);

  data = (FindProfileData) {
    .loop = main_loop,
  };
  cd_client_find_profile (cd_client, cd_profile_id, NULL,
                          on_find_profile,
                          &data);
  g_main_loop_run (main_loop);

  g_main_context_pop_thread_default (main_context);

  if (data.error)
    g_propagate_error (error, data.error);
  return data.cd_profile;
}

static void
meta_color_profile_finalize (GObject *object)
{
  MetaColorProfile *color_profile = META_COLOR_PROFILE (object);
  MetaColorManager *color_manager = color_profile->color_manager;
  CdClient *cd_client = meta_color_manager_get_cd_client (color_manager);
  CdProfile *cd_profile;

  g_cancellable_cancel (color_profile->cancellable);
  g_clear_object (&color_profile->cancellable);

  cd_profile = color_profile->cd_profile;
  if (!cd_profile)
    {
      g_autoptr (GError) error = NULL;

      cd_profile = find_profile_sync (cd_client,
                                      color_profile->cd_profile_id,
                                      &error);
      if (!cd_profile &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_warning ("Failed to find colord profile %s: %s",
                     color_profile->cd_profile_id,
                     error->message);
        }
    }

  if (cd_profile)
    cd_client_delete_profile (cd_client, cd_profile, NULL, NULL, NULL);

  g_clear_pointer (&color_profile->cd_profile_id, g_free);
  g_clear_object (&color_profile->cd_icc);
  g_clear_pointer (&color_profile->bytes, g_bytes_unref);
  g_clear_object (&color_profile->cd_profile);

  G_OBJECT_CLASS (meta_color_profile_parent_class)->finalize (object);
}

static void
meta_color_profile_class_init (MetaColorProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_color_profile_finalize;

  signals[READY] =
    g_signal_new ("ready",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_color_profile_init (MetaColorProfile *color_profile)
{
}

static void
on_cd_profile_connected (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  CdProfile *cd_profile = CD_PROFILE (source_object);
  MetaColorProfile *color_profile = user_data;
  g_autoptr (GError) error = NULL;

  if (!cd_profile_connect_finish (cd_profile, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to connect to colord profile %s: %s",
                 color_profile->cd_profile_id,
                 error->message);
    }
  else
    {
      g_warn_if_fail (g_strcmp0 (cd_profile_get_id (cd_profile),
                                 color_profile->cd_profile_id) == 0);

      meta_topic (META_DEBUG_COLOR, "Color profile '%s' connected",
                  color_profile->cd_profile_id);
    }

  color_profile->is_ready = TRUE;
  g_signal_emit (color_profile, signals[READY], 0);
}

static void
on_cd_profile_created (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  CdClient *cd_client = CD_CLIENT (source_object);
  MetaColorProfile *color_profile = META_COLOR_PROFILE (user_data);
  g_autoptr (GError) error = NULL;
  CdProfile *cd_profile;

  cd_profile = cd_client_create_profile_finish (cd_client, res, &error);
  if (!cd_profile)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create colord color profile: %s", error->message);

      color_profile->is_ready = TRUE;
      g_signal_emit (color_profile, signals[READY], 0);
      return;
    }

  meta_topic (META_DEBUG_COLOR, "Created colord color profile '%s'",
              color_profile->cd_profile_id);

  color_profile->cd_profile = cd_profile;

  cd_profile_connect (cd_profile, color_profile->cancellable,
                      on_cd_profile_connected, color_profile);
}

static void
create_cd_profile (MetaColorProfile *color_profile,
                   const char       *checksum)
{
  MetaColorManager *color_manager = color_profile->color_manager;
  CdClient *cd_client = meta_color_manager_get_cd_client (color_manager);
  const char *filename;
  g_autoptr (GHashTable) profile_props = NULL;

  filename = cd_icc_get_metadata_item (color_profile->cd_icc,
                                       CD_PROFILE_PROPERTY_FILENAME);

  profile_props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         NULL, NULL);
  g_hash_table_insert (profile_props,
                       (gpointer) CD_PROFILE_PROPERTY_FILENAME,
                       (gpointer) filename);
  g_hash_table_insert (profile_props,
                       (gpointer) CD_PROFILE_METADATA_FILE_CHECKSUM,
                       (gpointer) checksum);
  cd_client_create_profile (cd_client,
                            color_profile->cd_profile_id,
                            CD_OBJECT_SCOPE_TEMP,
                            profile_props,
                            color_profile->cancellable,
                            on_cd_profile_created,
                            color_profile);
}

MetaColorProfile *
meta_color_profile_new_from_icc (MetaColorManager *color_manager,
                                 CdIcc            *cd_icc,
                                 GBytes           *raw_bytes)
{
  MetaColorProfile *color_profile;
  const char *checksum;

  checksum = cd_icc_get_metadata_item (cd_icc,
                                       CD_PROFILE_METADATA_FILE_CHECKSUM);

  color_profile = g_object_new (META_TYPE_COLOR_PROFILE, NULL);
  color_profile->color_manager = color_manager;
  color_profile->cd_icc = cd_icc;
  color_profile->bytes = raw_bytes;
  color_profile->cancellable = g_cancellable_new ();

  color_profile->cd_profile_id = g_strdup_printf ("icc-%s", checksum);

  create_cd_profile (color_profile, checksum);

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

gboolean
meta_color_profile_is_ready (MetaColorProfile *color_profile)
{
  return color_profile->is_ready;
}
