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

#include "backends/meta-output.h"

typedef struct _MetaOutputPrivate
{
  /* The CRTC driving this output, NULL if the output is not enabled */
  MetaCrtc *crtc;
} MetaOutputPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaOutput, meta_output, G_TYPE_OBJECT)

const char *
meta_output_get_connector_type_name (MetaOutput *output)
{
  switch (output->connector_type)
    {
    case META_CONNECTOR_TYPE_Unknown: return "Unknown";
    case META_CONNECTOR_TYPE_VGA: return "VGA";
    case META_CONNECTOR_TYPE_DVII: return "DVII";
    case META_CONNECTOR_TYPE_DVID: return "DVID";
    case META_CONNECTOR_TYPE_DVIA: return "DVIA";
    case META_CONNECTOR_TYPE_Composite: return "Composite";
    case META_CONNECTOR_TYPE_SVIDEO: return "SVIDEO";
    case META_CONNECTOR_TYPE_LVDS: return "LVDS";
    case META_CONNECTOR_TYPE_Component: return "Component";
    case META_CONNECTOR_TYPE_9PinDIN: return "9PinDIN";
    case META_CONNECTOR_TYPE_DisplayPort: return "DisplayPort";
    case META_CONNECTOR_TYPE_HDMIA: return "HDMIA";
    case META_CONNECTOR_TYPE_HDMIB: return "HDMIB";
    case META_CONNECTOR_TYPE_TV: return "TV";
    case META_CONNECTOR_TYPE_eDP: return "eDP";
    case META_CONNECTOR_TYPE_VIRTUAL: return "VIRTUAL";
    case META_CONNECTOR_TYPE_DSI: return "DSI";
    default: g_assert_not_reached ();
    }
  return NULL;
}

MetaGpu *
meta_output_get_gpu (MetaOutput *output)
{
  return output->gpu;
}

void
meta_output_assign_crtc (MetaOutput *output,
                         MetaCrtc   *crtc)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_assert (crtc);

  g_set_object (&priv->crtc, crtc);
}

void
meta_output_unassign_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  g_clear_object (&priv->crtc);
}

MetaCrtc *
meta_output_get_assigned_crtc (MetaOutput *output)
{
  MetaOutputPrivate *priv = meta_output_get_instance_private (output);

  return priv->crtc;
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

  g_free (output->name);
  g_free (output->vendor);
  g_free (output->product);
  g_free (output->serial);
  g_free (output->modes);
  g_free (output->possible_crtcs);
  g_free (output->possible_clones);

  if (output->driver_notify)
    output->driver_notify (output);

  G_OBJECT_CLASS (meta_output_parent_class)->finalize (object);
}

static void
meta_output_init (MetaOutput *output)
{
}

static void
meta_output_class_init (MetaOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_output_dispose;
  object_class->finalize = meta_output_finalize;
}
