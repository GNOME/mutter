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
#include "cogl/cogl-enum-types.h"

#include <glib-2.0/glib.h>
#include <gmodule.h>

uint32_t
_cogl_winsys_error_quark (void)
{
  return g_quark_from_static_string ("cogl-winsys-error-quark");
}

enum
{
  PROP_0,

  PROP_NAME,
  PROP_ID,
  PROP_CONSTRAINTS,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

typedef struct _CoglWinsysPrivate
{
  CoglWinsysID id;
  CoglRendererConstraint constraints;

  char *name;
} CoglWinsysPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (CoglWinsys, cogl_winsys, G_TYPE_OBJECT)

static void
cogl_winsys_finalize (GObject *object)
{
  CoglWinsys *winsys = COGL_WINSYS (object);
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (cogl_winsys_parent_class)->finalize (object);
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
      priv->name = g_value_get_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_enum (value);
      break;
    case PROP_CONSTRAINTS:
      priv->constraints = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cogl_winsys_class_init (CoglWinsysClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = cogl_winsys_finalize;
  gobject_class->set_property = cogl_winsys_set_property;

  obj_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_ID] =
    g_param_spec_enum ("id", NULL, NULL,
                       COGL_TYPE_WINSYS_ID,
                       COGL_WINSYS_ID_ANY,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  obj_props[PROP_CONSTRAINTS] =
    g_param_spec_flags ("constraints", NULL, NULL,
                        COGL_TYPE_RENDERER_CONSTRAINT,
                        0,
                        G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);
}

static void
cogl_winsys_init (CoglWinsys *winsys)
{
}

CoglWinsysID
cogl_winsys_get_id (CoglWinsys *winsys)
{
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  return priv->id;
}

CoglRendererConstraint
cogl_winsys_get_constraints (CoglWinsys *winsys)
{
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  return priv->constraints;
}

const char *
cogl_winsys_get_name (CoglWinsys *winsys)
{
  CoglWinsysPrivate *priv = cogl_winsys_get_instance_private (winsys);

  return priv->name;
}
