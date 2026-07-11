/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Joan Torres <joan.torres@suse.com>
 */

#include "config.h"

#include <glib/gstdio.h>
#include <lcms2.h>
#include <sys/mman.h>

#include "clutter/clutter-color-state-icc.h"

#include "clutter/clutter-color-state-params.h"
#include "mtk-anonymous-file.h"

#define CHECKSUM_SIZE 16

typedef struct _ClutterColorStateIcc
{
  ClutterColorState parent;

  MtkAnonymousFile *file;
  GBytes *bytes;

  cmsHPROFILE *icc_profile;

  uint8_t checksum[CHECKSUM_SIZE];

  gboolean is_linear;
} ClutterColorStateIcc;

static ClutterColorState * clutter_color_state_icc_new_full (ClutterContext             *context,
                                                             const uint8_t              *icc_bytes,
                                                             uint32_t                    icc_length,
                                                             GError                    **error);

G_DEFINE_TYPE (ClutterColorStateIcc,
               clutter_color_state_icc,
               CLUTTER_TYPE_COLOR_STATE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cmsHPROFILE, cmsCloseProfile);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (cmsHTRANSFORM, cmsDeleteTransform);

const MtkAnonymousFile *
clutter_color_state_icc_get_file (ClutterColorStateIcc *color_state_icc)
{
  return color_state_icc->file;
}

gpointer
clutter_color_state_icc_get_profile (ClutterColorStateIcc *color_state_icc)
{
  return color_state_icc->icc_profile;
}

gboolean
clutter_color_state_icc_is_linearized (ClutterColorStateIcc *color_state_icc)
{
  return color_state_icc->is_linear;
}

static void
clutter_color_state_icc_finalize (GObject *object)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (object);

  g_clear_pointer (&color_state_icc->icc_profile, cmsCloseProfile);
  g_clear_pointer (&color_state_icc->file, mtk_anonymous_file_free);
  g_clear_pointer (&color_state_icc->bytes, g_bytes_unref);

  G_OBJECT_CLASS (clutter_color_state_icc_parent_class)->finalize (object);
}

static gboolean
clutter_color_state_icc_equals (ClutterColorState *color_state,
                                ClutterColorState *other_color_state)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);
  ClutterColorStateIcc *other_color_state_icc =
    CLUTTER_COLOR_STATE_ICC (other_color_state);

  return memcmp (color_state_icc->checksum,
                 other_color_state_icc->checksum,
                 CHECKSUM_SIZE) == 0 &&
         color_state_icc->is_linear == other_color_state_icc->is_linear;
}

static const ClutterLuminance *
clutter_color_state_icc_get_luminance (ClutterColorState *color_state)
{
  return clutter_luminance_get_default_sdr ();
}

static char *
clutter_color_state_icc_to_string (ClutterColorState *color_state)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);
  uint8_t *checksum = color_state_icc->checksum;
  g_autoptr (GString) hex_checksum = g_string_new (NULL);
  unsigned int id;
  int i;

  for (i = 0; i < CHECKSUM_SIZE; i++)
    g_string_append_printf (hex_checksum, "%02x", checksum[i]);

  id = clutter_color_state_get_id (color_state);

  return g_strdup_printf ("ClutterColorState %d (ICC checksum: %s%s)",
                          id,
                          hex_checksum->str,
                          color_state_icc->is_linear ? ", linear" : "");
}

static ClutterEncodingRequiredFormat
clutter_color_state_icc_required_format (ClutterColorState *color_state)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);

  return color_state_icc->is_linear ? CLUTTER_ENCODING_REQUIRED_FORMAT_FP16 :
                                      CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8;
}

/*
 * On ICC color_states the blending is done in linear.
 */
static ClutterColorState *
clutter_color_state_icc_get_blending (ClutterColorState *color_state,
                                      gboolean           force)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);
  ClutterColorState *blending_color_state;
  ClutterContext *context;
  g_autoptr (GError) error = NULL;
  size_t length;
  uint8_t *bytes;

  if (color_state_icc->is_linear)
    return g_object_ref (color_state);

  context = clutter_color_state_get_context (color_state);

  bytes = (uint8_t *) g_bytes_get_data (color_state_icc->bytes, &length);
  blending_color_state =
    clutter_color_state_icc_new_full (context,
                                      bytes,
                                      length,
                                      &error);
  if (!blending_color_state)
    {
      g_warning ("Couldn't get ICC blending color state: %s", error->message);
      return g_object_ref (color_state);
    }

  {
    ClutterColorStateIcc *blending_color_state_icc =
      CLUTTER_COLOR_STATE_ICC (blending_color_state);

    blending_color_state_icc->is_linear = TRUE;
  }

  return blending_color_state;
}

