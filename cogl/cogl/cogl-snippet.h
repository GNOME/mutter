/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011, 2013 Intel Corporation.
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
 *
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

G_BEGIN_DECLS

/**
 * CoglSnippet:
 *
 * Functions for creating and manipulating shader snippets
 *
 * `CoglSnippet`s are used to modify or replace parts of a
 * #CoglPipeline using GLSL. GLSL is a programming language supported
 * by OpenGL on programmable hardware to provide a more flexible
 * description of what should be rendered. A description of GLSL
 * itself is outside the scope of this documentation but any good
 * OpenGL book should help to describe it.
 *
 * Unlike in OpenGL, when using GLSL with Cogl it is possible to write
 * short snippets to replace small sections of the pipeline instead of
 * having to replace the whole of either the vertex or fragment
 * pipelines. Of course it is also possible to replace the whole of
 * the pipeline if needed.
 *
 * Each snippet is a standalone chunk of code which would attach to
 * the pipeline at a particular point. The code is split into four
 * separate strings (all of which are optional):
 *
 * - `declarations`
 * The code in this string will be inserted outside of any function in
 * the global scope of the shader. This can be used to declare
 * uniforms, attributes, varyings and functions to be used by the
 * snippet.
 * - `pre`
 * The code in this string will be inserted before the hook point.
 * - `post`
 * The code in this string will be inserted after the hook point. This
 * can be used to modify the results of the builtin generated code for
 * that hook point.
 * - `replace
 * If present the code in this string will replace the generated code
 * for the hook point.
 *
 * All of the strings apart from the declarations string of a pipeline
 * are generated in a single function so they can share variables
 * declared from one string in another. The scope of the code is
 * limited to each snippet so local variables declared in the snippet
 * will not collide with variables declared in another
 * snippet. However, code in the 'declarations' string is global to
 * the shader so it is the application's responsibility to ensure that
 * variables declared here will not collide with those from other
 * snippets.
 *
 * The snippets can be added to a pipeline with
 * cogl_pipeline_add_snippet() or
 * cogl_pipeline_add_layer_snippet(). Which function to use depends on
 * which hook the snippet is targeting. The snippets are all
 * generated in the order they are added to the pipeline. That is, the
 * post strings are executed in the order they are added to the
 * pipeline and the pre strings are executed in reverse order. If any
 * replace strings are given for a snippet then any other snippets
 * with the same hook added before that snippet will be ignored. The
 * different hooks are documented under #CoglSnippetHook.
 *
 * For portability with GLES2, it is recommended not to use the GLSL
 * builtin names such as gl_FragColor. Instead there are replacement
 * names under the cogl_* namespace which can be used instead. These
 * are:
 *
 * - `uniform mat4 cogl_modelview_matrix
 *    The current modelview matrix. This is equivalent to
 *    #gl_ModelViewMatrix.
 * - `uniform mat4 cogl_projection_matrix
 *    The current projection matrix. This is equivalent to
 *    #gl_ProjectionMatrix.
 * - `uniform mat4 cogl_modelview_projection_matrix
 *    The combined modelview and projection matrix. A vertex shader
 *    would typically use this to transform the incoming vertex
 *    position. The separate modelview and projection matrices are
 *    usually only needed for lighting calculations. This is
 *    equivalent to #gl_ModelViewProjectionMatrix.
 * - `uniform mat4 cogl_texture_matrix[]
 *    An array of matrices for transforming the texture
 *    coordinates. This is equivalent to #gl_TextureMatrix.
 *
 * In a vertex shader, the following are also available:
 *
 * - `attribute vec4 cogl_position_in
 *    The incoming vertex position. This is equivalent to #gl_Vertex.
 * - `attribute vec4 cogl_color_in`
 *    The incoming vertex color. This is equivalent to #gl_Color.
 * - `attribute vec4 cogl_tex_coord_in`
 *    The texture coordinate for layer 0. This is an alternative name
 *    for #cogl_tex_coord0_in.
 * - `attribute vec4 cogl_tex_coord0_in
 *    The texture coordinate for the layer 0. This is equivalent to
 *    #gl_MultiTexCoord0. There will also be #cogl_tex_coord1_in and
 *    so on if more layers are added to the pipeline.
 * - `attribute vec3 cogl_normal_in`
 *    The normal of the vertex. This is equivalent to #gl_Normal.
 * - `vec4 cogl_position_out
 *    The calculated position of the vertex. This must be written to
 *    in all vertex shaders. This is equivalent to #gl_Position.
 * - `float cogl_point_size_in
 *    The incoming point size from the cogl_point_size_in attribute.
 *    This is only available if
 *    cogl_pipeline_set_per_vertex_point_size() is set on the
 *    pipeline.
 * - `float cogl_point_size_out`
 *    The calculated size of a point. This is equivalent to #gl_PointSize.
 * - `varying vec4 cogl_color_out`
 *    The calculated color of a vertex. This is equivalent to #gl_FrontColor.
 * - `varying vec4 cogl_tex_coord0_out`
 *    The calculated texture coordinate for layer 0 of the pipeline.
 *    This is equivalent to #gl_TexCoord[0]. There will also be
 *    #cogl_tex_coord1_out and so on if more layers are added to the
 *    pipeline. In the fragment shader, this varying is called
 *    #cogl_tex_coord0_in.
 *
 * In a fragment shader, the following are also available:
 *
 * - `varying vec4 cogl_color_in`
 *    The calculated color of a vertex. This is equivalent to #gl_FrontColor.
 * - `varying vec4 cogl_tex_coord0_in`
 *    The texture coordinate for layer 0. This is equivalent to
 *    #gl_TexCoord[0]. There will also be #cogl_tex_coord1_in and so
 *    on if more layers are added to the pipeline.
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
 * - `vec2 cogl_point_coord`
 *    When rendering points, this will contain a vec2 which represents
 *    the position within the point of the current fragment.
 *    vec2(0.0,0.0) will be the topleft of the point and vec2(1.0,1.0)
 *    will be the bottom right. Note that there is currently a bug in
 *    Cogl where when rendering to an offscreen buffer these
 *    coordinates will be upside-down. The value is undefined when not
 *    rendering points.
 *
 * Here is an example of using a snippet to add a desaturate effect to the
 * generated color on a pipeline.
 *
 * ```c
 *   CoglPipeline *pipeline = cogl_pipeline_new ();
 *
 *   /<!-- -->* Set up the pipeline here, ie by adding a texture or other
 *      layers *<!-- -->/
 *
 *   /<!-- -->* Create the snippet. The first string is the declarations which
 *      we will use to add a uniform. The second is the 'post' string which
 *      will contain the code to perform the desaturation. *<!-- -->/
 *   CoglSnippet *snippet =
 *     cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
 *                       "uniform float factor;",
 *                       "float gray = dot (vec3 (0.299, 0.587, 0.114), "
 *                       "                  cogl_color_out.rgb);"
 *                       "cogl_color_out.rgb = mix (vec3 (gray),"
 *                       "                          cogl_color_out.rgb,"
 *                       "                          factor);");
 *
 *   /<!-- -->* Add it to the pipeline *<!-- -->/
 *   cogl_pipeline_add_snippet (pipeline, snippet);
 *   /<!-- -->* The pipeline keeps a reference to the snippet
 *      so we don't need to *<!-- -->/
 *   g_object_unref (snippet);
 *
 *   /<!-- -->* Update the custom uniform on the pipeline *<!-- -->/
 *   int location = cogl_pipeline_get_uniform_location (pipeline, "factor");
 *   cogl_pipeline_set_uniform_1f (pipeline, location, 0.5f);
 *
 *   /<!-- -->* Now we can render with the snippet as usual *<!-- -->/
 *   cogl_push_source (pipeline);
 *   cogl_rectangle (0, 0, 10, 10);
 *   cogl_pop_source ();
 * ```
 */
