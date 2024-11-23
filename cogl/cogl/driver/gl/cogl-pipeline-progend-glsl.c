/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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

#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-offscreen.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-opengl-private.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pipeline-cache.h"
#include "cogl/cogl-pipeline-state-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/driver/gl/cogl-pipeline-fragend-glsl-private.h"
#include "cogl/driver/gl/cogl-pipeline-vertend-glsl-private.h"
#include "cogl/driver/gl/cogl-pipeline-progend-glsl-private.h"
#include "deprecated/cogl-program-private.h"
#include "deprecated/cogl-shader-private.h"

/* These are used to generalise updating some uniforms that are
   required when building for drivers missing some fixed function
   state that we use */

typedef void (* UpdateUniformFunc) (CoglPipeline *pipeline,
                                    int uniform_location,
                                    void *getter_func);

static void update_float_uniform (CoglPipeline *pipeline,
                                  int uniform_location,
                                  void *getter_func);

typedef struct
{
  const char *uniform_name;
  void *getter_func;
  UpdateUniformFunc update_func;
  CoglPipelineState change;
} BuiltinUniformData;

static BuiltinUniformData builtin_uniforms[] =
  {
    { "cogl_point_size_in",
      cogl_pipeline_get_point_size, update_float_uniform,
      COGL_PIPELINE_STATE_POINT_SIZE },
    { "_cogl_alpha_test_ref",
      cogl_pipeline_get_alpha_test_reference, update_float_uniform,
      COGL_PIPELINE_STATE_ALPHA_FUNC_REFERENCE },
  };

const CoglPipelineProgend _cogl_pipeline_glsl_progend;

typedef struct _UnitState
{
  unsigned int dirty_combine_constant:1;
  unsigned int dirty_texture_matrix:1;

  GLint combine_constant_uniform;

  GLint texture_matrix_uniform;
} UnitState;

typedef struct
{
  unsigned int ref_count;

  /* Age that the user program had last time we generated a GL
     program. If it's different then we need to relink the program */
  unsigned int user_program_age;

  GLuint program;

  unsigned long dirty_builtin_uniforms;
  GLint builtin_uniform_locations[G_N_ELEMENTS (builtin_uniforms)];

  GLint modelview_uniform;
  GLint projection_uniform;
  GLint mvp_uniform;

  CoglMatrixEntryCache projection_cache;
  CoglMatrixEntryCache modelview_cache;

  /* We need to track the last pipeline that the program was used with
   * so know if we need to update all of the uniforms */
  CoglPipeline *last_used_for_pipeline;

  /* Array of GL uniform locations indexed by Cogl's uniform
     location. We are careful only to allocated this array if a custom
     uniform is actually set */
  GArray *uniform_locations;

  /* Array of attribute locations. */
  GArray *attribute_locations;

  /* The 'flip' uniform is used to flip the geometry upside-down when
     the framebuffer requires it only when there are vertex
     snippets. Otherwise this is achieved using the projection
     matrix */
  GLint flip_uniform;
  int flushed_flip_state;

  UnitState *unit_state;

  CoglPipelineCacheEntry *cache_entry;
} CoglPipelineProgramState;

static GQuark program_state_key = 0;

typedef struct
{
  CoglPipelineProgramState *program_state;
  CoglPipeline *instance;
} CoglPipelineProgramStateCache;

static GQuark
get_cache_key (void)
{
  if (G_UNLIKELY (program_state_key == 0))
    program_state_key = g_quark_from_static_string ("program-state-progend-key");

  return program_state_key;
}

static CoglPipelineProgramState *
get_program_state (CoglPipeline *pipeline)
{
  CoglPipelineProgramStateCache *cache;
  cache = g_object_get_qdata (G_OBJECT (pipeline), get_cache_key ());
  if (cache)
    return cache->program_state;
  return NULL;
}

#define UNIFORM_LOCATION_UNKNOWN -2

#define ATTRIBUTE_LOCATION_UNKNOWN -2

/* Under GLES2 the vertex attribute API needs to query the attribute
   numbers because it can't used the fixed function API to set the
   builtin attributes. We cache the attributes here because the
   progend knows when the program is changed so it can clear the
   cache. This should always be called after the pipeline is flushed
   so they can assert that the gl program is valid */

/* All attributes names get internally mapped to a global set of
 * sequential indices when they are setup which we need to need to
 * then be able to map to a GL attribute location once we have
 * a linked GLSL program */

