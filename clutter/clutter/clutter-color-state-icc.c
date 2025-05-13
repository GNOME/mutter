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

#include "clutter/clutter-color-state-private.h"
#include "mtk-anonymous-file.h"

#define CHECKSUM_SIZE 16

typedef enum _ClutterColorStateIccFlags
{
  CLUTTER_COLOR_STATE_ICC_FLAG_NONE = 0,
  CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR = 1 << 0,
} ClutterColorStateIccFlags;

typedef struct _ClutterColorStateIcc
{
  ClutterColorState parent;

  MtkAnonymousFile *file;
  GBytes *bytes;

  cmsHPROFILE *icc_profile;

  cmsHTRANSFORM *to_XYZ;
  cmsHTRANSFORM *from_XYZ;

  uint8_t checksum[CHECKSUM_SIZE];

  gboolean is_linear;
} ClutterColorStateIcc;

static ClutterColorState * clutter_color_state_icc_new_full (ClutterContext             *context,
                                                             const uint8_t              *icc_bytes,
                                                             uint32_t                    icc_length,
                                                             ClutterColorStateIccFlags   flags,
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

static void
clutter_color_state_icc_finalize (GObject *object)
{
  ClutterColorStateIcc *color_state_icc = CLUTTER_COLOR_STATE_ICC (object);

  g_clear_pointer (&color_state_icc->icc_profile, cmsCloseProfile);
  g_clear_pointer (&color_state_icc->to_XYZ, cmsDeleteTransform);
  g_clear_pointer (&color_state_icc->from_XYZ, cmsDeleteTransform);
  g_clear_pointer (&color_state_icc->file, mtk_anonymous_file_free);
  g_clear_pointer (&color_state_icc->bytes, g_bytes_unref);

  G_OBJECT_CLASS (clutter_color_state_icc_parent_class)->finalize (object);
}

static void
clutter_color_state_icc_init_color_transform_key (ClutterColorState               *color_state,
                                                  ClutterColorState               *target_color_state,
                                                  ClutterColorStateTransformFlags  flags,
                                                  ClutterColorTransformKey        *key)
{
  clutter_color_state_init_3d_lut_transform_key (color_state,
                                                 target_color_state,
                                                 flags,
                                                 key);
}

static void
clutter_color_state_icc_append_transform_snippet (ClutterColorState *color_state,
                                                  ClutterColorState *target_color_state,
                                                  GString           *snippet_globals,
                                                  GString           *snippet_source,
                                                  const char        *snippet_color_var)
{
  clutter_color_state_append_3d_lut_transform_snippet (color_state,
                                                       target_color_state,
                                                       snippet_globals,
                                                       snippet_source,
                                                       snippet_color_var);
}

static void
clutter_color_state_icc_update_uniforms (ClutterColorState *color_state,
                                         ClutterColorState *target_color_state,
                                         CoglPipeline      *pipeline)
{
  clutter_color_state_update_3d_lut_uniforms (color_state,
                                              target_color_state,
                                              pipeline);
}

static void
do_transform (cmsHTRANSFORM *transform,
              float         *data,
              int            n_samples)
{
  int i;

  cmsDoTransform (transform, data, data, n_samples);

  for (i = 0; i < n_samples; i++)
    {
      data[0] = CLAMP (data[0], 0.0f, 1.0f);
      data[1] = CLAMP (data[1], 0.0f, 1.0f);
      data[2] = CLAMP (data[2], 0.0f, 1.0f);
      data += 3;
    }
}

static void
clutter_color_state_icc_do_transform_to_XYZ (ClutterColorState *color_state,
                                             float             *data,
                                             int                n_samples)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);

  do_transform (color_state_icc->to_XYZ, data, n_samples);
}

static void
clutter_color_state_icc_do_transform_from_XYZ (ClutterColorState *color_state,
                                               float             *data,
                                               int                n_samples)
{
  ClutterColorStateIcc *color_state_icc =
    CLUTTER_COLOR_STATE_ICC (color_state);

  do_transform (color_state_icc->from_XYZ, data, n_samples);
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

static gboolean
clutter_color_state_icc_needs_mapping (ClutterColorState *color_state,
                                       ClutterColorState *target_color_state)
{
  return !clutter_color_state_icc_equals (color_state, target_color_state);
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
                                      CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR,
                                      &error);
  if (!blending_color_state)
    {
      g_warning ("Couldn't get ICC blending color state: %s", error->message);
      return g_object_ref (color_state);
    }

  return blending_color_state;
}