typedef struct _CoglSnippet CoglSnippet;

#define COGL_TYPE_SNIPPET (cogl_snippet_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglSnippet,
                      cogl_snippet,
                      COGL,
                      SNIPPET,
                      GObject)

/* Enumeration of all the hook points that a snippet can be attached
   to within a pipeline. */
/**
 * CoglSnippetHook:
 * @COGL_SNIPPET_HOOK_VERTEX_GLOBALS: A hook for declaring global data
 *   that can be shared with all other snippets that are on a vertex
 *   hook.
 * @COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS: A hook for declaring global
 *   data wthat can be shared with all other snippets that are on a
 *   fragment hook.
 * @COGL_SNIPPET_HOOK_VERTEX: A hook for the entire vertex processing
 *   stage of the pipeline.
 * @COGL_SNIPPET_HOOK_VERTEX_TRANSFORM: A hook for the vertex transformation.
 * @COGL_SNIPPET_HOOK_POINT_SIZE: A hook for manipulating the point
 *   size of a vertex. This is only used if
 *   cogl_pipeline_set_per_vertex_point_size() is enabled on the
 *   pipeline.
 * @COGL_SNIPPET_HOOK_FRAGMENT: A hook for the entire fragment
 *   processing stage of the pipeline.
 * @COGL_SNIPPET_HOOK_TEXTURE_COORD_TRANSFORM: A hook for applying the
 *   layer matrix to a texture coordinate for a layer.
 * @COGL_SNIPPET_HOOK_LAYER_FRAGMENT: A hook for the fragment
 *   processing of a particular layer.
 * @COGL_SNIPPET_HOOK_TEXTURE_LOOKUP: A hook for the texture lookup
 *   stage of a given layer in a pipeline.
 *
 * #CoglSnippetHook is used to specify a location within a
 * #CoglPipeline where the code of the snippet should be used when it
 * is attached to a pipeline.
 *
 * - `COGL_SNIPPET_HOOK_VERTEX_GLOBALS`
 * 
 * Adds a shader snippet at the beginning of the global section of the
 * shader for the vertex processing. Any declarations here can be
 * shared with all other snippets that are attached to a vertex hook.
 * Only the ‘declarations’ string is used and the other strings are
 * ignored.
 * 
 * - `COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS`
 * 
 * Adds a shader snippet at the beginning of the global section of the
 * shader for the fragment processing. Any declarations here can be
 * shared with all other snippets that are attached to a fragment
 * hook. Only the ‘declarations’ string is used and the other strings
 * are ignored.
 * 
 * - `COGL_SNIPPET_HOOK_VERTEX`
 * 
 * Adds a shader snippet that will hook on to the vertex processing
 * stage of the pipeline. This gives a chance for the application to
 * modify the vertex attributes generated by the shader. Typically the
 * snippet will modify cogl_color_out or cogl_position_out builtins.
 * 
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 * 
 * The ‘pre’ string in @snippet will be inserted at the top of the
 * main() function before any vertex processing is done.
 * 
 * The ‘replace’ string in @snippet will be used instead of the
 * generated vertex processing if it is present. This can be used if
 * the application wants to provide a complete vertex shader and
 * doesn't need the generated output from Cogl.
 * 
 * The ‘post’ string in @snippet will be inserted after all of the
 * standard vertex processing is done. This can be used to modify the
 * outputs.
 * 
 * - `COGL_SNIPPET_HOOK_VERTEX_TRANSFORM`
 * 
 * Adds a shader snippet that will hook on to the vertex transform stage.
 * Typically the snippet will use the cogl_modelview_matrix,
 * cogl_projection_matrix and cogl_modelview_projection_matrix matrices and the
 * cogl_position_in attribute. The hook must write to cogl_position_out.
 * The default processing for this hook will multiply cogl_position_in by
 * the combined modelview-projection matrix and store it on cogl_position_out.
 * 
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 * 
 * The ‘pre’ string in @snippet will be inserted at the top of the
 * main() function before the vertex transform is done.
 * 
 * The ‘replace’ string in @snippet will be used instead of the
 * generated vertex transform if it is present.
 * 
 * The ‘post’ string in @snippet will be inserted after all of the
 * standard vertex transformation is done. This can be used to modify the
 * cogl_position_out in addition to the default processing.
 * 
 * - `COGL_SNIPPET_HOOK_POINT_SIZE`
 * 
 * Adds a shader snippet that will hook on to the point size
 * calculation step within the vertex shader stage. The snippet should
 * write to the builtin cogl_point_size_out with the new point size.
 * The snippet can either read cogl_point_size_in directly and write a
 * new value or first read an existing value in cogl_point_size_out
 * that would be set by a previous snippet. Note that this hook is
 * only used if cogl_pipeline_set_per_vertex_point_size() is enabled
 * on the pipeline.
 * 
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 *
 * The ‘pre’ string in @snippet will be inserted just before
 * calculating the point size.
 * 
 * The ‘replace’ string in @snippet will be used instead of the
 * generated point size calculation if it is present.
 * 
 * The ‘post’ string in @snippet will be inserted after the
 * standard point size calculation is done. This can be used to modify
 * cogl_point_size_out in addition to the default processing.
 * 
 * - `COGL_SNIPPET_HOOK_FRAGMENT`
 * 
 * Adds a shader snippet that will hook on to the fragment processing
 * stage of the pipeline. This gives a chance for the application to
 * modify the fragment color generated by the shader. Typically the
 * snippet will modify cogl_color_out.
 * 
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 * 
 * The ‘pre’ string in @snippet will be inserted at the top of the
 * main() function before any fragment processing is done.
 * 
 * The ‘replace’ string in @snippet will be used instead of the
 * generated fragment processing if it is present. This can be used if
 * the application wants to provide a complete fragment shader and
 * doesn't need the generated output from Cogl.
 * 
 * The ‘post’ string in @snippet will be inserted after all of the
 * standard fragment processing is done. At this point the generated
 * value for the rest of the pipeline state will already be in
 * cogl_color_out so the application can modify the result by altering
 * this variable.
 * 
 * - `COGL_SNIPPET_HOOK_TEXTURE_COORD_TRANSFORM`
 * 
 * Adds a shader snippet that will hook on to the texture coordinate
 * transformation of a particular layer. This can be used to replace
 * the processing for a layer or to modify the results.
 * 
 * Within the snippet code for this hook there are two extra
 * variables. The first is a mat4 called cogl_matrix which represents
 * the user matrix for this layer. The second is called cogl_tex_coord
 * and represents the incoming and outgoing texture coordinate. On
 * entry to the hook, cogl_tex_coord contains the value of the
 * corresponding texture coordinate attribute for this layer. The hook
 * is expected to modify this variable. The output will be passed as a
 * varying to the fragment processing stage. The default code will
 * just multiply cogl_matrix by cogl_tex_coord and store the result in
 * cogl_tex_coord.
 *
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 *
 * The ‘pre’ string in @snippet will be inserted just before the
 * fragment processing for this layer. At this point cogl_tex_coord
 * still contains the value of the texture coordinate attribute.
 * If a ‘replace’ string is given then this will be used instead of
 * the default fragment processing for this layer. The snippet can
 * modify cogl_tex_coord or leave it as is to apply no transformation.
 *
 * The ‘post’ string in @snippet will be inserted just after the
 * transformation. At this point cogl_tex_coord will contain the
 * results of the transformation but it can be further modified by the
 * snippet.
 * 
 * - `COGL_SNIPPET_HOOK_LAYER_FRAGMENT`
 * 
 * Adds a shader snippet that will hook on to the fragment processing
 * of a particular layer. This can be used to replace the processing
 * for a layer or to modify the results.
 *
 * Within the snippet code for this hook there is an extra vec4
 * variable called ‘cogl_layer’. This contains the resulting color
 * that will be used for the layer. This can be modified in the ‘post’
 * section or it the default processing can be replaced entirely using
 * the ‘replace’ section.
 *
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 *
 * The ‘pre’ string in @snippet will be inserted just before the
 * fragment processing for this layer.
 *
 * If a ‘replace’ string is given then this will be used instead of
 * the default fragment processing for this layer. The snippet must write to
 * the ‘cogl_layer’ variable in that case.
 *
 * The ‘post’ string in @snippet will be inserted just after the
 * fragment processing for the layer. The results can be modified by changing
 * the value of the ‘cogl_layer’ variable.
 * 
 * - `COGL_SNIPPET_HOOK_TEXTURE_LOOKUP`
 * 
 * Adds a shader snippet that will hook on to the texture lookup part
 * of a given layer. This gives a chance for the application to modify
 * the coordinates that will be used for the texture lookup or to
 * alter the returned texel.
 *
 * Within the snippet code for this hook there are three extra
 * variables available. ‘cogl_sampler’ is a sampler object
 * representing the sampler for the layer where the snippet is
 * attached. ‘cogl_tex_coord’ is a vec4 which contains the texture
 * coordinates that will be used for the texture lookup. This can be
 * modified. ‘cogl_texel’ will contain the result of the texture
 * lookup. This can also be modified.
 *
 * The ‘declarations’ string in @snippet will be inserted in the
 * global scope of the shader. Use this to declare any uniforms,
 * attributes or functions that the snippet requires.
 *
 * The ‘pre’ string in @snippet will be inserted at the top of the
 * main() function before any fragment processing is done. This is a
 * good place to modify the cogl_tex_coord variable.
 *
 * If a ‘replace’ string is given then this will be used instead of a
 * the default texture lookup. The snippet would typically use its own
 * sampler in this case.
 *
 * The ‘post’ string in @snippet will be inserted after texture lookup
 * has been performed. Here the snippet can modify the cogl_texel
 * variable to alter the returned texel.
 */
