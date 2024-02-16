/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009 Intel Corporation.
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

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-types.h"
#include "cogl/cogl-macros.h"
#include "cogl/deprecated/cogl-program.h"

G_BEGIN_DECLS

/**
 * CoglShader:
 *
 * Functions for accessing the programmable GL pipeline
 *
 * Cogl allows accessing the GL programmable pipeline in order to create
 * vertex and fragment shaders.
 *
 * When using GLSL Cogl provides replacement names for most of the
 * builtin varyings and uniforms. It is recommended to use these names
 * wherever possible to increase portability between OpenGL 2.0 and
 * GLES 2.0. GLES 2.0 does not have most of the builtins under their
 * original names so they will only work with the Cogl names.
 *
 * For use in all GLSL shaders, the Cogl builtins are as follows:
 *
 * - `uniform mat4 cogl_modelview_matrix`
 *    The current modelview matrix. This is equivalent to
 *    #gl_ModelViewMatrix.
 * - `uniform mat4 cogl_projection_matrix`
 *    The current projection matrix. This is equivalent to
 *    #gl_ProjectionMatrix.
* - `uniform mat4 cogl_modelview_projection_matrix`
 *    The combined modelview and projection matrix. A vertex shader
 *    would typically use this to transform the incoming vertex
 *    position. The separate modelview and projection matrices are
 *    usually only needed for lighting calculations. This is
 *    equivalent to #gl_ModelViewProjectionMatrix.
 * - `uniform mat4 cogl_texture_matrix[]`
 *    An array of matrices for transforming the texture
 *    coordinates. This is equivalent to #gl_TextureMatrix.
 *
 * In a vertex shader, the following are also available:
 *
 * - `attribute vec4 cogl_position_in`
 *    The incoming vertex position. This is equivalent to #gl_Vertex.
 * - `attribute vec4 cogl_color_in`
 *    The incoming vertex color. This is equivalent to #gl_Color.
 * - `attribute vec4 cogl_tex_coord_in`
 *    The texture coordinate for the first texture unit. This is
 *    equivalent to #gl_MultiTexCoord0.
 * - `attribute vec4 cogl_tex_coord0_in`
 *    The texture coordinate for the first texture unit. This is
 *    equivalent to #gl_MultiTexCoord0. There is also
 *    #cogl_tex_coord1_in and so on.
 * - `attribute vec3 cogl_normal_in`
 *    The normal of the vertex. This is equivalent to #gl_Normal.
 * - `vec4 cogl_position_out`
 *    The calculated position of the vertex. This must be written to
 *    in all vertex shaders. This is equivalent to #gl_Position.
 * - `float cogl_point_size_out`
 *    The calculated size of a point. This is equivalent to #gl_PointSize.
 * - `varying vec4 cogl_color_out`
 *    The calculated color of a vertex. This is equivalent to #gl_FrontColor.
 * - `varying vec4 cogl_tex_coord_out[]`
 *    An array of calculated texture coordinates for a vertex. This is
 *    equivalent to #gl_TexCoord.
 *
 * In a fragment shader, the following are also available:
 *
 * - `varying vec4 cogl_color_in`
 *    The calculated color of a vertex. This is equivalent to #gl_FrontColor.
 * - `varying vec4 cogl_tex_coord_in[]`
 *    An array of calculated texture coordinates for a vertex. This is
 *    equivalent to #gl_TexCoord.
 * - `vec4 cogl_color_out`
 *    The final calculated color of the fragment. All fragment shaders
 *    must write to this variable. This is equivalent to
 *    #gl_FrontColor.
 * - `float cogl_depth_out`
 *    An optional output variable specifying the depth value to use
 *    for this fragment. This is equivalent to #gl_FragDepth.
 * - `bool cogl_front_facing`
 *    A readonly variable that will be true if the current primitive
 *    is front facing. This can be used to implement two-sided
 *    coloring algorithms. This is equivalent to #gl_FrontFacing.
 *
 * It's worth nothing that this API isn't what Cogl would like to have
 * in the long term and it may be removed in Cogl 2.0. The
 * experimental #CoglShader API is the proposed replacement.
 */

#define COGL_TYPE_SHADER (cogl_shader_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglShader,
                      cogl_shader,
                      COGL,
                      SHADER,
                      GObject)

/**
 * CoglShaderType:
 * @COGL_SHADER_TYPE_VERTEX: A program for processing vertices
 * @COGL_SHADER_TYPE_FRAGMENT: A program for processing fragments
 *
 * Types of shaders
 */
typedef enum
{
  COGL_SHADER_TYPE_VERTEX,
  COGL_SHADER_TYPE_FRAGMENT
} CoglShaderType;

