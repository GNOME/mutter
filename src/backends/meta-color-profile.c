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
  MetaColorCalibration *calibration;

  char *cd_profile_id;
  gboolean is_owner;
  CdProfile *cd_profile;
  GCancellable *cancellable;
  guint notify_ready_id;

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

  g_cancellable_cancel (color_profile->cancellable);
  g_clear_object (&color_profile->cancellable);
  g_clear_handle_id (&color_profile->notify_ready_id, g_source_remove);

  if (color_profile->is_owner)
    {
      CdProfile *cd_profile;

      cd_profile = color_profile->cd_profile;
      if (!cd_profile && !color_profile->is_ready)
        {
          g_autoptr (GError) error = NULL;

          cd_profile = find_profile_sync (cd_client,
                                          color_profile->cd_profile_id,
                                          &error);
          if (!cd_profile &&
              !g_error_matches (error,
                                CD_CLIENT_ERROR,
                                CD_CLIENT_ERROR_NOT_FOUND))
            {
              g_warning ("Failed to find colord profile %s: %s",
                         color_profile->cd_profile_id,
                         error->message);
            }
        }

      if (cd_profile)
        cd_client_delete_profile (cd_client, cd_profile, NULL, NULL, NULL);
    }

  g_clear_pointer (&color_profile->cd_profile_id, g_free);
  g_clear_object (&color_profile->cd_icc);
  g_clear_pointer (&color_profile->bytes, g_bytes_unref);
  g_clear_object (&color_profile->cd_profile);
  g_clear_pointer (&color_profile->calibration, meta_color_calibration_free);

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
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
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

      color_profile->is_ready = TRUE;
      g_signal_emit (color_profile, signals[READY], 0, FALSE);
      return;
    }

  g_warn_if_fail (g_strcmp0 (cd_profile_get_id (cd_profile),
                             color_profile->cd_profile_id) == 0);

  meta_topic (META_DEBUG_COLOR, "Color profile '%s' connected",
              color_profile->cd_profile_id);

  color_profile->is_ready = TRUE;
  g_signal_emit (color_profile, signals[READY], 0, TRUE);
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

      if (g_error_matches (error,
                           CD_CLIENT_ERROR, CD_CLIENT_ERROR_ALREADY_EXISTS))
        {
          meta_topic (META_DEBUG_COLOR, "Tried to create duplicate profile %s",
                      color_profile->cd_profile_id);
        }
      else
        {
          g_warning ("Failed to create colord color profile %s: %s",
                     color_profile->cd_profile_id,
                     error->message);
        }

      color_profile->is_ready = TRUE;
      g_signal_emit (color_profile, signals[READY], 0, FALSE);
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
meta_color_profile_new_from_icc (MetaColorManager     *color_manager,
                                 CdIcc                *cd_icc,
                                 GBytes               *raw_bytes,
                                 MetaColorCalibration *color_calibration)
{
  MetaColorProfile *color_profile;
  const char *checksum;

  checksum = cd_icc_get_metadata_item (cd_icc,
                                       CD_PROFILE_METADATA_FILE_CHECKSUM);

  color_profile = g_object_new (META_TYPE_COLOR_PROFILE, NULL);
  color_profile->color_manager = color_manager;
  color_profile->cd_icc = cd_icc;
  color_profile->bytes = raw_bytes;
  color_profile->calibration = color_calibration;
  color_profile->cancellable = g_cancellable_new ();
  color_profile->is_owner = TRUE;

  color_profile->cd_profile_id = g_strdup_printf ("icc-%s", checksum);

  create_cd_profile (color_profile, checksum);

  return color_profile;
}

static void
notify_ready_idle (gpointer user_data)
{
  MetaColorProfile *color_profile = user_data;

  color_profile->notify_ready_id = 0;
  color_profile->is_ready = TRUE;
  g_signal_emit (color_profile, signals[READY], 0, TRUE);
}

