/*
 * Copyright (C) 2018-2020 Red Hat
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

#include "backends/native/meta-thread-impl.h"

#include <glib-object.h>

#include "backends/native/meta-thread.h"

enum
{
  PROP_0,

  PROP_THREAD,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaThreadImplPrivate
{
  MetaThread *thread;
  GMainContext *thread_context;
} MetaThreadImplPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaThreadImpl, meta_thread_impl, G_TYPE_OBJECT)

static void
meta_thread_impl_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  switch (prop_id)
    {
    case PROP_THREAD:
      g_value_set_object (value, priv->thread);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_thread_impl_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  switch (prop_id)
    {
    case PROP_THREAD:
      priv->thread = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_thread_impl_constructed (GObject *object)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (object);
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  G_OBJECT_CLASS (meta_thread_impl_parent_class)->constructed (object);

  priv->thread_context = g_main_context_get_thread_default ();
}

static void
meta_thread_impl_class_init (MetaThreadImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_thread_impl_get_property;
  object_class->set_property = meta_thread_impl_set_property;
  object_class->constructed = meta_thread_impl_constructed;

  obj_props[PROP_THREAD] =
    g_param_spec_object ("thread",
                         "thread",
                         "MetaThread",
                         META_TYPE_THREAD,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_thread_impl_init (MetaThreadImpl *thread_impl)
{
}

MetaThread *
meta_thread_impl_get_thread (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  return priv->thread;
}

GMainContext *
meta_thread_impl_get_main_context (MetaThreadImpl *thread_impl)
{
  MetaThreadImplPrivate *priv =
    meta_thread_impl_get_instance_private (thread_impl);

  return priv->thread_context;
}
