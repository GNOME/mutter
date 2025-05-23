/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

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

#include "backends/meta-gpu.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-output.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _MetaGpuPrivate
{
  MetaBackend *backend;

  GList *outputs;
  GList *crtcs;
  GList *modes;
} MetaGpuPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaGpu, meta_gpu, G_TYPE_OBJECT)

gboolean
meta_gpu_has_hotplug_mode_update (MetaGpu *gpu)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);
  GList *l;

  for (l = priv->outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;
      const MetaOutputInfo *output_info = meta_output_get_info (output);

      if (output_info->hotplug_mode_update)
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_gpu_read_current (MetaGpu  *gpu,
                       GError  **error)
{
  return META_GPU_GET_CLASS (gpu)->read_current (gpu, error);
}

MetaBackend *
meta_gpu_get_backend (MetaGpu *gpu)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  return priv->backend;
}

GList *
meta_gpu_get_outputs (MetaGpu *gpu)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  return priv->outputs;
}

GList *
meta_gpu_get_crtcs (MetaGpu *gpu)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  return priv->crtcs;
}

GList *
meta_gpu_get_modes (MetaGpu *gpu)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  return priv->modes;
}

void
meta_gpu_take_outputs (MetaGpu *gpu,
                          GList   *outputs)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  g_clear_list (&priv->outputs, g_object_unref);
  priv->outputs = outputs;
}

void
meta_gpu_take_crtcs (MetaGpu *gpu,
                     GList   *crtcs)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  g_clear_list (&priv->crtcs, g_object_unref);
  priv->crtcs = crtcs;
}

void
meta_gpu_take_modes (MetaGpu *gpu,
                     GList   *modes)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  g_clear_list (&priv->modes, g_object_unref);
  priv->modes = modes;
}

static void
meta_gpu_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  MetaGpu *gpu = META_GPU (object);
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_gpu_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  MetaGpu *gpu = META_GPU (object);
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_gpu_finalize (GObject *object)
{
  MetaGpu *gpu = META_GPU (object);
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);

  g_list_free_full (priv->outputs, g_object_unref);
  g_list_free_full (priv->modes, g_object_unref);
  g_list_free_full (priv->crtcs, g_object_unref);

  G_OBJECT_CLASS (meta_gpu_parent_class)->finalize (object);
}

static void
meta_gpu_init (MetaGpu *gpu)
{
}

static void
meta_gpu_class_init (MetaGpuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_gpu_set_property;
  object_class->get_property = meta_gpu_get_property;
  object_class->finalize = meta_gpu_finalize;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, PROP_LAST, obj_props);
}

MetaOutput *
meta_gpu_find_output (MetaGpu    *gpu,
                      MetaOutput *old_output)
{
  MetaGpuPrivate *priv = meta_gpu_get_instance_private (gpu);
  GList *l;

  for (l = priv->outputs; l; l = l->next)
    {
      MetaOutput *output = META_OUTPUT (l->data);

      if (meta_output_matches (output, old_output))
        return output;
    }

  return NULL;
}