typedef enum
{
  /* Per pipeline vertex hooks */
  COGL_SNIPPET_HOOK_VERTEX = 0,
  COGL_SNIPPET_HOOK_VERTEX_TRANSFORM,
  COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
  COGL_SNIPPET_HOOK_POINT_SIZE,

  /* Per pipeline fragment hooks */
  COGL_SNIPPET_HOOK_FRAGMENT = 2048,
  COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,

  /* Per layer vertex hooks */
  COGL_SNIPPET_HOOK_TEXTURE_COORD_TRANSFORM = 4096,

  /* Per layer fragment hooks */
  COGL_SNIPPET_HOOK_LAYER_FRAGMENT = 6144,
  COGL_SNIPPET_HOOK_TEXTURE_LOOKUP
} CoglSnippetHook;

/**
 * cogl_snippet_new:
 * @hook: The point in the pipeline that this snippet will wrap around
 *   or replace.
 * @declarations: (nullable): The source code for the declarations for this
 *   snippet or %NULL. See cogl_snippet_set_declarations().
 * @post: (nullable): The source code to run after the hook point where this
 *   shader snippet is attached or %NULL. See cogl_snippet_set_post().
 *
 * Allocates and initializes a new snippet with the given source strings.
 *
 * Returns: (transfer full): a pointer to a new #CoglSnippet
 */