int
_cogl_pipeline_progend_glsl_get_attrib_location (CoglPipeline *pipeline,
                                                 int name_index)
{
  CoglPipelineProgramState *program_state = get_program_state (pipeline);
  int *locations;
  CoglContext *ctx = pipeline->context;

  g_return_val_if_fail (program_state != NULL, -1);
  g_return_val_if_fail (program_state->program != 0, -1);

  if (G_UNLIKELY (program_state->attribute_locations == NULL))
    program_state->attribute_locations =
      g_array_new (FALSE, FALSE, sizeof (int));

  if (G_UNLIKELY (program_state->attribute_locations->len <= name_index))
    {
      int i = program_state->attribute_locations->len;
      g_array_set_size (program_state->attribute_locations, name_index + 1);
      for (; i < program_state->attribute_locations->len; i++)
        g_array_index (program_state->attribute_locations, int, i)
          = ATTRIBUTE_LOCATION_UNKNOWN;
    }

  locations = &g_array_index (program_state->attribute_locations, int, 0);

  if (locations[name_index] == ATTRIBUTE_LOCATION_UNKNOWN)
    {
      CoglAttributeNameState *name_state =
        g_array_index (ctx->attribute_name_index_map,
                       CoglAttributeNameState *, name_index);

      g_return_val_if_fail (name_state != NULL, 0);

      GE_RET( locations[name_index],
              ctx, glGetAttribLocation (program_state->program,
                                        name_state->name) );
    }

  return locations[name_index];
}

static void
clear_attribute_cache (CoglPipelineProgramState *program_state)
{
  if (program_state->attribute_locations)
    {
      g_array_free (program_state->attribute_locations, TRUE);
      program_state->attribute_locations = NULL;
    }
}

static void
clear_flushed_matrix_stacks (CoglPipelineProgramState *program_state)
{
  _cogl_matrix_entry_cache_destroy (&program_state->projection_cache);
  _cogl_matrix_entry_cache_init (&program_state->projection_cache);
  _cogl_matrix_entry_cache_destroy (&program_state->modelview_cache);
  _cogl_matrix_entry_cache_init (&program_state->modelview_cache);
}

static CoglPipelineProgramState *
program_state_new (int n_layers,
                   CoglPipelineCacheEntry *cache_entry)
{
  CoglPipelineProgramState *program_state;

  program_state = g_new0 (CoglPipelineProgramState, 1);
  program_state->ref_count = 1;
  program_state->program = 0;
  program_state->unit_state = g_new (UnitState, n_layers);
  program_state->uniform_locations = NULL;
  program_state->attribute_locations = NULL;
  program_state->cache_entry = cache_entry;
  _cogl_matrix_entry_cache_init (&program_state->modelview_cache);
  _cogl_matrix_entry_cache_init (&program_state->projection_cache);

  return program_state;
}

static void
destroy_program_state (void *user_data)
{
  CoglPipelineProgramStateCache *cache = user_data;
  CoglPipelineProgramState *program_state = cache->program_state;
  CoglContext *ctx = cache->instance->context;

  /* If the program state was last used for this pipeline then clear
     it so that if same address gets used again for a new pipeline
     then we won't think it's the same pipeline and avoid updating the
     uniforms */
  if (program_state->last_used_for_pipeline == cache->instance)
    program_state->last_used_for_pipeline = NULL;

  if (program_state->cache_entry &&
      program_state->cache_entry->pipeline != cache->instance)
    program_state->cache_entry->usage_count--;

  if (--program_state->ref_count == 0)
    {
      clear_attribute_cache (program_state);

      _cogl_matrix_entry_cache_destroy (&program_state->projection_cache);
      _cogl_matrix_entry_cache_destroy (&program_state->modelview_cache);

      if (program_state->program)
        GE( ctx, glDeleteProgram (program_state->program) );

      g_free (program_state->unit_state);

      if (program_state->uniform_locations)
        g_array_free (program_state->uniform_locations, TRUE);

      g_free (program_state);
    }

  g_free (cache);
}

static void
set_program_state (CoglPipeline *pipeline,
                  CoglPipelineProgramState *program_state)
{
  if (program_state)
    {
      program_state->ref_count++;

      /* If we're not setting the state on the template pipeline then
       * mark it as a usage of the pipeline cache entry */
      if (program_state->cache_entry &&
          program_state->cache_entry->pipeline != pipeline)
        program_state->cache_entry->usage_count++;
    }

  CoglPipelineProgramStateCache *cache = g_new0 (CoglPipelineProgramStateCache, 1);
  cache->instance = pipeline;
  cache->program_state = program_state;

  g_object_set_qdata_full (G_OBJECT (pipeline),
                           get_cache_key (),
                           cache,
                           destroy_program_state);
}

