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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/meta-crtc.h"

#include "backends/meta-gpu.h"

enum
{
  PROP_0,

  PROP_ID,
  PROP_GPU,
  PROP_ALL_TRANSFORMS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCrtcPrivate
{
  uint64_t id;

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
meta_crtc_set_config (MetaCrtc             *crtc,
                      graphene_rect_t      *layout,
                      MetaCrtcMode         *mode,
                      MetaMonitorTransform  transform)
{
  MetaCrtcPrivate *priv = meta_crtc_get_instance_private (crtc);
  MetaCrtcConfig *config;

  meta_crtc_unset_config (crtc);

  config = g_new0 (MetaCrtcConfig, 1);
  config->layout = *layout;
  config->mode = mode;
  config->transform = transform;

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
    g_param_spec_uint64 ("id",
                         "id",
                         "CRTC id",
                         0, UINT64_MAX, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_GPU] =
    g_param_spec_object ("gpu",
                         "gpu",
                         "MetaGpu",
                         META_TYPE_GPU,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ALL_TRANSFORMS] =
    g_param_spec_uint ("all-transforms",
                       "all-transforms",
                       "All transforms",
                       0,
                       META_MONITOR_ALL_TRANSFORMS,
                       META_MONITOR_ALL_TRANSFORMS,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
