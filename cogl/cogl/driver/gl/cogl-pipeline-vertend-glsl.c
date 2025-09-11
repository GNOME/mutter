/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010,2013 Intel Corporation.
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

#include "config.h"

#include <string.h>

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pipeline-state-private.h"
#include "cogl/cogl-glsl-shader-boilerplate.h"
#include "cogl/driver/gl/cogl-pipeline-vertend-glsl-private.h"
#include "deprecated/cogl-program-private.h"

const CoglPipelineVertend _cogl_pipeline_glsl_vertend;

struct _CoglPipelineVertendShaderState
{
  unsigned int ref_count;

  GLuint gl_shader;
  GString *header, *source;

  CoglPipelineCacheEntry *cache_entry;
};

static GQuark shader_state_key = 0;

static CoglPipelineVertendShaderState *
shader_state_new (CoglPipelineCacheEntry *cache_entry)
{
  CoglPipelineVertendShaderState *shader_state;

  shader_state = g_new0 (CoglPipelineVertendShaderState, 1);
  shader_state->ref_count = 1;
  shader_state->cache_entry = cache_entry;

  return shader_state;
}

typedef struct
{
  CoglPipelineVertendShaderState *shader_state;
  CoglPipeline *instance;
} CoglPipelineVertendShaderStateCache;

static GQuark
get_cache_key (void)
{
  if (G_UNLIKELY (shader_state_key == 0))
    shader_state_key = g_quark_from_static_string ("shader-vertend-state-key");

  return shader_state_key;
}

static CoglPipelineVertendShaderState *
get_shader_state (CoglPipeline *pipeline)
{
  CoglPipelineVertendShaderStateCache * cache;
  cache = g_object_get_qdata (G_OBJECT (pipeline), get_cache_key ());
  if (cache)
    return cache->shader_state;
  return NULL;
}

CoglPipelineVertendShaderState *
cogl_pipeline_vertend_glsl_get_shader_state (CoglPipeline *pipeline)
{
  return get_shader_state (pipeline);
}

static void
destroy_shader_state (void *user_data)
{
  CoglPipelineVertendShaderStateCache *cache = user_data;
  CoglPipelineVertendShaderState *shader_state = cache->shader_state;
  CoglContext *ctx = cache->instance->context;

  if (shader_state->cache_entry &&
      shader_state->cache_entry->pipeline != cache->instance)
    shader_state->cache_entry->usage_count--;

  if (--shader_state->ref_count == 0)
    {
      if (shader_state->gl_shader)
        {
          CoglDriver *driver = cogl_context_get_driver (ctx);

          GE (driver, glDeleteShader (shader_state->gl_shader));
        }

      g_free (shader_state);
    }

  g_free (cache);
}

static void
set_shader_state (CoglPipeline *pipeline,
                  CoglPipelineVertendShaderState *shader_state)
{
  if (shader_state)
    {
      shader_state->ref_count++;

      /* If we're not setting the state on the template pipeline then
       * mark it as a usage of the pipeline cache entry */
      if (shader_state->cache_entry &&
          shader_state->cache_entry->pipeline != pipeline)
        shader_state->cache_entry->usage_count++;
    }

  CoglPipelineVertendShaderStateCache *cache = g_new0 (CoglPipelineVertendShaderStateCache, 1);
  cache->instance = pipeline;
  cache->shader_state = shader_state;
  g_object_set_qdata_full (G_OBJECT (pipeline),
                           get_cache_key (),
                           cache,
                           destroy_shader_state);
}

static void
dirty_shader_state (CoglPipeline *pipeline)
{
  g_object_set_qdata_full (G_OBJECT (pipeline),
                           get_cache_key (),
                           NULL,
                           NULL);
}

static gboolean
add_layer_vertex_boilerplate_cb (CoglPipelineLayer *layer,
                                 void *user_data)
{
  GString *layer_declarations = user_data;
  int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
  g_string_append_printf (layer_declarations,
                          "attribute vec4 cogl_tex_coord%d_in;\n"
                          "#define cogl_texture_matrix%i cogl_texture_matrix[%i]\n"
                          "#define cogl_tex_coord%i_out _cogl_tex_coord[%i]\n",
                          layer->index,
                          layer->index,
                          unit_index,
                          layer->index,
                          unit_index);
  return TRUE;
}