/**
 * cogl_create_shader:
 * @shader_type: COGL_SHADER_TYPE_VERTEX or COGL_SHADER_TYPE_FRAGMENT.
 *
 * Create a new shader handle, use cogl_shader_source() to set the
 * source code to be used on it.
 *
 * Returns: (transfer full): a new shader handle.
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT CoglShader*
cogl_create_shader (CoglShaderType shader_type);

/**
 * cogl_shader_source:
 * @self: A shader.
 * @source: Shader source.
 *
 * Replaces the current source associated with a shader with a new
 * one.
 * 
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_shader_source (CoglShader *self,
                    const char *source);

/**
 * cogl_shader_get_shader_type:
 * @self: #CoglShader for a shader.
 *
 * Retrieves the type of a shader
 *
 * Return value: %COGL_SHADER_TYPE_VERTEX if the shader is a vertex processor
 *          or %COGL_SHADER_TYPE_FRAGMENT if the shader is a fragment processor
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT CoglShaderType
cogl_shader_get_shader_type (CoglShader *self);

/**
 * cogl_create_program:
 *
 * Create a new cogl program object that can be used to replace parts of the GL
 * rendering pipeline with custom code.
 *
 * Returns: (transfer full): a new cogl program.
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT CoglProgram*
cogl_create_program (void);

/**
 * cogl_program_attach_shader:
 * @program: a #CoglProgram for a shader program.
 * @shader: a #CoglShader for a vertex of fragment shader.
 *
 * Attaches a shader to a program object. A program can have multiple
 * vertex or fragment shaders but only one of them may provide a
 * main() function. It is allowed to use a program with only a vertex
 * shader or only a fragment shader.
 *
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_attach_shader (CoglProgram *program,
                            CoglShader  *shader);

/**
 * cogl_program_link:
 * @program: A shader program.
 *
 * Links a program making it ready for use. Note that calling this
 * function is optional. If it is not called the program will
 * automatically be linked the first time it is used.
 *
 * Deprecated: 1.16: Use #CoglSnippet api
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_link (CoglProgram *program);

/**
 * cogl_program_get_uniform_location:
 * @program: A shader program.
 * @uniform_name: the name of a uniform.
 *
 * Retrieve the location (offset) of a uniform variable in a shader program,
 * a uniform is a variable that is constant for all vertices/fragments for a
 * shader object and is possible to modify as an external parameter.
 *
 * Return value: the offset of a uniform in a specified program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT int
cogl_program_get_uniform_location (CoglProgram *program,
                                   const char  *uniform_name);

/**
 * cogl_program_set_uniform_1f:
 * @program: A linked program
 * @uniform_location: the uniform location retrieved from
 *    [method@Program.get_uniform_location].
 * @value: the new value of the uniform.
 *
 * Changes the value of a floating point uniform for the given linked
 * @program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_set_uniform_1f (CoglProgram *program,
                             int          uniform_location,
                             float        value);

/**
 * cogl_program_set_uniform_1i:
 * @program: A linked program
 * @uniform_location: the uniform location retrieved from
 *    [method@Program.get_uniform_location].
 * @value: the new value of the uniform.
 *
 * Changes the value of an integer uniform for the given linked
 * @program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_set_uniform_1i (CoglProgram *program,
                             int          uniform_location,
                             int          value);

/**
 * cogl_program_set_uniform_float:
 * @program: A linked program
 * @uniform_location: the uniform location retrieved from
 *    [method@Program.get_uniform_location].
 * @n_components: The number of components for the uniform. For
 * example with glsl you'd use 3 for a vec3 or 4 for a vec4.
 * @count: For uniform arrays this is the array length otherwise just
 * pass 1
 * @value: (array length=count): the new value of the uniform[s].
 *
 * Changes the value of a float vector uniform, or uniform array for
 * the given linked @program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_set_uniform_float (CoglProgram *program,
                                int          uniform_location,
                                int          n_components,
                                int          count,
                                const float *value);

/**
 * cogl_program_set_uniform_int:
 * @program: A linked program
 * @uniform_location: the uniform location retrieved from
 *    [method@Program.get_uniform_location].
 * @n_components: The number of components for the uniform. For
 * example with glsl you'd use 3 for a vec3 or 4 for a vec4.
 * @count: For uniform arrays this is the array length otherwise just
 * pass 1
 * @value: (array length=count): the new value of the uniform[s].
 *
 * Changes the value of a int vector uniform, or uniform array for
 * the given linked @program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_set_uniform_int (CoglProgram *program,
                              int          uniform_location,
                              int          n_components,
                              int          count,
                              const int   *value);

/**
 * cogl_program_set_uniform_matrix:
 * @program: A linked program
 * @uniform_location: the uniform location retrieved from
 *    [method@Program.get_uniform_location].
 * @dimensions: The dimensions of the matrix. So for for example pass
 *    2 for a 2x2 matrix or 3 for 3x3.
 * @count: For uniform arrays this is the array length otherwise just
 * pass 1
 * @transpose: Whether to transpose the matrix when setting the uniform.
 * @value: (array length=count): the new value of the uniform.
 *
 * Changes the value of a matrix uniform, or uniform array in the
 * given linked @program.
 * Deprecated: 1.16: Use #CoglSnippet api instead
 */
COGL_DEPRECATED_FOR (cogl_snippet_)
COGL_EXPORT void
cogl_program_set_uniform_matrix (CoglProgram *program,
                                 int          uniform_location,
                                 int          dimensions,
                                 int          count,
                                 gboolean     transpose,
                                 const float *value);

G_END_DECLS