static void
dirty_program_state (CoglPipeline *pipeline)
{
  g_object_set_qdata_full (G_OBJECT (pipeline),
                           get_cache_key (),
                           NULL,
                           NULL);
}

static void
link_program (CoglContext *ctx,
              GLint        gl_program)
{
  GLint link_status;

  GE( ctx, glLinkProgram (gl_program) );

  GE( ctx, glGetProgramiv (gl_program, GL_LINK_STATUS, &link_status) );

  if (!link_status)
    {
      GLint log_length;
      GLsizei out_log_length;
      char *log;

      GE( ctx, glGetProgramiv (gl_program, GL_INFO_LOG_LENGTH, &log_length) );

      log = g_malloc (log_length);

      GE( ctx, glGetProgramInfoLog (gl_program, log_length,
                                    &out_log_length, log) );

      g_warning ("Failed to link GLSL program:\n%.*s\n",
                 log_length, log);

      g_free (log);
    }
}

typedef struct
{
  int unit;
  GLuint gl_program;
  gboolean update_all;
  CoglPipelineProgramState *program_state;
} UpdateUniformsState;

static gboolean
get_uniform_cb (CoglPipeline *pipeline,
                int layer_index,
                void *user_data)
{
  UpdateUniformsState *state = user_data;
  CoglPipelineProgramState *program_state = state->program_state;
  UnitState *unit_state = &program_state->unit_state[state->unit];
  GLint uniform_location;
  CoglContext *ctx = pipeline->context;

  /* We can reuse the source buffer to create the uniform name because
     the program has now been linked */
  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "cogl_sampler%i", layer_index);

  GE_RET( uniform_location,
          ctx, glGetUniformLocation (state->gl_program,
                                     ctx->codegen_source_buffer->str) );

  /* We can set the uniform immediately because the samplers are the
     unit index not the texture object number so it will never
     change. Unfortunately GL won't let us use a constant instead of a
     uniform */
  if (uniform_location != -1)
    GE( ctx, glUniform1i (uniform_location, state->unit) );

  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "_cogl_layer_constant_%i", layer_index);

  GE_RET( uniform_location,
          ctx, glGetUniformLocation (state->gl_program,
                                     ctx->codegen_source_buffer->str) );

  unit_state->combine_constant_uniform = uniform_location;

  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "cogl_texture_matrix[%i]", layer_index);

  GE_RET( uniform_location,
          ctx, glGetUniformLocation (state->gl_program,
                                     ctx->codegen_source_buffer->str) );

  unit_state->texture_matrix_uniform = uniform_location;

  state->unit++;

  return TRUE;
}

static gboolean
update_constants_cb (CoglPipeline *pipeline,
                     int layer_index,
                     void *user_data)
{
  UpdateUniformsState *state = user_data;
  CoglPipelineProgramState *program_state = state->program_state;
  UnitState *unit_state = &program_state->unit_state[state->unit++];
  CoglContext *ctx = pipeline->context;

  if (unit_state->combine_constant_uniform != -1 &&
      (state->update_all || unit_state->dirty_combine_constant))
    {
      float constant[4];
      _cogl_pipeline_get_layer_combine_constant (pipeline,
                                                 layer_index,
                                                 constant);
      GE (ctx, glUniform4fv (unit_state->combine_constant_uniform,
                             1, constant));
      unit_state->dirty_combine_constant = FALSE;
    }

  if (unit_state->texture_matrix_uniform != -1 &&
      (state->update_all || unit_state->dirty_texture_matrix))
    {
      const graphene_matrix_t *matrix;
      float array[16];

      matrix = _cogl_pipeline_get_layer_matrix (pipeline, layer_index);
      graphene_matrix_to_float (matrix, array);
      GE (ctx, glUniformMatrix4fv (unit_state->texture_matrix_uniform,
                                   1, FALSE, array));
      unit_state->dirty_texture_matrix = FALSE;
    }

  return TRUE;
}

static void
update_builtin_uniforms (CoglContext *context,
                         CoglPipeline *pipeline,
                         GLuint gl_program,
                         CoglPipelineProgramState *program_state)
{
  int i;

  if (program_state->dirty_builtin_uniforms == 0)
    return;

  for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
    if ((program_state->dirty_builtin_uniforms & (1 << i)) &&
        program_state->builtin_uniform_locations[i] != -1)
      builtin_uniforms[i].update_func (pipeline,
                                       program_state
                                       ->builtin_uniform_locations[i],
                                       builtin_uniforms[i].getter_func);

  program_state->dirty_builtin_uniforms = 0;
}