COGL_EXPORT CoglSnippet *
cogl_snippet_new (CoglSnippetHook hook,
                  const char *declarations,
                  const char *post);

/**
 * cogl_snippet_get_hook:
 * @snippet: A #CoglSnippet
 *
 * Returns: (transfer none): the hook that was set when cogl_snippet_new()
 *   was called.
 */
COGL_EXPORT CoglSnippetHook
cogl_snippet_get_hook (CoglSnippet *snippet);

/**
 * cogl_snippet_set_declarations:
 * @snippet: A #CoglSnippet
 * @declarations: The new source string for the declarations section
 *   of this snippet.
 *
 * Sets a source string that will be inserted in the global scope of
 * the generated shader when this snippet is used on a pipeline. This
 * string is typically used to declare uniforms, attributes or
 * functions that will be used by the other parts of the snippets.
 *
 * This function should only be called before the snippet is attached
 * to its first pipeline. After that the snippet should be considered
 * immutable.
 */
COGL_EXPORT void
cogl_snippet_set_declarations (CoglSnippet *snippet,
                               const char *declarations);

/**
 * cogl_snippet_get_declarations:
 * @snippet: A #CoglSnippet
 *
 * Returns: (transfer none): the source string that was set with
 *   cogl_snippet_set_declarations() or %NULL if none was set.
 */