static gboolean
add_layer_fragment_boilerplate_cb (CoglPipelineLayer *layer,
                                   void *user_data)
{
  GString *layer_declarations = user_data;
  g_string_append_printf (layer_declarations,
                          "#define cogl_tex_coord%i_in _cogl_tex_coord[%i]\n",
                          layer->index,
                          _cogl_pipeline_layer_get_unit_index (layer));
  return TRUE;
}

static char *
glsl_version_string (CoglDriverGL *driver)
{
  int major, minor;
  gboolean needs_es_annotation;

  cogl_driver_gl_get_glsl_version (driver, &major, &minor);
  needs_es_annotation = cogl_driver_gl_is_es (driver) && major > 1;

  return g_strdup_printf ("%d%02d%s",
                          major,
                          minor,
                          needs_es_annotation ? " es" : "");
}

static gboolean
is_glsl140_syntax (CoglDriverGL *driver)
{
  int major, minor;

  cogl_driver_gl_get_glsl_version (driver, &major, &minor);
  if (cogl_driver_gl_is_es (driver))
    return COGL_CHECK_GL_VERSION (major, minor, 3, 0);

  return COGL_CHECK_GL_VERSION (major, minor, 1, 40);
}

void
_cogl_glsl_shader_set_source_with_boilerplate (CoglContext *ctx,
                                               GLuint shader_gl_handle,
                                               GLenum shader_gl_type,
                                               CoglPipeline *pipeline,
                                               GLsizei count_in,
                                               const char **strings_in,
                                               const GLint *lengths_in)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  const char **strings = g_alloca (sizeof (char *) * (count_in + 5));
  GLint *lengths = g_alloca (sizeof (GLint) * (count_in + 5));
  g_autofree char *glsl_version = NULL;
  g_autofree char *version_string = NULL;
  int count = 0;
  int n_layers;
  g_autoptr (GString) layer_declarations = NULL;

  glsl_version = glsl_version_string (driver_gl);
  version_string = g_strdup_printf ("#version %s\n\n", glsl_version);

  strings[count] = version_string;
  lengths[count++] = -1;

  if (cogl_driver_has_feature (driver, COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL))
    {
      static const char image_external_extension[] =
        "#extension GL_OES_EGL_image_external : require\n";
      strings[count] = image_external_extension;
      lengths[count++] = sizeof (image_external_extension) - 1;
    }

  if (shader_gl_type == GL_VERTEX_SHADER)
    {
      if (is_glsl140_syntax (driver_gl))
        {
          strings[count] = _COGL_VERTEX_SHADER_FALLBACK_BOILERPLATE;
          lengths[count++] = strlen (_COGL_VERTEX_SHADER_FALLBACK_BOILERPLATE);
        }

      strings[count] = _COGL_VERTEX_SHADER_BOILERPLATE;
      lengths[count++] = strlen (_COGL_VERTEX_SHADER_BOILERPLATE);
    }
  else if (shader_gl_type == GL_FRAGMENT_SHADER)
    {
      if (is_glsl140_syntax (driver_gl))
        {
          strings[count] = _COGL_FRAGMENT_SHADER_FALLBACK_BOILERPLATE;
          lengths[count++] = strlen (_COGL_FRAGMENT_SHADER_FALLBACK_BOILERPLATE);
        }

      strings[count] = _COGL_FRAGMENT_SHADER_BOILERPLATE;
      lengths[count++] = strlen (_COGL_FRAGMENT_SHADER_BOILERPLATE);
    }

  n_layers = cogl_pipeline_get_n_layers (pipeline);
  if (n_layers)
    {
      layer_declarations = g_string_new ("");

      g_string_append_printf (layer_declarations,
                              "varying vec4 _cogl_tex_coord[%d];\n",
                              n_layers);

      if (shader_gl_type == GL_VERTEX_SHADER)
        {
          g_string_append_printf (layer_declarations,
                                  "uniform mat4 cogl_texture_matrix[%d];\n",
                                  n_layers);

          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_vertex_boilerplate_cb,
                                                 layer_declarations);
        }
      else if (shader_gl_type == GL_FRAGMENT_SHADER)
        {
          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_fragment_boilerplate_cb,
                                                 layer_declarations);
        }

      strings[count] = layer_declarations->str;
      lengths[count++] = -1; /* null terminated */
    }

  memcpy (strings + count, strings_in, sizeof (char *) * count_in);
  if (lengths_in)
    memcpy (lengths + count, lengths_in, sizeof (GLint) * count_in);
  else
    {
      int i;

      for (i = 0; i < count_in; i++)
        lengths[count + i] = -1; /* null terminated */
    }
  count += count_in;

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SHOW_SOURCE)))
    {
      GString *buf;
      g_auto (GStrv) lines = NULL;
      int i;

      buf = g_string_new (NULL);
      for (i = 0; i < count; i++)
        if (lengths[i] != -1)
          g_string_append_len (buf, strings[i], lengths[i]);
        else
          g_string_append (buf, strings[i]);

      lines = g_strsplit (buf->str, "\n", 0);
      g_string_free (buf, TRUE);

      buf = g_string_new (NULL);
      for (i = 0; lines[i]; i++)
        g_string_append_printf (buf, "%4d: %s\n", i + 1, lines[i]);

      g_message ("%s shader (%s; %u):\n%s",
                 shader_gl_type == GL_VERTEX_SHADER ?
                 "vertex" : "fragment",
                 pipeline->name ? pipeline->name : "unknown",
                 shader_gl_handle,
                 buf->str);
      g_string_free (buf, TRUE);
    }

  GE (driver, glShaderSource (shader_gl_handle, count,
                              (const char **) strings, lengths));
}
GLuint
_cogl_pipeline_vertend_glsl_get_shader (CoglPipeline *pipeline)
{
  CoglPipelineVertendShaderState *shader_state = get_shader_state (pipeline);

  if (shader_state)
    return shader_state->gl_shader;
  else
    return 0;
}

