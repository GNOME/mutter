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

#include "backends/meta-crtc.h"

#include "backends/meta-gpu.h"

enum
{
  PROP_0,

  PROP_ID,
  PROP_BACKEND,
  PROP_GPU,
  PROP_ALL_TRANSFORMS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCrtcPrivate
{
  uint64_t id;

  MetaBackend *backend;
  MetaGpu *gpu;

  MetaMonitorTransform all_transforms;

  GList *outputs;
  MetaCrtcConfig *config;
} MetaCrtcPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaCrtc, meta_crtc, G_TYPE_OBJECT)

uint64_t
meta_crtc_get_id (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->id;
}

MetaBackend *
meta_crtc_get_backend (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->backend;
}

MetaGpu *
meta_crtc_get_gpu (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->gpu;
}

const GList *
meta_crtc_get_outputs (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->outputs;
}

void
meta_crtc_assign_output (MetaCrtc   *crtc,
                         MetaOutput *output)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  priv->outputs = g_list_append (priv->outputs, output);
}

void
meta_crtc_unassign_output (MetaCrtc   *crtc,
                           MetaOutput *output)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  g_return_if_fail (g_list_find (priv->outputs, output));

  priv->outputs = g_list_remove (priv->outputs, output);
}

MetaMonitorTransform
meta_crtc_get_all_transforms (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->all_transforms;
}

void
meta_crtc_set_config (MetaCrtc       *crtc,
                      MetaCrtcConfig *config,
                      gpointer        backend_private)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);
  MetaCrtcClass *klass = META_CRTC_GET_CLASS (crtc);

  meta_crtc_unset_config (crtc);

  if (klass->set_config)
    klass->set_config (crtc, config, backend_private);

  priv->config = config;
}

void
meta_crtc_unset_config (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  g_clear_pointer (&priv->config, g_free);
}

const MetaCrtcConfig *
meta_crtc_get_config (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  return priv->config;
}

gboolean
meta_crtc_assign_extra (MetaCrtc            *crtc,
                        MetaCrtcAssignment  *crtc_assignment,
                        GPtrArray           *crtc_assignments,
                        GError             **error)
{
  MetaCrtcClass *klass = META_CRTC_GET_CLASS (crtc);

  if (klass->assign_extra)
    return klass->assign_extra (crtc, crtc_assignment, crtc_assignments, error);
  else
    return TRUE;
}

size_t
meta_crtc_get_gamma_lut_size (MetaCrtc *crtc)
{
  return META_CRTC_GET_CLASS (crtc)->get_gamma_lut_size (crtc);
}

MetaGammaLut *
meta_crtc_get_gamma_lut (MetaCrtc *crtc)
{
  return META_CRTC_GET_CLASS (crtc)->get_gamma_lut (crtc);
}

void
meta_crtc_set_gamma_lut (MetaCrtc           *crtc,
                         const MetaGammaLut *lut)
{
  return META_CRTC_GET_CLASS (crtc)->set_gamma_lut (crtc, lut);
}

void
meta_gamma_lut_free (MetaGammaLut *lut)
{
  g_free (lut->red);
  g_free (lut->green);
  g_free (lut->blue);
  g_free (lut);
}

MetaGammaLut *
meta_gamma_lut_new (int             size,
                    const uint16_t *red,
                    const uint16_t *green,
                    const uint16_t *blue)
{
  MetaGammaLut *gamma;

  gamma = g_new0 (MetaGammaLut, 1);
  *gamma = (MetaGammaLut) {
    .size = size,
    .red = g_memdup2 (red, size * sizeof (*red)),
    .green = g_memdup2 (green, size * sizeof (*green)),
    .blue = g_memdup2 (blue, size * sizeof (*blue)),
  };

  return gamma;
}

MetaGammaLut *
meta_gamma_lut_new_sized (int size)
{
  MetaGammaLut *gamma;

  gamma = g_new0 (MetaGammaLut, 1);
  *gamma = (MetaGammaLut) {
    .size = size,
    .red = g_new0 (uint16_t, size),
    .green = g_new0 (uint16_t, size),
    .blue = g_new0 (uint16_t, size),
  };

  return gamma;
}

MetaGammaLut *
meta_gamma_lut_new_identity (int size)
{
  MetaGammaLut *lut = meta_gamma_lut_new_sized (size);
  int i;

  if (size < 2)
    return lut;

  for (i = 0; i < size; i++)
    {
      double value = (i / (double) (size - 1));

      lut->red[i] = value * UINT16_MAX;
      lut->green[i] = value * UINT16_MAX;
      lut->blue[i] = value * UINT16_MAX;
    }

  return lut;
}

