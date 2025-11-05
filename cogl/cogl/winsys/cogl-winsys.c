/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#include "config.h"

#include "cogl/cogl-context-private.h"
#include "cogl/winsys/cogl-winsys.h"

#include <gmodule.h>

typedef struct _CoglWinsysPrivate
{
  char *name;
} CoglWinsysPrivate;

enum
{
  PROP_0,
  PROP_NAME,
  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglWinsys, cogl_winsys, G_TYPE_OBJECT)

uint32_t
_cogl_winsys_error_quark (void)
{
  return g_quark_from_static_string ("cogl-winsys-error-quark");
}

static void
cogl_winsys_get_property (GObject      *object,
                          unsigned int  prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  CoglWinsys *winsys = COGL_WINSYS (object);
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_winsys_set_property (GObject      *object,
                          unsigned int  prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  CoglWinsys *winsys = COGL_WINSYS (object);
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cogl_winsys_finalize (GObject *object)
{
  CoglWinsys *winsys = COGL_WINSYS (object);
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (cogl_winsys_parent_class)->finalize (object);
}

static void
cogl_winsys_class_init (CoglWinsysClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = cogl_winsys_get_property;
  object_class->set_property = cogl_winsys_set_property;
  object_class->finalize = cogl_winsys_finalize;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
cogl_winsys_init (CoglWinsys *winsys)
{
}
