/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009,2010 Intel Corporation.
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

#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"

#include "cogl/deprecated/cogl-shader-private.h"
#include "cogl/deprecated/cogl-program-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

#include <string.h>

G_DEFINE_FINAL_TYPE (CoglProgram, cogl_program, G_TYPE_OBJECT);

static void
cogl_program_dispose (GObject *object)
{
  CoglProgram *program = COGL_PROGRAM (object);
  int i;

  /* Unref all of the attached shaders and destroy the list */
  g_slist_free_full (program->attached_shaders, g_object_unref);

  for (i = 0; i < program->custom_uniforms->len; i++)
    {
      CoglProgramUniform *uniform =
        &g_array_index (program->custom_uniforms, CoglProgramUniform, i);

      g_free (uniform->name);

      _cogl_boxed_value_destroy (&uniform->value);
    }

  g_array_free (program->custom_uniforms, TRUE);

  G_OBJECT_CLASS (cogl_program_parent_class)->dispose (object);
}

static void
cogl_program_init (CoglProgram *program)
{
}

static void
cogl_program_class_init (CoglProgramClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_program_dispose;
}

/* A CoglProgram is effectively just a list of shaders that will be
   used together and a set of values for the custom uniforms. No
   actual GL program is created - instead this is the responsibility
   of the GLSL pipeline backend. The uniform values are collected in
   an array and then flushed whenever the pipeline backend requests
   it. */

CoglProgram*
cogl_program_new (void)
{
  CoglProgram *program;

  program = g_object_new (COGL_TYPE_PROGRAM, NULL);

  program->custom_uniforms =
    g_array_new (FALSE, FALSE, sizeof (CoglProgramUniform));
  program->age = 0;

  return program;
}

void
cogl_program_attach_shader (CoglProgram *program,
                            CoglShader  *shader)
{
  if (!COGL_IS_PROGRAM (program) || !COGL_IS_SHADER (shader))
    return;

  program->attached_shaders
    = g_slist_prepend (program->attached_shaders,
                       g_object_ref (shader));

  program->age++;
}

void
cogl_program_link (CoglProgram *program)
{
  /* There's no point in linking the program here because it will have
     to be relinked with a different fixed functionality shader
     whenever the settings change */
}

int
cogl_program_get_uniform_location (CoglProgram *program,
                                   const char  *uniform_name)
{
  g_return_val_if_fail (COGL_IS_PROGRAM (program), -1);

  int i;
  CoglProgramUniform *uniform;

  /* We can't just ask the GL program object for the uniform location
     directly because it will change every time the program is linked
     with a different shader. Instead we make our own mapping of
     uniform numbers and cache the names */
  for (i = 0; i < program->custom_uniforms->len; i++)
    {
      uniform = &g_array_index (program->custom_uniforms,
                                CoglProgramUniform, i);

      if (!strcmp (uniform->name, uniform_name))
        return i;
    }

  /* Create a new uniform with the given name */
  g_array_set_size (program->custom_uniforms,
                    program->custom_uniforms->len + 1);
  uniform = &g_array_index (program->custom_uniforms,
                            CoglProgramUniform,
                            program->custom_uniforms->len - 1);

  uniform->name = g_strdup (uniform_name);
  memset (&uniform->value, 0, sizeof (CoglBoxedValue));
  uniform->dirty = TRUE;
  uniform->location_valid = FALSE;

  return program->custom_uniforms->len - 1;
}

static CoglProgramUniform *
cogl_program_modify_uniform (CoglProgram *program,
                             int uniform_no)
{
  CoglProgramUniform *uniform;

  g_return_val_if_fail (COGL_IS_PROGRAM (program), NULL);
  g_return_val_if_fail (uniform_no >= 0 &&
                        uniform_no < program->custom_uniforms->len,
                        NULL);

  uniform = &g_array_index (program->custom_uniforms,
                            CoglProgramUniform, uniform_no);
  uniform->dirty = TRUE;

  return uniform;
}

void
cogl_program_set_uniform_1f (CoglProgram *program,
                             int          uniform_location,
                             float        value)
{
  CoglProgramUniform *uniform;

  uniform = cogl_program_modify_uniform (program, uniform_location);
  _cogl_boxed_value_set_1f (&uniform->value, value);
}

void
cogl_program_set_uniform_1i (CoglProgram *program,
                             int          uniform_location,
                             int          value)
{
  CoglProgramUniform *uniform;

  uniform = cogl_program_modify_uniform (program, uniform_location);
  _cogl_boxed_value_set_1i (&uniform->value, value);
}

void
cogl_program_set_uniform_float (CoglProgram *program,
                                int          uniform_location,
                                int          n_components,
                                int          count,
                                const float *value)
{
  CoglProgramUniform *uniform;

  uniform = cogl_program_modify_uniform (program, uniform_location);
  _cogl_boxed_value_set_float (&uniform->value, n_components, count, value);
}

void
cogl_program_set_uniform_int (CoglProgram *program,
                              int          uniform_location,
                              int          n_components,
                              int          count,
                              const int   *value)
{
  CoglProgramUniform *uniform;

  uniform = cogl_program_modify_uniform (program, uniform_location);
  _cogl_boxed_value_set_int (&uniform->value, n_components, count, value);
}

void
cogl_program_set_uniform_matrix (CoglProgram *program,
                                 int          uniform_location,
                                 int          dimensions,
                                 int          count,
                                 gboolean     transpose,
                                 const float *value)
{
  CoglProgramUniform *uniform;

  uniform = cogl_program_modify_uniform (program, uniform_location);
  _cogl_boxed_value_set_matrix (&uniform->value,
                                dimensions,
                                count,
                                transpose,
                                value);
}

void
_cogl_program_flush_uniforms (CoglContext *ctx,
                              CoglProgram *program,
                              GLuint       gl_program,
                              gboolean     gl_program_changed)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglProgramUniform *uniform;
  int i;

  for (i = 0; i < program->custom_uniforms->len; i++)
    {
      uniform = &g_array_index (program->custom_uniforms,
                                CoglProgramUniform, i);

      if (gl_program_changed || uniform->dirty)
        {
          if (gl_program_changed || !uniform->location_valid)
            {
              GE_RET (uniform->location, driver,
                      glGetUniformLocation (gl_program, uniform->name));

               uniform->location_valid = TRUE;
            }

          /* If the uniform isn't really in the program then there's
             no need to actually set it */
          if (uniform->location != -1)
            {
               _cogl_boxed_value_set_uniform (ctx,
                                              uniform->location,
                                              &uniform->value);
            }

          uniform->dirty = FALSE;
        }
    }
}

static gboolean
_cogl_program_has_shader_type (CoglProgram *program,
                               CoglShaderType type)
{
  GSList *l;

  for (l = program->attached_shaders; l; l = l->next)
    {
      CoglShader *shader = l->data;

      if (shader->type == type)
        return TRUE;
    }

  return FALSE;
}

gboolean
_cogl_program_has_fragment_shader (CoglProgram *program)
{
  return _cogl_program_has_shader_type (program, COGL_SHADER_TYPE_FRAGMENT);
}

gboolean
_cogl_program_has_vertex_shader (CoglProgram *program)
{
  return _cogl_program_has_shader_type (program, COGL_SHADER_TYPE_VERTEX);
}
