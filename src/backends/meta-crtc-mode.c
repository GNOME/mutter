/*
 * Copyright (C) 2017-2020 Red Hat
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

#include "backends/meta-crtc-mode.h"

enum
{
  PROP_0,

  PROP_ID,
  PROP_NAME,
  PROP_INFO,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCrtcModePrivate
{
  uint64_t id;
  char *name;
  MetaCrtcModeInfo *info;
} MetaCrtcModePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaCrtcMode, meta_crtc_mode, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (MetaCrtcModeInfo, meta_crtc_mode_info,
                     meta_crtc_mode_info_ref,
                     meta_crtc_mode_info_unref)

MetaCrtcModeInfo *
meta_crtc_mode_info_new (void)
{
  MetaCrtcModeInfo *crtc_mode_info;

  crtc_mode_info = g_new0 (MetaCrtcModeInfo, 1);
  g_ref_count_init (&crtc_mode_info->ref_count);

  return crtc_mode_info;
}

MetaCrtcModeInfo *
meta_crtc_mode_info_ref (MetaCrtcModeInfo *crtc_mode_info)
{
  g_ref_count_inc (&crtc_mode_info->ref_count);
  return crtc_mode_info;
}

void
meta_crtc_mode_info_unref (MetaCrtcModeInfo *crtc_mode_info)
{
  if (g_ref_count_dec (&crtc_mode_info->ref_count))
    g_free (crtc_mode_info);
}

uint64_t
meta_crtc_mode_get_id (MetaCrtcMode *crtc_mode)
{
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  return priv->id;
}

const char *
meta_crtc_mode_get_name (MetaCrtcMode *crtc_mode)
{
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  return priv->name;
}

const MetaCrtcModeInfo *
meta_crtc_mode_get_info (MetaCrtcMode *crtc_mode)
{
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  return priv->info;
}

static void
meta_crtc_mode_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  MetaCrtcMode *crtc_mode = META_CRTC_MODE (object);
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  switch (prop_id)
    {
    case PROP_ID:
      priv->id = g_value_get_uint64 (value);
      break;
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_INFO:
      priv->info = meta_crtc_mode_info_ref (g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_crtc_mode_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  MetaCrtcMode *crtc_mode = META_CRTC_MODE (object);
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_uint64 (value, priv->id);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_INFO:
      g_value_set_boxed (value, priv->info);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_crtc_mode_finalize (GObject *object)
{
  MetaCrtcMode *crtc_mode = META_CRTC_MODE (object);
  MetaCrtcModePrivate *priv = meta_crtc_mode_get_instance_private (crtc_mode);

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->info, meta_crtc_mode_info_unref);

  G_OBJECT_CLASS (meta_crtc_mode_parent_class)->finalize (object);
}

static void
meta_crtc_mode_init (MetaCrtcMode *crtc_mode)
{
}

static void
meta_crtc_mode_class_init (MetaCrtcModeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_crtc_mode_set_property;
  object_class->get_property = meta_crtc_mode_get_property;
  object_class->finalize = meta_crtc_mode_finalize;

  obj_props[PROP_ID] =
    g_param_spec_uint64 ("id",
                         "id",
                         "CRTC mode id",
                         0, UINT64_MAX, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "name",
                         "Name of CRTC mode",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_INFO] =
    g_param_spec_boxed ("info",
                        "info",
                        "MetaOutputInfo",
                        META_TYPE_CRTC_MODE_INFO,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