static void
clutter_color_state_icc_class_init (ClutterColorStateIccClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_icc_finalize;

  color_state_class->init_color_transform_key = clutter_color_state_icc_init_color_transform_key;
  color_state_class->append_transform_snippet = clutter_color_state_icc_append_transform_snippet;
  color_state_class->update_uniforms = clutter_color_state_icc_update_uniforms;
  color_state_class->do_transform_to_XYZ = clutter_color_state_icc_do_transform_to_XYZ;
  color_state_class->do_transform_from_XYZ = clutter_color_state_icc_do_transform_from_XYZ;
  color_state_class->equals = clutter_color_state_icc_equals;
  color_state_class->needs_mapping = clutter_color_state_icc_needs_mapping;
  color_state_class->to_string = clutter_color_state_icc_to_string;
  color_state_class->required_format = clutter_color_state_icc_required_format;
  color_state_class->get_blending = clutter_color_state_icc_get_blending;
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

static float
dot_product (float a[3],
             float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Estimation of eotf based on sketch:
 * https://lists.freedesktop.org/archives/wayland-devel/2019-March/040171.html
 */
static void
estimate_eotf_curves (cmsHPROFILE  *icc_profile,
                      cmsToneCurve *curves[3])
{
  cmsHTRANSFORM transform;
  int ch, i;
  int n_points;
  float t, step;
  float squared_max_XYZ_norm;
  float xyz[3], max_XYZ[3];
  float rgb[3] = { 0.0f, 0.0f, 0.0f };
  int valid_intents[4] = {
    INTENT_PERCEPTUAL,
    INTENT_RELATIVE_COLORIMETRIC,
    INTENT_SATURATION,
    INTENT_ABSOLUTE_COLORIMETRIC
  };
  g_autofree float *values = NULL;
  g_autoptr (cmsHPROFILE) XYZ_profile = NULL;

  XYZ_profile = cmsCreateXYZProfile ();

  for (i = 0; i < G_N_ELEMENTS (valid_intents); i++)
    {
      transform = cmsCreateTransform (icc_profile,
                                      TYPE_RGB_FLT,
                                      XYZ_profile,
                                      TYPE_XYZ_FLT,
                                      valid_intents[i],
                                      0);
      if (transform)
        break;
    }

  if (!transform)
    return;

  n_points = 1024;
  step = 1.0f / (n_points - 1);
  values = g_malloc (n_points * sizeof (float));

  for (ch = 0; ch < 3; ch++)
    {
      rgb[ch] = 1.0f;
      cmsDoTransform (transform, rgb, max_XYZ, 1);
      squared_max_XYZ_norm = dot_product (max_XYZ, max_XYZ);

      for (i = 0, t = 0.0f; i < n_points; i++, t += step)
        {
          rgb[ch] = t;
          cmsDoTransform (transform, rgb, xyz, 1);
          values[i] = dot_product (xyz, max_XYZ) / squared_max_XYZ_norm;
        }

      rgb[ch] = 0.0f;

      curves[ch] = cmsBuildTabulatedToneCurveFloat (NULL, n_points, values);

      if (!cmsIsToneCurveMonotonic (curves[ch]))
        g_warning ("Estimated curve is not monotonic, something is "
                   "probably wrong");
    }

  cmsDeleteTransform (transform);
}

static gboolean
get_eotf_profiles (cmsHPROFILE                *icc_profile,
                   cmsHPROFILE               **eotf_profile,
                   cmsHPROFILE               **inv_eotf_profile,
                   ClutterColorStateIccFlags   flags,
                   GError                    **error)
{
  g_autoptr (cmsHPROFILE) eotf_prof = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_prof = NULL;
  cmsToneCurve *eotfs[3] = { 0 };
  cmsToneCurve *inv_eotfs[3] = { 0 };

  if ((flags & CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR) == 0)
    return TRUE;

  if (cmsIsMatrixShaper (icc_profile))
    {
      eotfs[0] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigRedTRCTag));
      eotfs[1] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigGreenTRCTag));
      eotfs[2] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigBlueTRCTag));
    }
  else
    {
      estimate_eotf_curves (icc_profile, eotfs);
    }

  if (!eotfs[0] || !eotfs[1] || !eotfs[2])
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't %s to get EOTF of ICC profile",
                   cmsIsMatrixShaper (icc_profile) ?
                   "find required tags" :
                   "estimate EOTF");
      cmsFreeToneCurveTriple (eotfs);
      return FALSE;
    }

  inv_eotfs[0] = cmsReverseToneCurve (eotfs[0]);
  inv_eotfs[1] = cmsReverseToneCurve (eotfs[1]);
  inv_eotfs[2] = cmsReverseToneCurve (eotfs[2]);
  if (!inv_eotfs[0] || !inv_eotfs[1] || !inv_eotfs[2])
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't inverse EOTFs of ICC profile");
      cmsFreeToneCurveTriple (eotfs);
      cmsFreeToneCurveTriple (inv_eotfs);
      return FALSE;
    }

  eotf_prof = cmsCreateLinearizationDeviceLink (cmsSigRgbData, eotfs);
  inv_eotf_prof = cmsCreateLinearizationDeviceLink (cmsSigRgbData, inv_eotfs);
  if (!eotf_prof || !inv_eotf_prof)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Couldn't create EOTFs profiles from ICC profile");
      cmsFreeToneCurveTriple (eotfs);
      cmsFreeToneCurveTriple (inv_eotfs);
      return FALSE;
    }

  cmsFreeToneCurveTriple (eotfs);
  cmsFreeToneCurveTriple (inv_eotfs);

  *eotf_profile = g_steal_pointer (&eotf_prof);
  *inv_eotf_profile = g_steal_pointer (&inv_eotf_prof);

  return TRUE;
}

