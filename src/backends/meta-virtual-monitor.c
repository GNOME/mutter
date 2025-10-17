/*
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
 *
 */

#include "config.h"

#include "backends/meta-virtual-monitor.h"

#include "backends/meta-crtc.h"
#include "backends/meta-crtc-mode.h"
#include "backends/meta-output.h"

enum
{
  DESTROY,

  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

enum
{
  PROP_0,

  PROP_CRTC,
  PROP_CRTC_MODE,
  PROP_OUTPUT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaVirtualMonitorPrivate
{
  MetaCrtc *crtc;
  MetaCrtcMode *crtc_mode;
  MetaOutput *output;

  gboolean is_destroyed;
} MetaVirtualMonitorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MetaVirtualMonitor, meta_virtual_monitor,
                                     G_TYPE_OBJECT)

static MetaVirtualModeInfo *
meta_virtual_mode_info_dup (const MetaVirtualModeInfo *mode_info)
{
  return g_memdup2 (mode_info, sizeof (*mode_info));
}

MetaVirtualModeInfo *
meta_virtual_mode_info_new (int   width,
                            int   height,
                            float refresh_rate)
{
  MetaVirtualModeInfo *mode_info;

  mode_info = g_new0 (MetaVirtualModeInfo, 1);
  mode_info->width = width;
  mode_info->height = height;
  mode_info->refresh_rate = refresh_rate;

  return mode_info;
}

MetaVirtualMonitorInfo *
meta_virtual_monitor_info_new (const char *vendor,
                               const char *product,
                               const char *serial,
                               GList      *mode_infos)
{
  MetaVirtualMonitorInfo *info;

  info = g_new0 (MetaVirtualMonitorInfo, 1);
  info->mode_infos = g_list_copy_deep (mode_infos,
                                       (GCopyFunc) meta_virtual_mode_info_dup,
                                       NULL);
  info->vendor = g_strdup (vendor);
  info->product = g_strdup (product);
  info->serial = g_strdup (serial);

  return info;
}

MetaVirtualMonitorInfo *
meta_virtual_monitor_info_new_simple (int         width,
                                      int         height,
                                      float       refresh_rate,
                                      const char *vendor,
                                      const char *product,
                                      const char *serial)
{
  g_autolist (MetaVirtualModeInfo) mode_infos = NULL;

  mode_infos = g_list_append (mode_infos,
                              meta_virtual_mode_info_new (width, height,
                                                          refresh_rate));

  return meta_virtual_monitor_info_new (vendor, product, serial, mode_infos);
}

void
meta_virtual_monitor_info_free (MetaVirtualMonitorInfo *info)
{
  g_list_free_full (info->mode_infos, g_free);
  g_free (info->vendor);
  g_free (info->product);
  g_free (info->serial);
  g_free (info);
}

MetaCrtc *
meta_virtual_monitor_get_crtc (MetaVirtualMonitor *virtual_monitor)
{
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  return priv->crtc;
}

MetaCrtcMode *
meta_virtual_monitor_get_crtc_mode (MetaVirtualMonitor *virtual_monitor)
{
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  return priv->crtc_mode;
}

MetaOutput *
meta_virtual_monitor_get_output (MetaVirtualMonitor *virtual_monitor)
{
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  return priv->output;
}

static void
meta_virtual_monitor_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaVirtualMonitor *virtual_monitor = META_VIRTUAL_MONITOR (object);
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  switch (prop_id)
    {
    case PROP_CRTC:
      priv->crtc = g_value_get_object (value);
      break;
    case PROP_CRTC_MODE:
      g_set_object (&priv->crtc_mode,
                    g_value_get_object (value));
      break;
    case PROP_OUTPUT:
      priv->output = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_virtual_monitor_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaVirtualMonitor *virtual_monitor = META_VIRTUAL_MONITOR (object);
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  switch (prop_id)
    {
    case PROP_CRTC:
      g_value_set_object (value, priv->crtc);
      break;
    case PROP_CRTC_MODE:
      g_value_set_object (value, priv->crtc_mode);
      break;
    case PROP_OUTPUT:
      g_value_set_object (value, priv->output);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_virtual_monitor_dispose (GObject *object)
{
  MetaVirtualMonitor *virtual_monitor = META_VIRTUAL_MONITOR (object);
  MetaVirtualMonitorPrivate *priv =
    meta_virtual_monitor_get_instance_private (virtual_monitor);

  if (!priv->is_destroyed)
    {
      g_signal_emit (virtual_monitor, signals[DESTROY], 0);
      priv->is_destroyed = TRUE;
    }

  g_clear_object (&priv->crtc);
  g_clear_object (&priv->crtc_mode);
  g_clear_object (&priv->output);

  G_OBJECT_CLASS (meta_virtual_monitor_parent_class)->dispose (object);
}

static void
meta_virtual_monitor_init (MetaVirtualMonitor *virtual_monitor)
{
}

static void
meta_virtual_monitor_class_init (MetaVirtualMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_virtual_monitor_set_property;
  object_class->get_property = meta_virtual_monitor_get_property;
  object_class->dispose = meta_virtual_monitor_dispose;

  obj_props[PROP_CRTC] =
    g_param_spec_object ("crtc", NULL, NULL,
                         META_TYPE_CRTC,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CRTC_MODE] =
    g_param_spec_object ("crtc-mode", NULL, NULL,
                         META_TYPE_CRTC_MODE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_OUTPUT] =
    g_param_spec_object ("output", NULL, NULL,
                         META_TYPE_OUTPUT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[DESTROY] =
    g_signal_new ("destroy",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

void
meta_virtual_monitor_set_mode (MetaVirtualMonitor *virtual_monitor,
                               int                 width,
                               int                 height,
                               float               refresh_rate)
{
  MetaVirtualMonitorClass *klass =
    META_VIRTUAL_MONITOR_GET_CLASS (virtual_monitor);

  klass->set_mode (virtual_monitor, width, height, refresh_rate);
}
