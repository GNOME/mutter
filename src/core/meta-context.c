/*
 * Copyright (C) 2019 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "core/meta-context-private.h"

#include "core/util-private.h"

enum
{
  PROP_0,

  PROP_NAME,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaContextPrivate
{
  char *name;
} MetaContextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaContext, meta_context, G_TYPE_OBJECT)

static MetaCompositorType
meta_context_get_compositor_type (MetaContext *context)
{
  return META_CONTEXT_GET_CLASS (context)->get_compositor_type (context);
}

gboolean
meta_context_configure (MetaContext   *context,
                        int           *argc,
                        char        ***argv,
                        GError       **error)
{
  MetaCompositorType compositor_type;

  if (!META_CONTEXT_GET_CLASS (context)->configure (context, argc, argv, error))
    return FALSE;

  compositor_type = meta_context_get_compositor_type (context);
  switch (compositor_type)
    {
    case META_COMPOSITOR_TYPE_WAYLAND:
      meta_set_is_wayland_compositor (TRUE);
      break;
    case META_COMPOSITOR_TYPE_X11:
      meta_set_is_wayland_compositor (FALSE);
      break;
    }

  return TRUE;
}

static void
meta_context_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_context_finalize (GObject *object)
{
  MetaContext *context = META_CONTEXT (object);
  MetaContextPrivate *priv = meta_context_get_instance_private (context);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (meta_context_parent_class)->finalize (object);
}

static void
meta_context_class_init (MetaContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_context_get_property;
  object_class->set_property = meta_context_set_property;
  object_class->finalize = meta_context_finalize;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name",
                         "name",
                         "Human readable name",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_context_init (MetaContext *context)
{
}
