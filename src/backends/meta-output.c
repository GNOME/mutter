/*
 * Copyright (C) 2017 Red Hat
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

#include "backends/edid.h"
#include "backends/meta-output.h"

#include "backends/meta-crtc.h"

enum
{
  PROP_0,

  PROP_ID,
  PROP_GPU,
  PROP_INFO,
  PROP_IS_PRIVACY_SCREEN_ENABLED,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  COLOR_SPACE_CHANGED,
  HDR_METADATA_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef struct _MetaOutputPrivate
{
  uint64_t id;

  MetaGpu *gpu;

  MetaOutputInfo *info;

  MetaMonitor *monitor;

  /* The CRTC driving this output, NULL if the output is not enabled */
  MetaCrtc *crtc;

  gboolean is_primary;
  gboolean is_presentation;

  gboolean is_underscanning;

  gboolean has_max_bpc;
  unsigned int max_bpc;

  int backlight;

  MetaPrivacyScreenState privacy_screen_state;
  gboolean is_privacy_screen_enabled;

  MetaOutputHdrMetadata hdr_metadata;
  MetaOutputColorspace color_space;
  MetaOutputRGBRange rgb_range;
} MetaOutputPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaOutput, meta_output, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (MetaOutputInfo, meta_output_info,
                     meta_output_info_ref,
                     meta_output_info_unref)

MetaOutputInfo *
meta_output_info_new (void)
{
  MetaOutputInfo *output_info;

  output_info = g_new0 (MetaOutputInfo, 1);
  g_ref_count_init (&output_info->ref_count);

  return output_info;
}

MetaOutputInfo *
meta_output_info_ref (MetaOutputInfo *output_info)
{
  g_ref_count_inc (&output_info->ref_count);
  return output_info;
}

void
meta_output_info_unref (MetaOutputInfo *output_info)
{
  if (g_ref_count_dec (&output_info->ref_count))
    {
      g_free (output_info->name);
      g_free (output_info->vendor);
      g_free (output_info->product);
      g_free (output_info->serial);
      g_free (output_info->edid_checksum_md5);
      g_free (output_info->edid_info);
      g_free (output_info->modes);
      g_free (output_info->possible_crtcs);
      g_free (output_info->possible_clones);
      g_free (output_info);
    }
}

uint64_t
meta_output_get_id (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->id;
}

MetaGpu *
meta_output_get_gpu (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->gpu;
}

MetaMonitor *
meta_output_get_monitor (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_warn_if_fail (priv->monitor);

  return priv->monitor;
}

void
meta_output_set_monitor (MetaOutput  *output,
                         MetaMonitor *monitor)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_warn_if_fail (!priv->monitor);

  priv->monitor = monitor;
}

void
meta_output_unset_monitor (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_warn_if_fail (priv->monitor);

  priv->monitor = NULL;
}

const char *
meta_output_get_name (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->info->name;
}

gboolean
meta_output_is_primary (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->is_primary;
}

gboolean
meta_output_is_presentation (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->is_presentation;
}

gboolean
meta_output_is_underscanning (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->is_underscanning;
}

gboolean
meta_output_get_max_bpc (MetaOutput   *output,
                         unsigned int *max_bpc)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  if (priv->has_max_bpc && max_bpc)
    *max_bpc = priv->max_bpc;

  return priv->has_max_bpc;
}

void
meta_output_set_backlight (MetaOutput *output,
                           int         backlight)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  priv->backlight = backlight;
}

int
meta_output_get_backlight (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->backlight;
}

void
meta_output_add_possible_clone (MetaOutput *output,
                                MetaOutput *possible_clone)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);
  MetaOutputInfo *output_info = priv->info;

  output_info->n_possible_clones++;
  output_info->possible_clones = g_renew (MetaOutput *,
                                          output_info->possible_clones,
                                          output_info->n_possible_clones);
  output_info->possible_clones[output_info->n_possible_clones - 1] =
    possible_clone;
}

const MetaOutputInfo *
meta_output_get_info (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->info;
}