static CoglPipelineSnippetList *
get_vertex_snippets (CoglPipeline *pipeline)
{
  pipeline =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_VERTEX_SNIPPETS);

  return &pipeline->big_state->vertex_snippets;
}

static CoglPipelineSnippetList *
get_layer_vertex_snippets (CoglPipelineLayer *layer)
{
  unsigned long state = COGL_PIPELINE_LAYER_STATE_VERTEX_SNIPPETS;
  layer = _cogl_pipeline_layer_get_authority (layer, state);

  return &layer->big_state->vertex_snippets;
}

static gboolean
add_layer_declaration_cb (CoglPipelineLayer *layer,
                          void *user_data)
{
  CoglPipelineVertendShaderState *shader_state = user_data;

  g_string_append_printf (shader_state->header,
                          "uniform sampler2D cogl_sampler%i;\n",
                          layer->index);

  return TRUE;
}

static void
add_layer_declarations (CoglPipeline *pipeline,
                        CoglPipelineVertendShaderState *shader_state)
{
  /* We always emit sampler uniforms in case there will be custom
   * layer snippets that want to sample arbitrary layers. */

  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         add_layer_declaration_cb,
                                         shader_state);
}

static void
add_global_declarations (CoglPipeline *pipeline,
                         CoglPipelineVertendShaderState *shader_state)
{
  CoglSnippetHook hook = COGL_SNIPPET_HOOK_VERTEX_GLOBALS;
  CoglPipelineSnippetList *snippets = get_vertex_snippets (pipeline);

  /* Add the global data hooks. All of the code in these snippets is
   * always added and only the declarations data is used */

  _cogl_pipeline_snippet_generate_declarations (shader_state->header,
                                                hook,
                                                snippets);
}