gboolean
meta_gamma_lut_is_identity (const MetaGammaLut *lut)
{
  int i;

  if (!lut)
    return TRUE;

  for (i = 0; i < lut->size; i++)
    {
      uint16_t value = (i / (double) (lut->size - 1)) * UINT16_MAX;

      if (ABS (lut->red[i] - value) > 1 ||
          ABS (lut->green[i] - value) > 1 ||
          ABS (lut->blue[i] - value) > 1)
        return FALSE;
    }

  return TRUE;
}

MetaGammaLut *
meta_gamma_lut_copy (const MetaGammaLut *gamma)
{
  g_return_val_if_fail (gamma != NULL, NULL);

  return meta_gamma_lut_new (gamma->size, gamma->red, gamma->green, gamma->blue);
}

MetaGammaLut *
meta_gamma_lut_copy_to_size (const MetaGammaLut *gamma,
                             int                 target_size)
{
  MetaGammaLut *out;

  g_return_val_if_fail (gamma != NULL, NULL);

  if (gamma->size == target_size)
    return meta_gamma_lut_copy (gamma);

  out = meta_gamma_lut_new_sized (target_size);

  if (target_size >= gamma->size)
    {
      int i, j;
      int slots;

      slots = target_size / gamma->size;
      for (i = 0; i < gamma->size; i++)
        {
          for (j = 0; j < slots; j++)
            {
              out->red[i * slots + j] = gamma->red[i];
              out->green[i * slots + j] = gamma->green[i];
              out->blue[i * slots + j] = gamma->blue[i];
            }
        }

      for (j = i * slots; j < target_size; j++)
        {
          out->red[j] = gamma->red[i - 1];
          out->green[j] = gamma->green[i - 1];
          out->blue[j] = gamma->blue[i - 1];
        }
    }
  else
    {
      int i;
      int idx;

      for (i = 0; i < target_size; i++)
        {
          idx = i * (gamma->size - 1) / (target_size - 1);

          out->red[i] = gamma->red[idx];
          out->green[i] = gamma->green[idx];
          out->blue[i] = gamma->blue[idx];
        }
    }

  return out;
}

gboolean
meta_gamma_lut_equal (const MetaGammaLut *gamma,
                      const MetaGammaLut *other_gamma)
{
  if (gamma == other_gamma)
    return TRUE;

  if (gamma == NULL || other_gamma == NULL)
    return FALSE;

  return gamma->size == other_gamma->size &&
         memcmp (gamma->red, other_gamma->red,
                 gamma->size * sizeof (uint16_t)) == 0 &&
         memcmp (gamma->green, other_gamma->green,
                 gamma->size * sizeof (uint16_t)) == 0 &&
         memcmp (gamma->blue, other_gamma->blue,
                 gamma->size * sizeof (uint16_t)) == 0;
}

static void
meta_crtc_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  MetaCrtc *crtc = META_CRTC (object);
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  switch (prop_id)
    {
    case PROP_ID:
      priv->id = g_value_get_uint64 (value);
      break;
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_GPU:
      priv->gpu = g_value_get_object (value);
      break;
    case PROP_ALL_TRANSFORMS:
      priv->all_transforms = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_crtc_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  MetaCrtc *crtc = META_CRTC (object);
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_uint64 (value, priv->id);
      break;
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_GPU:
      g_value_set_object (value, priv->gpu);
      break;
    case PROP_ALL_TRANSFORMS:
      g_value_set_uint (value, priv->all_transforms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_crtc_finalize (GObject *object)
{
  MetaCrtc *crtc = META_CRTC (object);
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  g_clear_pointer (&priv->config, g_free);
  g_clear_pointer (&priv->outputs, g_list_free);

  G_OBJECT_CLASS (meta_crtc_parent_class)->finalize (object);
}

static void
meta_crtc_init (MetaCrtc *crtc)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);

  priv->all_transforms = META_MONITOR_ALL_TRANSFORMS;
}

static void
meta_crtc_class_init (MetaCrtcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_crtc_set_property;
  object_class->get_property = meta_crtc_get_property;
  object_class->finalize = meta_crtc_finalize;

  obj_props[PROP_ID] =
    g_param_spec_uint64 ("id", NULL, NULL,
                         0, UINT64_MAX, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GPU] =
    g_param_spec_object ("gpu", NULL, NULL,
                         META_TYPE_GPU,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ALL_TRANSFORMS] =
    g_param_spec_uint ("all-transforms", NULL, NULL,
                       0,
                       META_MONITOR_ALL_TRANSFORMS,
                       META_MONITOR_ALL_TRANSFORMS,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

MetaCrtcConfig *
meta_crtc_config_new (graphene_rect_t      *layout,
                      MetaCrtcMode         *mode,
                      MetaMonitorTransform  transform)
{
  MetaCrtcConfig *config;

  config = g_new0 (MetaCrtcConfig, 1);
  config->layout = *layout;
  config->mode = mode;
  config->transform = transform;

  return config;
}