void
meta_output_assign_crtc (MetaOutput                 *output,
                         MetaCrtc                   *crtc,
                         const MetaOutputAssignment *output_assignment)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_assert (crtc);

  meta_output_unassign_crtc (output);

  g_set_object (&priv->crtc, crtc);

  meta_crtc_assign_output (crtc, output);

  priv->is_primary = output_assignment->is_primary;
  priv->is_presentation = output_assignment->is_presentation;
  priv->is_underscanning = output_assignment->is_underscanning;

  if (output_assignment->rgb_range)
    priv->rgb_range = output_assignment->rgb_range;

  priv->has_max_bpc = output_assignment->has_max_bpc;
  if (priv->has_max_bpc)
    priv->max_bpc = output_assignment->max_bpc;
}

void
meta_output_unassign_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  if (priv->crtc)
    {
      meta_crtc_unassign_output (priv->crtc, output);
      g_clear_object (&priv->crtc);
    }

  priv->is_primary = FALSE;
  priv->is_presentation = FALSE;
}

MetaCrtc *
meta_output_get_assigned_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->crtc;
}

MetaMonitorTransform
meta_output_logical_to_crtc_transform (MetaOutput           *output,
                                       MetaMonitorTransform  transform)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);
  MetaMonitorTransform panel_orientation_transform;

  panel_orientation_transform = priv->info->panel_orientation_transform;
  return meta_monitor_transform_transform (transform,
                                           panel_orientation_transform);
}

MetaMonitorTransform
meta_output_crtc_to_logical_transform (MetaOutput           *output,
                                       MetaMonitorTransform  transform)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);
  MetaMonitorTransform inverted_panel_orientation_transform;

  inverted_panel_orientation_transform =
    meta_monitor_transform_invert (priv->info->panel_orientation_transform);
  return meta_monitor_transform_transform (transform,
                                           inverted_panel_orientation_transform);
}

static void
set_output_details_from_edid (MetaOutputInfo *output_info,
                              MetaEdidInfo   *edid_info)
{
  output_info->vendor = g_strdup (edid_info->manufacturer_code);
  if (!g_utf8_validate (output_info->vendor, -1, NULL))
    g_clear_pointer (&output_info->vendor, g_free);

  output_info->product = g_strdup (edid_info->dsc_product_name);
  if (!output_info->product ||
      !g_utf8_validate (output_info->product, -1, NULL) ||
      output_info->product[0] == '\0')
    {
      g_clear_pointer (&output_info->product, g_free);
      output_info->product =
        g_strdup_printf ("0x%04x", (unsigned) edid_info->product_code);
    }

  output_info->serial = g_strdup (edid_info->dsc_serial_number);
  if (!output_info->serial ||
      !g_utf8_validate (output_info->serial, -1, NULL) ||
      output_info->serial[0] == '\0')
    {
      g_clear_pointer (&output_info->serial, g_free);
      output_info->serial =
        g_strdup_printf ("0x%08x", edid_info->serial_number);
    }
}

void
meta_output_info_parse_edid (MetaOutputInfo *output_info,
                             GBytes         *edid)
{
  MetaEdidInfo *edid_info;
  size_t size;
  gconstpointer data;

  g_return_if_fail (!output_info->edid_info);
  g_return_if_fail (edid);

  data = g_bytes_get_data (edid, &size);
  edid_info = meta_edid_info_new_parse (data, size);

  output_info->edid_checksum_md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5,
                                                                data, size);

  if (edid_info)
    {
      output_info->edid_info = edid_info;
      set_output_details_from_edid (output_info, edid_info);
    }
}