static void
_cogl_pipeline_vertend_glsl_start (CoglPipeline *pipeline,
                                   int n_layers,
                                   unsigned long pipelines_difference)
{
  CoglPipelineVertendShaderState *shader_state;
  CoglPipelineCacheEntry *cache_entry = NULL;
  CoglProgram *user_program = cogl_pipeline_get_user_program (pipeline);
  CoglContext *ctx = pipeline->context;

  /* Now lookup our glsl backend private state (allocating if
   * necessary) */
  shader_state = get_shader_state (pipeline);

  if (shader_state == NULL)
    {
      CoglPipeline *authority;

      /* Get the authority for anything affecting vertex shader
         state */
      authority = _cogl_pipeline_find_equivalent_parent
        (pipeline,
         _cogl_pipeline_get_state_for_vertex_codegen (ctx) &
         ~COGL_PIPELINE_STATE_LAYERS,
         COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN);

      shader_state = get_shader_state (authority);

      if (shader_state == NULL)
        {
          /* Check if there is already a similar cached pipeline whose
             shader state we can share */
          if (G_LIKELY (!(COGL_DEBUG_ENABLED
                          (COGL_DEBUG_DISABLE_PROGRAM_CACHES))))
            {
              cache_entry =
                _cogl_pipeline_cache_get_vertex_template (ctx->pipeline_cache,
                                                          authority);

              shader_state = get_shader_state (cache_entry->pipeline);
            }

          if (shader_state)
            shader_state->ref_count++;
          else
            shader_state = shader_state_new (cache_entry);

          set_shader_state (authority, shader_state);

          shader_state->ref_count--;

          if (cache_entry)
            set_shader_state (cache_entry->pipeline, shader_state);
        }

      if (authority != pipeline)
        set_shader_state (pipeline, shader_state);
    }

  if (user_program)
    {
      /* If the user program contains a vertex shader then we don't need
         to generate one */
      if (_cogl_program_has_vertex_shader (user_program))
        {
          if (shader_state->gl_shader)
            {
              CoglDriver *driver = cogl_context_get_driver (ctx);

              GE (driver, glDeleteShader (shader_state->gl_shader));
              shader_state->gl_shader = 0;
            }
          return;
        }
    }

  if (shader_state->gl_shader)
    return;

  /* If we make it here then we have a shader_state struct without a gl_shader
     either because this is the first time we've encountered it or
     because the user program has changed */

  /* We reuse two grow-only GStrings for code-gen. One string
     contains the uniform and attribute declarations while the
     other contains the main function. We need two strings
     because we need to dynamically declare attributes as the
     add_layer callback is invoked */
  g_string_set_size (ctx->codegen_header_buffer, 0);
  g_string_set_size (ctx->codegen_source_buffer, 0);
  shader_state->header = ctx->codegen_header_buffer;
  shader_state->source = ctx->codegen_source_buffer;

  add_layer_declarations (pipeline, shader_state);
  add_global_declarations (pipeline, shader_state);

  g_string_append (shader_state->source,
                   "void\n"
                   "cogl_generated_source ()\n"
                   "{\n");

  if (cogl_pipeline_get_per_vertex_point_size (pipeline))
    g_string_append (shader_state->header,
                     "attribute float cogl_point_size_in;\n");
  else
    {
      /* There is no builtin uniform for the point size on GLES2 so we
         need to copy it from the custom uniform in the vertex shader
         if we're not using per-vertex point sizes, however we'll only
         do this if the point-size is non-zero. Toggle the point size
         between zero and non-zero causes a state change which
         generates a new program */
      if (cogl_pipeline_get_point_size (pipeline) > 0.0f)
        {
          g_string_append (shader_state->header,
                           "uniform float cogl_point_size_in;\n");
          g_string_append (shader_state->source,
                           "  cogl_point_size_out = cogl_point_size_in;\n");
        }
    }
}