COGL_EXPORT const char *
cogl_snippet_get_declarations (CoglSnippet *snippet);

/**
 * cogl_snippet_set_pre:
 * @snippet: A #CoglSnippet
 * @pre: The new source string for the pre section of this snippet.
 *
 * Sets a source string that will be inserted before the hook point in
 * the generated shader for the pipeline that this snippet is attached
 * to. Please see the documentation of each hook point in
 * #CoglPipeline for a description of how this string should be used.
 *
 * This function should only be called before the snippet is attached
 * to its first pipeline. After that the snippet should be considered
 * immutable.
 */
COGL_EXPORT void
cogl_snippet_set_pre (CoglSnippet *snippet,
                      const char *pre);

/**
 * cogl_snippet_get_pre:
 * @snippet: A #CoglSnippet
 *
 * Returns: (transfer none): the source string that was set with
 *   cogl_snippet_set_pre() or %NULL if none was set.
 */
COGL_EXPORT const char *
cogl_snippet_get_pre (CoglSnippet *snippet);

/**
 * cogl_snippet_set_replace:
 * @snippet: A #CoglSnippet
 * @replace: The new source string for the replace section of this snippet.
 *
 * Sets a source string that will be used instead of any generated
 * source code or any previous snippets for this hook point. Please
 * see the documentation of each hook point in #CoglPipeline for a
 * description of how this string should be used.
 *
 * This function should only be called before the snippet is attached
 * to its first pipeline. After that the snippet should be considered
 * immutable.
 */
