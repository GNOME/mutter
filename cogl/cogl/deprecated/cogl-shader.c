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
#include "cogl/cogl-glsl-shader-boilerplate.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/deprecated/cogl-shader-private.h"

#include <glib.h>

#include <string.h>

G_DEFINE_TYPE (CoglShader, cogl_shader, G_TYPE_OBJECT);

static void
cogl_shader_dispose (GObject *object)
{
  CoglShader *shader = COGL_SHADER (object);

  /* Frees shader resources but its handle is not
     released! Do that separately before this! */
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (shader->gl_handle)
    GE (ctx, glDeleteShader (shader->gl_handle));

  G_OBJECT_CLASS (cogl_shader_parent_class)->dispose (object);
}

static void
cogl_shader_init (CoglShader *shader)
{
}

static void
cogl_shader_class_init (CoglShaderClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_shader_dispose;
}

CoglShader*
cogl_create_shader (CoglShaderType type)
{
  CoglShader *shader;

  _COGL_GET_CONTEXT (ctx, NULL);

  switch (type)
    {
    case COGL_SHADER_TYPE_VERTEX:
    case COGL_SHADER_TYPE_FRAGMENT:
      break;
    default:
      g_warning ("Unexpected shader type (0x%08lX) given to "
                 "cogl_create_shader", (unsigned long) type);
      return NULL;
    }

  shader = g_object_new (COGL_TYPE_SHADER, NULL);
  shader->gl_handle = 0;
  shader->compilation_pipeline = NULL;
  shader->type = type;

  return shader;
}

void
cogl_shader_source (CoglShader *self,
                    const char *source)
{
  g_return_if_fail (COGL_IS_SHADER (self));

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  self->source = g_strdup (source);
}

CoglShaderType
cogl_shader_get_shader_type (CoglShader *self)
{
  g_return_val_if_fail (COGL_IS_SHADER (self), COGL_SHADER_TYPE_VERTEX);

  _COGL_GET_CONTEXT (ctx, COGL_SHADER_TYPE_VERTEX);

  return self->type;
}