static gboolean
_cogl_pipeline_vertend_glsl_add_layer (CoglPipeline *pipeline,
                                       CoglPipelineLayer *layer,
                                       unsigned long layers_difference,
                                       CoglFramebuffer *framebuffer)
{
  CoglPipelineVertendShaderState *shader_state;
  CoglPipelineSnippetData snippet_data;
  int layer_index = layer->index;

  shader_state = get_shader_state (pipeline);

  if (shader_state->source == NULL)
    return TRUE;

  /* Transform the texture coordinates by the layer's user matrix.
   *
   * FIXME: this should avoid doing the transform if there is no user
   * matrix set. This might need a separate layer state flag for
   * whether there is a user matrix
   *
   * FIXME: we could be more clever here and try to detect if the
   * fragment program is going to use the texture coordinates and
   * avoid setting them if not
   */

  g_string_append_printf (shader_state->header,
                          "vec4\n"
                          "cogl_real_transform_layer%i (mat4 matrix, "
                          "vec4 tex_coord)\n"
                          "{\n"
                          "  return matrix * tex_coord;\n"
                          "}\n",
                          layer_index);

  /* Wrap the layer code in any snippets that have been hooked */
  memset (&snippet_data, 0, sizeof (snippet_data));
  snippet_data.snippets = get_layer_vertex_snippets (layer);
  snippet_data.hook = COGL_SNIPPET_HOOK_TEXTURE_COORD_TRANSFORM;
  snippet_data.chain_function = g_strdup_printf ("cogl_real_transform_layer%i",
                                                 layer_index);
  snippet_data.final_name = g_strdup_printf ("cogl_transform_layer%i",
                                             layer_index);
  snippet_data.function_prefix = g_strdup_printf ("cogl_transform_layer%i",
                                                  layer_index);
  snippet_data.return_type = "vec4";
  snippet_data.return_variable = "cogl_tex_coord";
  snippet_data.return_variable_is_argument = TRUE;
  snippet_data.arguments = "cogl_matrix, cogl_tex_coord";
  snippet_data.argument_declarations = "mat4 cogl_matrix, vec4 cogl_tex_coord";
  snippet_data.source_buf = shader_state->header;

  _cogl_pipeline_snippet_generate_code (&snippet_data);

  g_free ((char *) snippet_data.chain_function);
  g_free ((char *) snippet_data.final_name);
  g_free ((char *) snippet_data.function_prefix);

  g_string_append_printf (shader_state->source,
                          "  cogl_tex_coord%i_out = "
                          "cogl_transform_layer%i (cogl_texture_matrix%i,\n"
                          "                           "
                          "                        cogl_tex_coord%i_in);\n",
                          layer_index,
                          layer_index,
                          layer_index,
                          layer_index);

  return TRUE;
}

static gboolean
_cogl_pipeline_vertend_glsl_end (CoglPipeline *pipeline,
                                 unsigned long pipelines_difference)
{
  CoglPipelineVertendShaderState *shader_state;
  CoglContext *ctx = pipeline->context;

  shader_state = get_shader_state (pipeline);

  if (shader_state->source)
    {
      CoglDriver *driver = cogl_context_get_driver (ctx);
      const char *source_strings[2];
      GLint lengths[2];
      GLint compile_status;
      GLuint shader;
      CoglPipelineSnippetData snippet_data;
      CoglPipelineSnippetList *vertex_snippets;
      gboolean has_per_vertex_point_size =
        cogl_pipeline_get_per_vertex_point_size (pipeline);

      COGL_STATIC_COUNTER (vertend_glsl_compile_counter,
                           "glsl vertex compile counter",
                           "Increments each time a new GLSL "
                           "vertex shader is compiled",
                           0 /* no application private data */);
      COGL_COUNTER_INC (_cogl_uprof_context, vertend_glsl_compile_counter);

      g_string_append (shader_state->header,
                       "void\n"
                       "cogl_real_vertex_transform ()\n"
                       "{\n"
                       "  cogl_position_out = "
                       "cogl_modelview_projection_matrix * "
                       "cogl_position_in;\n"
                       "}\n");

      g_string_append (shader_state->source,
                       "  cogl_vertex_transform ();\n");

      if (has_per_vertex_point_size)
        {
          g_string_append (shader_state->header,
                           "void\n"
                           "cogl_real_point_size_calculation ()\n"
                           "{\n"
                           "  cogl_point_size_out = cogl_point_size_in;\n"
                           "}\n");
          g_string_append (shader_state->source,
                           "  cogl_point_size_calculation ();\n");
        }

      g_string_append (shader_state->source,
                       "  cogl_color_out = cogl_color_in;\n"
                       "}\n");

      vertex_snippets = get_vertex_snippets (pipeline);

      /* Add hooks for the vertex transform part */
      memset (&snippet_data, 0, sizeof (snippet_data));
      snippet_data.snippets = vertex_snippets;
      snippet_data.hook = COGL_SNIPPET_HOOK_VERTEX_TRANSFORM;
      snippet_data.chain_function = "cogl_real_vertex_transform";
      snippet_data.final_name = "cogl_vertex_transform";
      snippet_data.function_prefix = "cogl_vertex_transform";
      snippet_data.source_buf = shader_state->header;
      _cogl_pipeline_snippet_generate_code (&snippet_data);

      /* Add hooks for the point size calculation part */
      if (has_per_vertex_point_size)
        {
          memset (&snippet_data, 0, sizeof (snippet_data));
          snippet_data.snippets = vertex_snippets;
          snippet_data.hook = COGL_SNIPPET_HOOK_POINT_SIZE;
          snippet_data.chain_function = "cogl_real_point_size_calculation";
          snippet_data.final_name = "cogl_point_size_calculation";
          snippet_data.function_prefix = "cogl_point_size_calculation";
          snippet_data.source_buf = shader_state->header;
          _cogl_pipeline_snippet_generate_code (&snippet_data);
        }

      /* Add all of the hooks for vertex processing */
      memset (&snippet_data, 0, sizeof (snippet_data));
      snippet_data.snippets = vertex_snippets;
      snippet_data.hook = COGL_SNIPPET_HOOK_VERTEX;
      snippet_data.chain_function = "cogl_generated_source";
      snippet_data.final_name = "cogl_vertex_hook";
      snippet_data.function_prefix = "cogl_vertex_hook";
      snippet_data.source_buf = shader_state->source;
      _cogl_pipeline_snippet_generate_code (&snippet_data);

      g_string_append (shader_state->source,
                       "void\n"
                       "main ()\n"
                       "{\n"
                       "  cogl_vertex_hook ();\n");

      /* If there are any snippets then we can't rely on the
         projection matrix to flip the rendering for offscreen buffers
         so we'll need to flip it using an extra statement and a
         uniform */
      if (_cogl_pipeline_has_vertex_snippets (pipeline))
        {
          g_string_append (shader_state->header,
                           "uniform vec4 _cogl_flip_vector;\n");
          g_string_append (shader_state->source,
                           "  cogl_position_out *= _cogl_flip_vector;\n");
        }

      g_string_append (shader_state->source,
                       "}\n");

      GE_RET (shader, driver, glCreateShader (GL_VERTEX_SHADER));

      lengths[0] = shader_state->header->len;
      source_strings[0] = shader_state->header->str;
      lengths[1] = shader_state->source->len;
      source_strings[1] = shader_state->source->str;

      _cogl_glsl_shader_set_source_with_boilerplate (ctx,
                                                     shader, GL_VERTEX_SHADER,
                                                     pipeline,
                                                     2, /* count */
                                                     source_strings, lengths);

      GE (driver, glCompileShader (shader));
      GE (driver, glGetShaderiv (shader, GL_COMPILE_STATUS, &compile_status));

      if (!compile_status)
        {
          GLint len = 0;
          char *shader_log;

          GE (driver, glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &len));
          shader_log = g_alloca (len);
          GE (driver, glGetShaderInfoLog (shader, len, &len, shader_log));
          g_warning ("Shader compilation failed:\n%s", shader_log);
        }

      shader_state->header = NULL;
      shader_state->source = NULL;
      shader_state->gl_shader = shader;
    }

  return TRUE;
}