typedef struct
{
  CoglPipelineProgramState *program_state;
  unsigned long *uniform_differences;
  int n_differences;
  CoglContext *ctx;
  const CoglBoxedValue *values;
  int value_index;
} FlushUniformsClosure;

static gboolean
flush_uniform_cb (int uniform_num, void *user_data)
{
  FlushUniformsClosure *data = user_data;

  if (COGL_FLAGS_GET (data->uniform_differences, uniform_num))
    {
      GArray *uniform_locations;
      GLint uniform_location;

      if (data->program_state->uniform_locations == NULL)
        data->program_state->uniform_locations =
          g_array_new (FALSE, FALSE, sizeof (GLint));

      uniform_locations = data->program_state->uniform_locations;

      if (uniform_locations->len <= uniform_num)
        {
          unsigned int old_len = uniform_locations->len;

          g_array_set_size (uniform_locations, uniform_num + 1);

          while (old_len <= uniform_num)
            {
              g_array_index (uniform_locations, GLint, old_len) =
                UNIFORM_LOCATION_UNKNOWN;
              old_len++;
            }
        }

      uniform_location = g_array_index (uniform_locations, GLint, uniform_num);

      if (uniform_location == UNIFORM_LOCATION_UNKNOWN)
        {
          const char *uniform_name =
            g_ptr_array_index (data->ctx->uniform_names, uniform_num);

          uniform_location =
            data->ctx->glGetUniformLocation (data->program_state->program,
                                             uniform_name);
          g_array_index (uniform_locations, GLint, uniform_num) =
            uniform_location;
        }

      if (uniform_location != -1)
        _cogl_boxed_value_set_uniform (data->ctx,
                                       uniform_location,
                                       data->values + data->value_index);

      data->n_differences--;
      COGL_FLAGS_SET (data->uniform_differences, uniform_num, FALSE);
    }

  data->value_index++;

  return data->n_differences > 0;
}

static void
_cogl_pipeline_progend_glsl_flush_uniforms (CoglPipeline *pipeline,
                                            CoglPipelineProgramState *
                                                                  program_state,
                                            GLuint gl_program,
                                            gboolean program_changed)
{
  CoglPipelineUniformsState *uniforms_state;
  FlushUniformsClosure data;
  int n_uniform_longs;
  CoglContext *ctx = pipeline->context;

  if (pipeline->differences & COGL_PIPELINE_STATE_UNIFORMS)
    uniforms_state = &pipeline->big_state->uniforms_state;
  else
    uniforms_state = NULL;

  data.program_state = program_state;
  data.ctx = ctx;

  n_uniform_longs = COGL_FLAGS_N_LONGS_FOR_SIZE (ctx->n_uniform_names);

  data.uniform_differences = g_newa (unsigned long, n_uniform_longs);

  /* Try to find a common ancestor for the values that were already
     flushed on the pipeline that this program state was last used for
     so we can avoid flushing those */

  if (program_changed || program_state->last_used_for_pipeline == NULL)
    {
      if (program_changed)
        {
          /* The program has changed so all of the uniform locations
             are invalid */
          if (program_state->uniform_locations)
            g_array_set_size (program_state->uniform_locations, 0);
        }

      /* We need to flush everything so mark all of the uniforms as
         dirty */
      memset (data.uniform_differences, 0xff,
              n_uniform_longs * sizeof (unsigned long));
      data.n_differences = G_MAXINT;
    }
  else if (program_state->last_used_for_pipeline)
    {
      int i;

      memset (data.uniform_differences, 0,
              n_uniform_longs * sizeof (unsigned long));
      _cogl_pipeline_compare_uniform_differences
        (data.uniform_differences,
         program_state->last_used_for_pipeline,
         pipeline);

      /* We need to be sure to flush any uniforms that have changed
         since the last flush */
      if (uniforms_state)
        _cogl_bitmask_set_flags (&uniforms_state->changed_mask,
                                 data.uniform_differences);

      /* Count the number of differences. This is so we can stop early
         when we've flushed all of them */
      data.n_differences = 0;

      for (i = 0; i < n_uniform_longs; i++)
        data.n_differences +=
          __builtin_popcountl (data.uniform_differences[i]);
    }

  while (pipeline && data.n_differences > 0)
    {
      if (pipeline->differences & COGL_PIPELINE_STATE_UNIFORMS)
        {
          const CoglPipelineUniformsState *parent_uniforms_state =
            &pipeline->big_state->uniforms_state;

          data.values = parent_uniforms_state->override_values;
          data.value_index = 0;

          _cogl_bitmask_foreach (&parent_uniforms_state->override_mask,
                                 flush_uniform_cb,
                                 &data);
        }

      pipeline = _cogl_pipeline_get_parent (pipeline);
    }

  if (uniforms_state)
    _cogl_bitmask_clear_all (&uniforms_state->changed_mask);
}