static void
meta_output_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaOutput *output = META_OUTPUT (object);
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  switch (prop_id)
    {
    case PROP_ID:
      priv->id = g_value_get_uint64 (value);
      break;
    case PROP_GPU:
      priv->gpu = g_value_get_object (value);
      break;
    case PROP_INFO:
      priv->info = meta_output_info_ref (g_value_get_boxed (value));
      break;
    case PROP_IS_PRIVACY_SCREEN_ENABLED:
      priv->is_privacy_screen_enabled = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_output_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  MetaOutput *output = META_OUTPUT (object);
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_uint64 (value, priv->id);
      break;
    case PROP_GPU:
      g_value_set_object (value, priv->gpu);
      break;
    case PROP_INFO:
      g_value_set_boxed (value, priv->info);
      break;
    case PROP_IS_PRIVACY_SCREEN_ENABLED:
      g_value_set_boolean (value, priv->is_privacy_screen_enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_output_dispose (GObject *object)
{
  MetaOutput *output = META_OUTPUT (object);
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_clear_object (&priv->crtc);

  G_OBJECT_CLASS (meta_output_parent_class)->dispose (object);
}

static void
meta_output_finalize (GObject *object)
{
  MetaOutput *output = META_OUTPUT (object);
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_clear_pointer (&priv->info, meta_output_info_unref);

  G_OBJECT_CLASS (meta_output_parent_class)->finalize (object);
}

MetaPrivacyScreenState
meta_output_get_privacy_screen_state (MetaOutput *output)
{
  MetaOutputClass *output_class = META_OUTPUT_GET_CLASS (output);

  if (!output_class->get_privacy_screen_state)
    return META_PRIVACY_SCREEN_UNAVAILABLE;

  return output_class->get_privacy_screen_state (output);
}

gboolean
meta_output_is_privacy_screen_supported (MetaOutput *output)
{
  return !(meta_output_get_privacy_screen_state (output) ==
           META_PRIVACY_SCREEN_UNAVAILABLE);
}

gboolean
meta_output_is_privacy_screen_enabled (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->privacy_screen_state;
}

gboolean
meta_output_set_privacy_screen_enabled (MetaOutput  *output,
                                        gboolean     enabled,
                                        GError     **error)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);
  MetaPrivacyScreenState state;

  state = priv->privacy_screen_state;

  if (state == META_PRIVACY_SCREEN_UNAVAILABLE)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "The privacy screen is not supported by this output");
      return FALSE;
    }

  if (state & META_PRIVACY_SCREEN_LOCKED)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                           "The privacy screen is locked at hardware level, "
                           "impossible to set it");
      return FALSE;
    }

  if (priv->is_privacy_screen_enabled == enabled)
    return TRUE;

  priv->is_privacy_screen_enabled = enabled;
  g_object_notify_by_pspec (G_OBJECT (output),
                            obj_props[PROP_IS_PRIVACY_SCREEN_ENABLED]);
  return TRUE;
}

gboolean
meta_output_info_is_color_space_supported (const MetaOutputInfo *output_info,
                                           MetaOutputColorspace  color_space)
{
  MetaEdidColorimetry colorimetry;

  if (!output_info->edid_info)
    return FALSE;

  colorimetry = output_info->edid_info->colorimetry;

  switch (color_space)
    {
    case META_OUTPUT_COLORSPACE_DEFAULT:
      return TRUE;
    case META_OUTPUT_COLORSPACE_BT2020:
      return !!(colorimetry & META_EDID_COLORIMETRY_BT2020RGB);
    default:
      return FALSE;
    }
}

gboolean
meta_output_is_color_space_supported (MetaOutput           *output,
                                      MetaOutputColorspace  color_space)
{
  MetaOutputClass *output_class = META_OUTPUT_GET_CLASS (output);

  if (!output_class->is_color_space_supported)
    return FALSE;

  return output_class->is_color_space_supported (output, color_space);
}

void
meta_output_set_color_space (MetaOutput           *output,
                             MetaOutputColorspace  color_space)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  priv->color_space = color_space;

  g_signal_emit (output, signals[COLOR_SPACE_CHANGED], 0);
}

MetaOutputColorspace
meta_output_peek_color_space (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->color_space;
}

const char *
meta_output_colorspace_get_name (MetaOutputColorspace color_space)
{
  switch (color_space)
    {
    case META_OUTPUT_COLORSPACE_UNKNOWN:
      return "Unknown";
    case META_OUTPUT_COLORSPACE_DEFAULT:
      return "Default";
    case META_OUTPUT_COLORSPACE_BT2020:
      return "bt.2020";
    }
  g_assert_not_reached ();
}