static void
_cogl_pipeline_vertend_glsl_pre_change_notify (CoglPipeline *pipeline,
                                               CoglPipelineState change,
                                               const CoglColor *new_color)
{
  CoglContext *ctx = pipeline->context;

  if ((change & _cogl_pipeline_get_state_for_vertex_codegen (ctx)))
    dirty_shader_state (pipeline);
}

/* NB: layers are considered immutable once they have any dependants
 * so although multiple pipelines can end up depending on a single
 * static layer, we can guarantee that if a layer is being *changed*
 * then it can only have one pipeline depending on it.
 *
 * XXX: Don't forget this is *pre* change, we can't read the new value
 * yet!
 */
static void
_cogl_pipeline_vertend_glsl_layer_pre_change_notify (
                                                CoglPipeline *owner,
                                                CoglPipelineLayer *layer,
                                                CoglPipelineLayerState change)
{
  CoglPipelineVertendShaderState *shader_state;

  shader_state = get_shader_state (owner);
  if (!shader_state)
    return;

  if ((change & COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN))
    {
      dirty_shader_state (owner);
      return;
    }

  /* TODO: we could be saving snippets of texture combine code along
   * with each layer and then when a layer changes we would just free
   * the snippet. */
}

const CoglPipelineVertend _cogl_pipeline_glsl_vertend =
  {
    _cogl_pipeline_vertend_glsl_start,
    _cogl_pipeline_vertend_glsl_add_layer,
    _cogl_pipeline_vertend_glsl_end,
    _cogl_pipeline_vertend_glsl_pre_change_notify,
    _cogl_pipeline_vertend_glsl_layer_pre_change_notify
  };