COGL_EXPORT void
cogl_snippet_set_replace (CoglSnippet *snippet,
                          const char *replace);

/**
 * cogl_snippet_get_replace:
 * @snippet: A #CoglSnippet
 *
 * Returns: (transfer none): the source string that was set with
 *   cogl_snippet_set_replace() or %NULL if none was set.
 */
COGL_EXPORT const char *
cogl_snippet_get_replace (CoglSnippet *snippet);

/**
 * cogl_snippet_set_post:
 * @snippet: A #CoglSnippet
 * @post: The new source string for the post section of this snippet.
 *
 * Sets a source string that will be inserted after the hook point in
 * the generated shader for the pipeline that this snippet is attached
 * to. Please see the documentation of each hook point in
 * #CoglPipeline for a description of how this string should be used.
 *
 * This function should only be called before the snippet is attached
 * to its first pipeline. After that the snippet should be considered
 * immutable.
 */
COGL_EXPORT void
cogl_snippet_set_post (CoglSnippet *snippet,
                       const char *post);

/**
 * cogl_snippet_get_post:
 * @snippet: A #CoglSnippet
 *
 * Returns: (transfer none): the source string that was set with
 *   cogl_snippet_set_post() or %NULL if none was set.
 */
COGL_EXPORT const char *
cogl_snippet_get_post (CoglSnippet *snippet);

G_END_DECLS