static gboolean
get_transform_to_XYZ (cmsHPROFILE                *icc_profile,
                      cmsHPROFILE                *inv_eotf_profile,
                      cmsHTRANSFORM             **out_transform,
                      ClutterColorStateIccFlags   flags,
                      GError                    **error)
{
  g_autoptr (cmsHPROFILE) XYZ_profile = NULL;
  cmsHPROFILE profiles[3];
  cmsHTRANSFORM transform;
  int n_profiles;

  n_profiles = 0;

  XYZ_profile = cmsCreateXYZProfile ();

  if (flags & CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR)
    profiles[n_profiles++] = inv_eotf_profile;

  profiles[n_profiles++] = icc_profile;
  profiles[n_profiles++] = XYZ_profile;

  transform = cmsCreateMultiprofileTransform (profiles,
                                              n_profiles,
                                              TYPE_RGB_FLT,
                                              TYPE_XYZ_FLT,
                                              INTENT_RELATIVE_COLORIMETRIC,
                                              0);
  if (!transform)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed generating ICC transform to XYZ");
      return FALSE;
    }

  *out_transform = transform;

  return TRUE;
}

static gboolean
get_transform_from_XYZ (cmsHPROFILE                *icc_profile,
                        cmsHPROFILE                *eotf_profile,
                        cmsHTRANSFORM             **out_transform,
                        ClutterColorStateIccFlags   flags,
                        GError                    **error)
{
  g_autoptr (cmsHPROFILE) XYZ_profile = NULL;
  cmsHPROFILE profiles[3];
  cmsHTRANSFORM transform;
  int n_profiles;

  n_profiles = 0;

  XYZ_profile = cmsCreateXYZProfile ();

  profiles[n_profiles++] = XYZ_profile;
  profiles[n_profiles++] = icc_profile;

  if (flags & CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR)
    profiles[n_profiles++] = eotf_profile;

  transform = cmsCreateMultiprofileTransform (profiles,
                                              n_profiles,
                                              TYPE_XYZ_FLT,
                                              TYPE_RGB_FLT,
                                              INTENT_RELATIVE_COLORIMETRIC,
                                              0);
  if (!transform)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed generating ICC transform from XYZ");
      return FALSE;
    }

  *out_transform = transform;

  return TRUE;
}

static gboolean
get_transforms (cmsHPROFILE                *icc_profile,
                cmsHTRANSFORM             **to_XYZ,
                cmsHTRANSFORM             **from_XYZ,
                ClutterColorStateIccFlags   flags,
                GError                    **error)
{
  g_autoptr (cmsHPROFILE) eotf_profile = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_profile = NULL;

  if (!get_eotf_profiles (icc_profile,
                          &eotf_profile,
                          &inv_eotf_profile,
                          flags,
                          error))
    return FALSE;

  if (!get_transform_to_XYZ (icc_profile,
                             inv_eotf_profile,
                             to_XYZ,
                             flags,
                             error))
    return FALSE;

  if (!get_transform_from_XYZ (icc_profile,
                               eotf_profile,
                               from_XYZ,
                               flags,
                               error))
    return FALSE;

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
                                  ClutterColorStateIccFlags   flags,
                                  GError                    **error)
{
  ClutterColorStateIcc *color_state_icc;
  g_autoptr (MtkAnonymousFile) icc_file = NULL;
  g_autoptr (cmsHPROFILE) icc_profile = NULL;
  g_autoptr (cmsHTRANSFORM) to_XYZ = NULL;
  g_autoptr (cmsHTRANSFORM) from_XYZ = NULL;
  uint8_t checksum[CHECKSUM_SIZE];

  if (!get_icc_file (icc_bytes, icc_length, &icc_file, error))
    return NULL;

  if (!get_icc_profile (icc_bytes, icc_length, &icc_profile, error))
    return NULL;

  if (!get_transforms (icc_profile, &to_XYZ, &from_XYZ, flags, error))
    return NULL;

  if (!get_checksum (icc_profile, checksum, error))
    return NULL;

  color_state_icc = g_object_new (CLUTTER_TYPE_COLOR_STATE_ICC,
                                  "context", context,
                                  NULL);

  color_state_icc->file = g_steal_pointer (&icc_file);
  color_state_icc->bytes = g_bytes_new (icc_bytes, icc_length);
  color_state_icc->icc_profile = g_steal_pointer (&icc_profile);
  color_state_icc->to_XYZ = g_steal_pointer (&to_XYZ);
  color_state_icc->from_XYZ = g_steal_pointer (&from_XYZ);
  memcpy (color_state_icc->checksum, checksum, sizeof (checksum));
  color_state_icc->is_linear = flags & CLUTTER_COLOR_STATE_ICC_FLAG_LINEAR;

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
                                           CLUTTER_COLOR_STATE_ICC_FLAG_NONE,
                                           error);
}
