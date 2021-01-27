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

#include "backends/edid.h"
#include "backends/meta-output.h"

#include "backends/meta-crtc.h"

enum
{
  PROP_0,

  PROP_ID,
  PROP_GPU,
  PROP_INFO,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

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

  int backlight;
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

void
meta_output_info_parse_edid (MetaOutputInfo *output_info,
                             GBytes         *edid)
{
  MonitorInfo *parsed_edid;
  size_t len;

  if (!edid)
    goto out;

  parsed_edid = decode_edid (g_bytes_get_data (edid, &len));

  if (parsed_edid)
    {
      output_info->vendor = g_strndup (parsed_edid->manufacturer_code, 4);
      if (!g_utf8_validate (output_info->vendor, -1, NULL))
        g_clear_pointer (&output_info->vendor, g_free);

      output_info->product = g_strndup (parsed_edid->dsc_product_name, 14);
      if (!g_utf8_validate (output_info->product, -1, NULL) ||
          output_info->product[0] == '\0')
        {
          g_clear_pointer (&output_info->product, g_free);
          output_info->product = g_strdup_printf ("0x%04x", (unsigned) parsed_edid->product_code);
        }

      output_info->serial = g_strndup (parsed_edid->dsc_serial_number, 14);
      if (!g_utf8_validate (output_info->serial, -1, NULL) ||
          output_info->serial[0] == '\0')
        {
          g_clear_pointer (&output_info->serial, g_free);
          output_info->serial = g_strdup_printf ("0x%08x", parsed_edid->serial_number);
        }

      g_free (parsed_edid);
    }

 out:
  if (!output_info->vendor)
    output_info->vendor = g_strdup ("unknown");
  if (!output_info->product)
    output_info->product = g_strdup ("unknown");
  if (!output_info->serial)
    output_info->serial = g_strdup ("unknown");
}

gboolean
meta_output_is_laptop (MetaOutput *output)
{
  const MetaOutputInfo *output_info = meta_output_get_info (output);

  switch (output_info->connector_type)
    {
    case META_CONNECTOR_TYPE_eDP:
    case META_CONNECTOR_TYPE_LVDS:
    case META_CONNECTOR_TYPE_DSI:
      return TRUE;
    default:
      return FALSE;
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

static void
meta_output_init (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  priv->backlight = -1;
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
  obj_props[PROP_INFO] =
    g_param_spec_boxed ("info",
                        "info",
                        "MetaOutputInfo",
                        META_TYPE_OUTPUT_INFO,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}