MetaColorProfile *
meta_color_profile_new_from_cd_profile (MetaColorManager     *color_manager,
                                        CdProfile            *cd_profile,
                                        CdIcc                *cd_icc,
                                        GBytes               *raw_bytes,
                                        MetaColorCalibration *color_calibration)
{
  MetaColorProfile *color_profile;
  const char *checksum;

  color_profile = g_object_new (META_TYPE_COLOR_PROFILE, NULL);
  color_profile->color_manager = color_manager;
  color_profile->cd_icc = cd_icc;
  color_profile->bytes = raw_bytes;
  color_profile->calibration = color_calibration;
  color_profile->cancellable = g_cancellable_new ();
  color_profile->is_owner = FALSE;

  checksum = cd_icc_get_metadata_item (cd_icc,
                                       CD_PROFILE_METADATA_FILE_CHECKSUM);
  color_profile->cd_profile_id = g_strdup_printf ("icc-%s", checksum);
  color_profile->cd_profile = g_object_ref (cd_profile);

  color_profile->notify_ready_id = g_idle_add_once (notify_ready_idle,
                                                    color_profile);

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

CdProfile *
meta_color_profile_get_cd_profile (MetaColorProfile *color_profile)
{
  return color_profile->cd_profile;
}

gboolean
meta_color_profile_is_ready (MetaColorProfile *color_profile)
{
  return color_profile->is_ready;
}

const char *
meta_color_profile_get_id (MetaColorProfile *color_profile)
{
  return color_profile->cd_profile_id;
}

const char *
meta_color_profile_get_file_path (MetaColorProfile *color_profile)
{
  return cd_icc_get_metadata_item (color_profile->cd_icc,
                                   CD_PROFILE_PROPERTY_FILENAME);
}

const char *
meta_color_profile_get_brightness_profile (MetaColorProfile *color_profile)
{
  return color_profile->calibration->brightness_profile;
}

static void
set_blackbody_color_for_temperature (CdColorRGB   *blackbody_color,
                                     unsigned int  temperature)
{
  if (!cd_color_get_blackbody_rgb_full (temperature,
                                        blackbody_color,
                                        CD_COLOR_BLACKBODY_FLAG_USE_PLANCKIAN))
    {
      g_warning ("Failed to get blackbody for %uK", temperature);
      cd_color_rgb_set (blackbody_color, 1.0, 1.0, 1.0);
    }
  else
    {
      meta_topic (META_DEBUG_COLOR,
                  "Using blackbody color from %uK: %.1f, %.1f, %.1f",
                  temperature,
                  blackbody_color->R,
                  blackbody_color->G,
                  blackbody_color->B);
    }
}

static MetaGammaLut *
generate_gamma_lut_from_vcgt (MetaColorProfile  *color_profile,
                              cmsToneCurve     **vcgt,
                              unsigned int       temperature,
                              size_t             lut_size)
{
  CdColorRGB blackbody_color;
  MetaGammaLut *lut;
  size_t i;

  meta_topic (META_DEBUG_COLOR,
              "Generating %zu sized GAMMA LUT using temperature %uK and VCGT",
              lut_size, temperature);

  set_blackbody_color_for_temperature (&blackbody_color, temperature);

  lut = g_new0 (MetaGammaLut, 1);
  lut->size = lut_size;
  lut->red = g_new0 (uint16_t, lut_size);
  lut->green = g_new0 (uint16_t, lut_size);
  lut->blue = g_new0 (uint16_t, lut_size);

  for (i = 0; i < lut_size; i++)
    {
      cmsFloat32Number in;

      in = (cmsFloat32Number) ((double) i / (double) (lut_size - 1));
      lut->red[i] =
        (uint16_t) (cmsEvalToneCurveFloat (vcgt[0], in) *
                    blackbody_color.R * (double) 0xffff);
      lut->green[i] =
        (uint16_t) (cmsEvalToneCurveFloat (vcgt[1], in) *
                    blackbody_color.G * (double) 0xffff);
      lut->blue[i] =
        (uint16_t) (cmsEvalToneCurveFloat (vcgt[2], in) *
                    blackbody_color.B * (gdouble) 0xffff);
    }

  return lut;
}

static MetaGammaLut *
generate_gamma_lut (MetaColorProfile *color_profile,
                    unsigned int      temperature,
                    size_t            lut_size)
{
  CdColorRGB blackbody_color;
  MetaGammaLut *lut;
  size_t i;

  meta_topic (META_DEBUG_COLOR,
              "Generating %zu sized GAMMA LUT using temperature %uK",
              lut_size, temperature);

  set_blackbody_color_for_temperature (&blackbody_color, temperature);

  lut = g_new0 (MetaGammaLut, 1);
  lut->size = lut_size;
  lut->red = g_new0 (uint16_t, lut_size);
  lut->green = g_new0 (uint16_t, lut_size);
  lut->blue = g_new0 (uint16_t, lut_size);

  for (i = 0; i < lut_size; i++)
    {
      uint16_t in;

      in = (i * 0xffff) / (lut->size - 1);
      lut->red[i] = (uint16_t) (in * blackbody_color.R);
      lut->green[i] = (uint16_t) (in * blackbody_color.G);
      lut->blue[i] = (uint16_t) (in * blackbody_color.B);
    }

  return lut;
}

MetaGammaLut *
meta_color_profile_generate_gamma_lut (MetaColorProfile *color_profile,
                                       unsigned int      temperature,
                                       size_t            lut_size)
{
  g_assert (lut_size > 0);

  if (color_profile->calibration->has_vcgt)
    {
      return generate_gamma_lut_from_vcgt (color_profile,
                                           color_profile->calibration->vcgt,
                                           temperature, lut_size);
    }
  else
    {
      return generate_gamma_lut (color_profile, temperature, lut_size);
    }
}

const MetaColorCalibration *
meta_color_profile_get_calibration (MetaColorProfile *color_profile)
{
  return color_profile->calibration;
}

MetaColorCalibration *
meta_color_calibration_new (CdIcc          *cd_icc,
                            const CdMat3x3 *adaptation_matrix)
{
  MetaColorCalibration *color_calibration;
  cmsHPROFILE lcms_profile;
  const cmsToneCurve **vcgt;
  const char *brightness_profile;

  color_calibration = g_new0 (MetaColorCalibration, 1);

  lcms_profile = cd_icc_get_handle (cd_icc);
  vcgt = cmsReadTag (lcms_profile, cmsSigVcgtTag);
  if (vcgt && vcgt[0])
    {
      color_calibration->has_vcgt = TRUE;
      color_calibration->vcgt[0] = cmsDupToneCurve (vcgt[0]);
      color_calibration->vcgt[1] = cmsDupToneCurve (vcgt[1]);
      color_calibration->vcgt[2] = cmsDupToneCurve (vcgt[2]);
    }

  brightness_profile =
    cd_icc_get_metadata_item (cd_icc, CD_PROFILE_METADATA_SCREEN_BRIGHTNESS);
  if (brightness_profile)
    color_calibration->brightness_profile = g_strdup (brightness_profile);

  if (adaptation_matrix)
    {
      color_calibration->has_adaptation_matrix = TRUE;
      color_calibration->adaptation_matrix = *adaptation_matrix;
    }

  return color_calibration;
}

void
meta_color_calibration_free (MetaColorCalibration *color_calibration)
{
  cmsFreeToneCurveTriple (color_calibration->vcgt);
  g_free (color_calibration->brightness_profile);
  g_free (color_calibration);
}