static guint
clutter_color_state_icc_hash (ClutterColorState *color_state)
{
  ClutterColorStateIcc *icc = CLUTTER_COLOR_STATE_ICC (color_state);
  guint32 parts[4];

  memcpy (parts, icc->checksum, CHECKSUM_SIZE);

  guint h = (parts[0] ^ parts[1] ^ parts[2] ^ parts[3]) * 31 +
            icc->is_linear;

  return h;
}

static void
clutter_color_state_icc_class_init (ClutterColorStateIccClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_icc_finalize;

  color_state_class->equals = clutter_color_state_icc_equals;
  color_state_class->to_string = clutter_color_state_icc_to_string;
  color_state_class->required_format = clutter_color_state_icc_required_format;
  color_state_class->get_blending = clutter_color_state_icc_get_blending;
  color_state_class->hash = clutter_color_state_icc_hash;
  color_state_class->get_luminance = clutter_color_state_icc_get_luminance;
}

static void
clutter_color_state_icc_init (ClutterColorStateIcc *color_state_icc)
{
}

static gboolean
get_icc_file (const uint8_t     *icc_bytes,
              uint32_t           icc_length,
              MtkAnonymousFile **icc_file,
              GError           **error)
{
  MtkAnonymousFile *file;

  file = mtk_anonymous_file_new ("icc-file", icc_length, icc_bytes);
  if (!file)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Couldn't create anonymous ICC file: %s",
                   g_strerror (errno));
      return FALSE;
    }

  *icc_file = file;

  return TRUE;
}

static gboolean
get_icc_profile (const uint8_t  *icc_bytes,
                 uint32_t        icc_length,
                 cmsHPROFILE   **icc_profile,
                 GError        **error)
{
  cmsColorSpaceSignature pcs;
  cmsColorSpaceSignature color_space;
  g_autoptr (cmsHPROFILE) profile = NULL;

  profile = cmsOpenProfileFromMem (icc_bytes, icc_length);
  if (!profile)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't parse ICC profile");
      return FALSE;
    }

  color_space = cmsGetColorSpace (profile);
  pcs = cmsGetPCS (profile);
  if (color_space != cmsSigRgbData || pcs != cmsSigXYZData)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "ICC profile unsupported");
      return FALSE;
    }

  *icc_profile = g_steal_pointer (&profile);

  return TRUE;
}

static gboolean
get_checksum (cmsHPROFILE  *icc_profile,
              uint8_t       checksum[CHECKSUM_SIZE],
              GError      **error)
{
  uint8_t checksum_zeros[CHECKSUM_SIZE] = { 0 };

  cmsGetHeaderProfileID (icc_profile, checksum);
  if (memcmp (checksum, checksum_zeros, CHECKSUM_SIZE) != 0)
    return TRUE;

  cmsMD5computeID (icc_profile);
  cmsGetHeaderProfileID (icc_profile, checksum);
  if (memcmp (checksum, checksum_zeros, CHECKSUM_SIZE) != 0)
    return TRUE;

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Failed getting checksum from ICC profile");

  return FALSE;
}

static ClutterColorState *
clutter_color_state_icc_new_full (ClutterContext             *context,
                                  const uint8_t              *icc_bytes,
                                  uint32_t                    icc_length,
                                  GError                    **error)
{
  ClutterColorStateIcc *color_state_icc;
  g_autoptr (MtkAnonymousFile) icc_file = NULL;
  g_autoptr (cmsHPROFILE) icc_profile = NULL;
  uint8_t checksum[CHECKSUM_SIZE];

  if (!get_icc_file (icc_bytes, icc_length, &icc_file, error))
    return NULL;

  if (!get_icc_profile (icc_bytes, icc_length, &icc_profile, error))
    return NULL;

  if (!get_checksum (icc_profile, checksum, error))
    return NULL;

  color_state_icc = g_object_new (CLUTTER_TYPE_COLOR_STATE_ICC,
                                  "context", context,
                                  NULL);

  color_state_icc->file = g_steal_pointer (&icc_file);
  color_state_icc->bytes = g_bytes_new (icc_bytes, icc_length);
  color_state_icc->icc_profile = g_steal_pointer (&icc_profile);
  memcpy (color_state_icc->checksum, checksum, sizeof (checksum));

  return CLUTTER_COLOR_STATE (color_state_icc);
}

/**
 * clutter_color_state_icc_new:
 *
 * Create a new ClutterColorStateIcc object from an icc profile.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_icc_new (ClutterContext  *context,
                             const uint8_t   *icc_bytes,
                             uint32_t         icc_length,
                             GError         **error)
{
  return clutter_color_state_icc_new_full (context,
                                           icc_bytes,
                                           icc_length,
                                           error);
}