static gboolean
_cogl_pipeline_progend_glsl_start (CoglPipeline *pipeline)
{
  return TRUE;
}

static CoglPipelineSnippetList *
get_fragment_snippets (CoglPipeline *pipeline)
{
  pipeline =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_FRAGMENT_SNIPPETS);

  return &pipeline->big_state->fragment_snippets;
}

static CoglPipelineSnippetList *
get_vertex_snippets (CoglPipeline *pipeline)
{
  pipeline =
    _cogl_pipeline_get_authority (pipeline,
                                  COGL_PIPELINE_STATE_VERTEX_SNIPPETS);

  return &pipeline->big_state->vertex_snippets;
}

static gboolean
needs_recompile (CoglShader   *shader,
                 CoglPipeline *pipeline,
                 CoglPipeline *prev)
{
  /* XXX: currently the only things that will affect the
   * boilerplate for user shaders, apart from driver features,
   * are the pipeline layer-indices, texture-unit-indices and
   * snippets
   */

  if (pipeline == prev)
    return FALSE;

  if (!_cogl_pipeline_layer_and_unit_numbers_equal (prev, pipeline))
    return TRUE;

  switch (shader->type)
    {
    case COGL_SHADER_TYPE_VERTEX:
      if (!_cogl_pipeline_vertex_snippets_state_equal (prev, pipeline))
        return TRUE;
      break;
    case COGL_SHADER_TYPE_FRAGMENT:
      if (!_cogl_pipeline_fragment_snippets_state_equal (prev, pipeline))
        return TRUE;
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  return FALSE;
}

static void
_cogl_shader_compile_real (CoglShader   *shader,
                           CoglPipeline *pipeline)
{
  g_autoptr(GString) hooks_source = NULL;
  CoglPipelineSnippetData snippet_data;
  const char *shader_sources[4];
  GLenum gl_type;
  GLint status;
  CoglContext *ctx = pipeline->context;

  if (shader->gl_handle)
    {
      if (!needs_recompile (shader, pipeline, shader->compilation_pipeline))
        return;

      GE (ctx, glDeleteShader (shader->gl_handle));
      shader->gl_handle = 0;

      if (shader->compilation_pipeline)
        {
          g_object_unref (shader->compilation_pipeline);
          shader->compilation_pipeline = NULL;
        }
    }

  hooks_source = g_string_new ("");
  memset (&snippet_data, 0, sizeof (snippet_data));
  snippet_data.chain_function = "cogl_main";
  snippet_data.final_name = "cogl_hooks";
  snippet_data.source_buf = hooks_source;

  switch (shader->type)
    {
    case COGL_SHADER_TYPE_VERTEX:
      gl_type = GL_VERTEX_SHADER;
      snippet_data.snippets = get_vertex_snippets (pipeline);
      snippet_data.hook = COGL_SNIPPET_HOOK_VERTEX;
      snippet_data.function_prefix = "cogl_vertex_hook";
      break;
    case COGL_SHADER_TYPE_FRAGMENT:
      gl_type = GL_FRAGMENT_SHADER;
      snippet_data.snippets = get_fragment_snippets (pipeline);
      snippet_data.hook = COGL_SNIPPET_HOOK_FRAGMENT;
      snippet_data.function_prefix = "cogl_fragment_hook";
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  _cogl_pipeline_snippet_generate_code (&snippet_data);

  shader_sources[0] = "#define main cogl_main\n";
  shader_sources[1] = shader->source;
  shader_sources[2] = hooks_source->str;
  shader_sources[3] = "#undef main\n"
                      "void main () { cogl_hooks(); }\n";

  shader->gl_handle = ctx->glCreateShader (gl_type);

  _cogl_glsl_shader_set_source_with_boilerplate (ctx,
                                                 shader->gl_handle,
                                                 gl_type,
                                                 pipeline,
                                                 G_N_ELEMENTS (shader_sources),
                                                 shader_sources,
                                                 NULL);
  GE (ctx, glCompileShader (shader->gl_handle));

  shader->compilation_pipeline = g_object_ref (pipeline);

  GE (ctx, glGetShaderiv (shader->gl_handle, GL_COMPILE_STATUS, &status));
  if (!status)
    {
      char buffer[512];
      int len = 0;

      ctx->glGetShaderInfoLog (shader->gl_handle, 511, &len, buffer);
      buffer[len] = '\0';

      g_warning ("Failed to compile GLSL program:\n"
                 "src:\n%s\n"
                 "error:\n%s\n",
                 shader->source,
                 buffer);
    }
}

static void
_cogl_pipeline_progend_glsl_end (CoglPipeline *pipeline,
                                 unsigned long pipelines_difference)
{
  CoglPipelineProgramState *program_state;
  GLuint gl_program;
  gboolean program_changed = FALSE;
  UpdateUniformsState state;
  CoglProgram *user_program;
  CoglPipelineCacheEntry *cache_entry = NULL;
  CoglContext *ctx = pipeline->context;

  program_state = get_program_state (pipeline);

  user_program = cogl_pipeline_get_user_program (pipeline);

  if (program_state == NULL)
    {
      CoglPipeline *authority;

      /* Get the authority for anything affecting program state. This
         should include both fragment codegen state and vertex codegen
         state */
      authority = _cogl_pipeline_find_equivalent_parent
        (pipeline,
         (_cogl_pipeline_get_state_for_vertex_codegen (ctx) |
          _cogl_pipeline_get_state_for_fragment_codegen (ctx)) &
         ~COGL_PIPELINE_STATE_LAYERS,
         _cogl_pipeline_get_layer_state_for_fragment_codegen (ctx) |
         COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN);

      program_state = get_program_state (authority);

      if (program_state == NULL)
        {
          /* Check if there is already a similar cached pipeline whose
             program state we can share */
          if (G_LIKELY (!(COGL_DEBUG_ENABLED
                          (COGL_DEBUG_DISABLE_PROGRAM_CACHES))))
            {
              cache_entry =
                _cogl_pipeline_cache_get_combined_template (ctx->pipeline_cache,
                                                            authority);

              program_state = get_program_state (cache_entry->pipeline);
            }

          if (program_state)
            program_state->ref_count++;
          else
            program_state
              = program_state_new (cogl_pipeline_get_n_layers (authority),
                                   cache_entry);

          set_program_state (authority, program_state);

          program_state->ref_count--;

          if (cache_entry)
            set_program_state (cache_entry->pipeline, program_state);
        }

      if (authority != pipeline)
        set_program_state (pipeline, program_state);
    }

  /* If the program has changed since the last link then we do
   * need to relink */
  if (program_state->program && user_program &&
       user_program->age != program_state->user_program_age)
    {
      GE( ctx, glDeleteProgram (program_state->program) );
      program_state->program = 0;
    }

  if (program_state->program == 0)
    {
      GLuint backend_shader;
      GSList *l;

      GE_RET( program_state->program, ctx, glCreateProgram () );

      /* Attach all of the shader from the user program */
      if (user_program)
        {
          for (l = user_program->attached_shaders; l; l = l->next)
            {
              CoglShader *shader = l->data;

              _cogl_shader_compile_real (shader, pipeline);

              GE( ctx, glAttachShader (program_state->program,
                                       shader->gl_handle) );
            }

          program_state->user_program_age = user_program->age;
        }

      /* Attach any shaders from the GLSL backends */
      if ((backend_shader = _cogl_pipeline_fragend_glsl_get_shader (pipeline)))
        GE( ctx, glAttachShader (program_state->program, backend_shader) );
      if ((backend_shader = _cogl_pipeline_vertend_glsl_get_shader (pipeline)))
        GE( ctx, glAttachShader (program_state->program, backend_shader) );

      /* XXX: OpenGL as a special case requires the vertex position to
       * be bound to generic attribute 0 so for simplicity we
       * unconditionally bind the cogl_position_in attribute here...
       */
      GE( ctx, glBindAttribLocation (program_state->program,
                                     0, "cogl_position_in"));

      link_program (ctx, program_state->program);

      program_changed = TRUE;
    }

  gl_program = program_state->program;

  if (ctx->current_gl_program != gl_program)
    {
      _cogl_gl_util_clear_gl_errors (ctx);
      ctx->glUseProgram (gl_program);
      if (_cogl_gl_util_get_error (ctx) == GL_NO_ERROR)
        ctx->current_gl_program = gl_program;
      else
        {
          GE( ctx, glUseProgram (0) );
          ctx->current_gl_program = 0;
        }
    }

  state.unit = 0;
  state.gl_program = gl_program;
  state.program_state = program_state;

  if (program_changed)
    {
      cogl_pipeline_foreach_layer (pipeline,
                                   get_uniform_cb,
                                   &state);
      clear_attribute_cache (program_state);

      GE_RET (program_state->flip_uniform,
              ctx, glGetUniformLocation (gl_program, "_cogl_flip_vector"));
      program_state->flushed_flip_state = -1;
    }

  state.unit = 0;
  state.update_all = (program_changed ||
                      program_state->last_used_for_pipeline != pipeline);

  cogl_pipeline_foreach_layer (pipeline,
                               update_constants_cb,
                               &state);

  if (program_changed)
    {
      int i;

      clear_flushed_matrix_stacks (program_state);

      for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
        GE_RET( program_state->builtin_uniform_locations[i], ctx,
                glGetUniformLocation (gl_program,
                                      builtin_uniforms[i].uniform_name) );

      GE_RET( program_state->modelview_uniform, ctx,
              glGetUniformLocation (gl_program,
                                    "cogl_modelview_matrix") );

      GE_RET( program_state->projection_uniform, ctx,
              glGetUniformLocation (gl_program,
                                    "cogl_projection_matrix") );

      GE_RET( program_state->mvp_uniform, ctx,
              glGetUniformLocation (gl_program,
                                    "cogl_modelview_projection_matrix") );
    }

  if (program_changed ||
      program_state->last_used_for_pipeline != pipeline)
    program_state->dirty_builtin_uniforms = ~(unsigned long) 0;

  update_builtin_uniforms (ctx, pipeline, gl_program, program_state);

  _cogl_pipeline_progend_glsl_flush_uniforms (pipeline,
                                              program_state,
                                              gl_program,
                                              program_changed);

  if (user_program)
    _cogl_program_flush_uniforms (ctx, user_program,
                                  gl_program,
                                  program_changed);

  /* We need to track the last pipeline that the program was used with
   * so know if we need to update all of the uniforms */
  program_state->last_used_for_pipeline = pipeline;
}

static void
_cogl_pipeline_progend_glsl_pre_change_notify (CoglPipeline *pipeline,
                                               CoglPipelineState change,
                                               const CoglColor *new_color)
{
  CoglContext *ctx = pipeline->context;

  if ((change & (_cogl_pipeline_get_state_for_vertex_codegen (ctx) |
                 _cogl_pipeline_get_state_for_fragment_codegen (ctx))))
    {
      dirty_program_state (pipeline);
    }
  else
    {
      int i;

      for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
        if (change & builtin_uniforms[i].change)
          {
            CoglPipelineProgramState *program_state
              = get_program_state (pipeline);
            if (program_state)
              program_state->dirty_builtin_uniforms |= 1 << i;
            return;
          }
    }
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
_cogl_pipeline_progend_glsl_layer_pre_change_notify (
                                                CoglPipeline *owner,
                                                CoglPipelineLayer *layer,
                                                CoglPipelineLayerState change)
{
  CoglTextureUnit *unit;
  CoglContext *ctx = owner->context;

  if ((change & (_cogl_pipeline_get_layer_state_for_fragment_codegen (ctx) |
                 COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN)))
    {
      dirty_program_state (owner);
    }
  else if (change & COGL_PIPELINE_LAYER_STATE_COMBINE_CONSTANT)
    {
      CoglPipelineProgramState *program_state = get_program_state (owner);
      if (program_state)
        {
          int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
          program_state->unit_state[unit_index].dirty_combine_constant = TRUE;
        }
    }
  else if (change & COGL_PIPELINE_LAYER_STATE_USER_MATRIX)
    {
      CoglPipelineProgramState *program_state = get_program_state (owner);
      if (program_state)
        {
          int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
          program_state->unit_state[unit_index].dirty_texture_matrix = TRUE;
        }
    }

  /* If the layer being changed is the same as the last layer we
   * flushed to the corresponding texture unit then we keep a track of
   * the changes so we can try to minimize redundant OpenGL calls if
   * the same layer is flushed again.
   */
  unit = _cogl_get_texture_unit (ctx, _cogl_pipeline_layer_get_unit_index (layer));
  if (unit->layer == layer)
    unit->layer_changes_since_flush |= change;
}

static void
_cogl_pipeline_progend_glsl_pre_paint (CoglPipeline *pipeline,
                                       CoglFramebuffer *framebuffer)
{
  gboolean needs_flip;
  CoglMatrixEntry *projection_entry;
  CoglMatrixEntry *modelview_entry;
  CoglPipelineProgramState *program_state;
  gboolean modelview_changed;
  gboolean projection_changed;
  gboolean need_modelview;
  gboolean need_projection;
  graphene_matrix_t modelview, projection;
  CoglContext *ctx = pipeline->context;

  program_state = get_program_state (pipeline);

  projection_entry = ctx->current_projection_entry;
  modelview_entry = ctx->current_modelview_entry;

  /* An initial pipeline is flushed while creating the context. At
     this point there are no matrices selected so we can't do
     anything */
  if (modelview_entry == NULL || projection_entry == NULL)
    return;

  needs_flip = cogl_framebuffer_is_y_flipped (ctx->current_draw_buffer);

  projection_changed =
    _cogl_matrix_entry_cache_maybe_update (&program_state->projection_cache,
                                           projection_entry,
                                           (needs_flip &&
                                            program_state->flip_uniform ==
                                            -1));

  modelview_changed =
    _cogl_matrix_entry_cache_maybe_update (&program_state->modelview_cache,
                                           modelview_entry,
                                           /* never flip modelview */
                                           FALSE);

  if (modelview_changed || projection_changed)
    {
      float v[16];

      if (program_state->mvp_uniform != -1)
        need_modelview = need_projection = TRUE;
      else
        {
          need_projection = (program_state->projection_uniform != -1 &&
                             projection_changed);
          need_modelview = (program_state->modelview_uniform != -1 &&
                            modelview_changed);
        }

      if (need_modelview)
        cogl_matrix_entry_get (modelview_entry, &modelview);
      if (need_projection)
        {
          if (needs_flip && program_state->flip_uniform == -1)
            {
              graphene_matrix_t tmp_matrix;
              cogl_matrix_entry_get (projection_entry, &tmp_matrix);
              graphene_matrix_multiply (&tmp_matrix,
                                        &ctx->y_flip_matrix,
                                        &projection);
            }
          else
            cogl_matrix_entry_get (projection_entry, &projection);
        }

      if (projection_changed && program_state->projection_uniform != -1)
        {
          graphene_matrix_to_float (&projection, v);
          GE (ctx, glUniformMatrix4fv (program_state->projection_uniform,
                                       1, /* count */
                                       FALSE, /* transpose */
                                       v));
        }

      if (modelview_changed && program_state->modelview_uniform != -1)
        {
          graphene_matrix_to_float (&modelview,v);
          GE (ctx, glUniformMatrix4fv (program_state->modelview_uniform,
                                       1, /* count */
                                       FALSE, /* transpose */
                                       v));
        }

      if (program_state->mvp_uniform != -1)
        {
          /* The journal usually uses an identity matrix for the
             modelview so we can optimise this common case by
             avoiding the matrix multiplication */
          if (cogl_matrix_entry_is_identity (modelview_entry))
            {
              graphene_matrix_to_float (&projection, v);
              GE (ctx,
                  glUniformMatrix4fv (program_state->mvp_uniform,
                                      1, /* count */
                                      FALSE, /* transpose */
                                      v));
            }
          else
            {
              graphene_matrix_t combined;

              graphene_matrix_multiply (&modelview, &projection, &combined);
              graphene_matrix_to_float (&combined, v);

              GE (ctx,
                  glUniformMatrix4fv (program_state->mvp_uniform,
                                      1, /* count */
                                      FALSE, /* transpose */
                                      v));
            }
        }
    }

  if (program_state->flip_uniform != -1
      && program_state->flushed_flip_state != needs_flip)
    {
      static const float do_flip[4] = { 1.0f, -1.0f, 1.0f, 1.0f };
      static const float dont_flip[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      GE( ctx, glUniform4fv (program_state->flip_uniform,
                             1, /* count */
                             needs_flip ? do_flip : dont_flip) );
      program_state->flushed_flip_state = needs_flip;
    }
}

static void
update_float_uniform (CoglPipeline *pipeline,
                      int uniform_location,
                      void *getter_func)
{
  float (* float_getter_func) (CoglPipeline *) = getter_func;
  float value;
  CoglContext *ctx = pipeline->context;

  value = float_getter_func (pipeline);
  GE( ctx, glUniform1f (uniform_location, value) );
}

const CoglPipelineProgend _cogl_pipeline_glsl_progend =
  {
    _cogl_pipeline_progend_glsl_start,
    _cogl_pipeline_progend_glsl_end,
    _cogl_pipeline_progend_glsl_pre_change_notify,
    _cogl_pipeline_progend_glsl_layer_pre_change_notify,
    _cogl_pipeline_progend_glsl_pre_paint
  };