gboolean
meta_output_is_hdr_metadata_supported (MetaOutput *output,
                                       MetaOutputHdrMetadataEOTF eotf)
{
  MetaOutputClass *output_class = META_OUTPUT_GET_CLASS (output);
  const MetaOutputInfo *output_info = meta_output_get_info (output);
  MetaEdidTransferFunction tf = 0;

  g_assert (output_info != NULL);
  if (!output_info->edid_info)
    return FALSE;

  if ((output_info->edid_info->hdr_static_metadata.sm &
       META_EDID_STATIC_METADATA_TYPE1) == 0)
    return FALSE;

  switch (eotf)
    {
    case META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR:
      tf = META_EDID_TF_TRADITIONAL_GAMMA_SDR;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR:
      tf = META_EDID_TF_TRADITIONAL_GAMMA_HDR;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_PQ:
      tf = META_EDID_TF_PQ;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_HLG:
      tf = META_EDID_TF_HLG;
      break;
    }

  if ((output_info->edid_info->hdr_static_metadata.tf & tf) == 0)
    return FALSE;

  if (!output_class->is_hdr_metadata_supported)
    return FALSE;

  return output_class->is_hdr_metadata_supported (output);
}

void
meta_output_set_hdr_metadata (MetaOutput            *output,
                              MetaOutputHdrMetadata *metadata)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  priv->hdr_metadata = *metadata;

  g_signal_emit (output, signals[HDR_METADATA_CHANGED], 0);
}

MetaOutputHdrMetadata *
meta_output_peek_hdr_metadata (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return &priv->hdr_metadata;
}

MetaOutputRGBRange
meta_output_peek_rgb_range (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->rgb_range;
}

static void
meta_output_init (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  priv->backlight = -1;
  priv->is_primary = FALSE;
  priv->is_presentation = FALSE;
  priv->is_underscanning = FALSE;
  priv->color_space = META_OUTPUT_COLORSPACE_DEFAULT;
  priv->hdr_metadata.active = FALSE;
  priv->has_max_bpc = FALSE;
  priv->max_bpc = 0;
  priv->rgb_range = META_OUTPUT_RGB_RANGE_AUTO;
}

static void
meta_output_class_init (MetaOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_output_set_property;
  object_class->get_property = meta_output_get_property;
  object_class->dispose = meta_output_dispose;
  object_class->finalize = meta_output_finalize;

  obj_props[PROP_ID] =
    g_param_spec_uint64 ("id", NULL, NULL,
                         0, UINT64_MAX, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GPU] =
    g_param_spec_object ("gpu", NULL, NULL,
                         META_TYPE_GPU,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_INFO] =
    g_param_spec_boxed ("info", NULL, NULL,
                        META_TYPE_OUTPUT_INFO,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IS_PRIVACY_SCREEN_ENABLED] =
    g_param_spec_boolean ("is-privacy-screen-enabled", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[COLOR_SPACE_CHANGED] =
    g_signal_new ("color-space-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[HDR_METADATA_CHANGED] =
    g_signal_new ("hdr-metadata-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

gboolean
meta_tile_info_equal (MetaTileInfo *a,
                      MetaTileInfo *b)
{
  if (a == b)
    return TRUE;

  if (a->group_id != b->group_id)
    return FALSE;

  if (a->flags != b->flags)
    return FALSE;

  if (a->max_h_tiles != b->max_h_tiles)
    return FALSE;

  if (a->max_v_tiles != b->max_v_tiles)
    return FALSE;

  if (a->loc_h_tile != b->loc_h_tile)
    return FALSE;

  if (a->loc_v_tile != b->loc_v_tile)
    return FALSE;

  if (a->tile_w != b->tile_w)
    return FALSE;

  if (a->tile_h != b->tile_h)
    return FALSE;

  return TRUE;
}

void
meta_output_update_modes (MetaOutput    *output,
                          MetaCrtcMode  *preferred_mode,
                          MetaCrtcMode **modes,
                          int            n_modes)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);
  int i;

  for (i = 0; i < priv->info->n_modes; i++)
    g_object_unref (priv->info->modes[i]);
  g_free (priv->info->modes);

  priv->info->preferred_mode = preferred_mode;
  priv->info->modes = modes;
  priv->info->n_modes = n_modes;
}
