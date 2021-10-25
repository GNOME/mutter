/*
 * Copyright (C) 2021 Jeremy Cline
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
 */

/**
 * SECTION:meta-color-manager
 * @title: MetaColorManager
 * @short_description: Interfaces for managing color-related properties like
 *   color look-up tables and color spaces.
 *
 * Each MetaBackend has a MetaColorManager which includes interfaces for querying
 * and altering the color-related properties for displays associated with that
 * backend.
 *
 * These tasks include configuring the hardware's lookup tables (LUTs) used to
 * apply or remove transfer functions (traditionally called "gamma"), set up
 * color space conversions (CSCs), and for determining or setting the output
 * color space and transfer function.
 *
 * Mutter itself does not store and manage device ICC profiles; this task is
 * handled by [colord](https://www.freedesktop.org/software/colord/). Colord
 * maintains a database of devices (displays, printers, etc) and color profiles,
 * including the default output profile for a device. Users configure colord
 * with their preferred color profile for a device via an external application
 * like GNOME Control Center or the colormgr CLI.
 *
 * Colord defines [a specification for device and profile names](
 * https://github.com/hughsie/colord/blob/1.4.5/doc/device-and-profile-naming-spec.txt)
 * which is used to map Colord's devices to Mutter's #MetaMonitor.
 */

#include "config.h"

#include "backends/meta-color-manager-private.h"

#include "backends/meta-backend-types.h"
#include "backends/meta-monitor.h"

enum
{
  PROP_0,

  PROP_BACKEND,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaColorManagerPrivate
{
  MetaBackend *backend;
} MetaColorManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaColorManager, meta_color_manager, G_TYPE_OBJECT)

static void
meta_color_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaColorManager *color_manager = META_COLOR_MANAGER (object);
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_color_manager_class_init (MetaColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_color_manager_set_property;
  object_class->get_property = meta_color_manager_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend",
                         "backend",
                         "MetaBackend",
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_color_manager_init (MetaColorManager *color_manager)
{
}

MetaBackend *
meta_color_manager_get_backend (MetaColorManager *color_manager)
{
  MetaColorManagerPrivate *priv =
    meta_color_manager_get_instance_private (color_manager);

  return priv->backend;
}
