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
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-journal-private.h"
#include "cogl/cogl-attribute.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-pipeline-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-indices-private.h"
#include "cogl/cogl-private.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

G_DEFINE_FINAL_TYPE (CoglAttribute, cogl_attribute, G_TYPE_OBJECT);

static void
cogl_attribute_dispose (GObject *object)
{
  CoglAttribute *attribute = COGL_ATTRIBUTE (object);

  g_clear_object (&attribute->attribute_buffer);

  G_OBJECT_CLASS (cogl_attribute_parent_class)->dispose (object);
}

static void
cogl_attribute_init (CoglAttribute *attribute)
{
}

static void
cogl_attribute_class_init (CoglAttributeClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_attribute_dispose;
}

static gboolean
validate_cogl_attribute_name (const char *name,
                              const char **real_attribute_name,
                              CoglAttributeNameID *name_id,
                              gboolean *normalized,
                              int *layer_number)
{
  name = name + 5; /* skip "cogl_" */

  *normalized = FALSE;
  *layer_number = 0;

  if (strcmp (name, "position_in") == 0)
    *name_id = COGL_ATTRIBUTE_NAME_ID_POSITION_ARRAY;
  else if (strcmp (name, "color_in") == 0)
    {
      *name_id = COGL_ATTRIBUTE_NAME_ID_COLOR_ARRAY;
      *normalized = TRUE;
    }
  else if (strcmp (name, "tex_coord_in") == 0)
    {
      *real_attribute_name = "cogl_tex_coord0_in";
      *name_id = COGL_ATTRIBUTE_NAME_ID_TEXTURE_COORD_ARRAY;
    }
  else if (strncmp (name, "tex_coord", strlen ("tex_coord")) == 0)
    {
      char *endptr;
      *layer_number = strtoul (name + 9, &endptr, 10);
      if (strcmp (endptr, "_in") != 0)
	{
	  g_warning ("Texture coordinate attributes should either be named "
                     "\"cogl_tex_coord_in\" or named with a texture unit index "
                     "like \"cogl_tex_coord2_in\"\n");
          return FALSE;
	}
      *name_id = COGL_ATTRIBUTE_NAME_ID_TEXTURE_COORD_ARRAY;
    }
  else if (strcmp (name, "normal_in") == 0)
    {
      *name_id = COGL_ATTRIBUTE_NAME_ID_NORMAL_ARRAY;
      *normalized = TRUE;
    }
  else if (strcmp (name, "point_size_in") == 0)
    *name_id = COGL_ATTRIBUTE_NAME_ID_POINT_SIZE_ARRAY;
  else
    {
      g_warning ("Unknown cogl_* attribute name cogl_%s\n", name);
      return FALSE;
    }

  return TRUE;
}

CoglAttributeNameState *
_cogl_attribute_register_attribute_name (CoglContext *context,
                                         const char *name)
{
  CoglAttributeNameState *name_state = g_new (CoglAttributeNameState, 1);
  int name_index = context->n_attribute_names++;
  char *name_copy = g_strdup (name);

  name_state->name = NULL;
  name_state->name_index = name_index;
  if (strncmp (name, "cogl_", 5) == 0)
    {
      if (!validate_cogl_attribute_name (name,
                                         &name_state->name,
                                         &name_state->name_id,
                                         &name_state->normalized_default,
                                         &name_state->layer_number))
        goto error;
    }
  else
    {
      name_state->name_id = COGL_ATTRIBUTE_NAME_ID_CUSTOM_ARRAY;
      name_state->normalized_default = FALSE;
      name_state->layer_number = 0;
    }

  if (name_state->name == NULL)
    name_state->name = name_copy;

  g_hash_table_insert (context->attribute_name_states_hash,
                       name_copy, name_state);

  if (G_UNLIKELY (context->attribute_name_index_map == NULL))
    context->attribute_name_index_map =
      g_array_new (FALSE, FALSE, sizeof (void *));

  g_array_set_size (context->attribute_name_index_map, name_index + 1);

  g_array_index (context->attribute_name_index_map,
                 CoglAttributeNameState *, name_index) = name_state;

  return name_state;

error:
  g_free (name_state);
  return NULL;
}

static gboolean
validate_n_components (const CoglAttributeNameState *name_state,
                       int n_components)
{
  switch (name_state->name_id)
    {
    case COGL_ATTRIBUTE_NAME_ID_POINT_SIZE_ARRAY:
      if (G_UNLIKELY (n_components != 1))
        {
          g_critical ("The point size attribute can only have one "
                      "component");
          return FALSE;
        }
      break;
    case COGL_ATTRIBUTE_NAME_ID_POSITION_ARRAY:
    case COGL_ATTRIBUTE_NAME_ID_COLOR_ARRAY:
    case COGL_ATTRIBUTE_NAME_ID_TEXTURE_COORD_ARRAY:
    case COGL_ATTRIBUTE_NAME_ID_NORMAL_ARRAY:
    case COGL_ATTRIBUTE_NAME_ID_CUSTOM_ARRAY:
      return TRUE;
    }

  return TRUE;
}

CoglAttribute *
cogl_attribute_new (CoglAttributeBuffer *attribute_buffer,
                    const char *name,
                    size_t stride,
                    size_t offset,
                    int n_components,
                    CoglAttributeType type)
{
  CoglAttribute *attribute = g_object_new (COGL_TYPE_ATTRIBUTE, NULL);
  CoglBuffer *buffer = COGL_BUFFER (attribute_buffer);
  CoglContext *ctx = buffer->context;

  attribute->name_state =
    g_hash_table_lookup (ctx->attribute_name_states_hash, name);
  if (!attribute->name_state)
    {
      CoglAttributeNameState *name_state =
        _cogl_attribute_register_attribute_name (ctx, name);
      if (!name_state)
        goto error;
      attribute->name_state = name_state;
    }

  attribute->attribute_buffer = g_object_ref (attribute_buffer);
  attribute->stride = stride;
  attribute->offset = offset;
  attribute->n_components = n_components;
  attribute->type = type;

  if (attribute->name_state->name_id != COGL_ATTRIBUTE_NAME_ID_CUSTOM_ARRAY)
    {
      if (!validate_n_components (attribute->name_state, n_components))
        return NULL;
      attribute->normalized =
        attribute->name_state->normalized_default;
    }
  else
    attribute->normalized = FALSE;

  return attribute;

error:
  g_object_unref (attribute);
  return NULL;
}

void
cogl_attribute_set_normalized (CoglAttribute *attribute,
                                      gboolean normalized)
{
  g_return_if_fail (COGL_IS_ATTRIBUTE (attribute));

  attribute->normalized = normalized;
}

CoglAttributeBuffer *
cogl_attribute_get_buffer (CoglAttribute *attribute)
{
  g_return_val_if_fail (COGL_IS_ATTRIBUTE (attribute), NULL);

  return attribute->attribute_buffer;
}

static gboolean
validate_layer_cb (CoglPipeline *pipeline,
                   int layer_index,
                   void *user_data)
{
  CoglTexture *texture =
    cogl_pipeline_get_layer_texture (pipeline, layer_index);
  CoglFlushLayerState *state = user_data;
  gboolean status = TRUE;

  /* invalid textures will be handled correctly in
   * _cogl_pipeline_flush_layers_gl_state */
  if (texture == NULL)
    goto validated;

  _cogl_texture_flush_journal_rendering (texture);

  /* Give the texture a chance to know that we're rendering
     non-quad shaped primitives. If the texture is in an atlas it
     will be migrated */
  COGL_TEXTURE_GET_CLASS (texture)->ensure_non_quad_rendering (texture);

  /* We need to ensure the mipmaps are ready before deciding
   * anything else about the texture because the texture storate
   * could completely change if it needs to be migrated out of the
   * atlas and will affect how we validate the layer.
   */
  _cogl_pipeline_pre_paint_for_layer (pipeline, layer_index);

  if (!_cogl_texture_can_hardware_repeat (texture))
    {
      g_warning ("Disabling layer %d of the current source pipeline, "
                 "because texturing with the vertex buffer API is not "
                 "currently supported using sliced textures, or textures "
                 "with waste\n", layer_index);

      /* XXX: maybe we can add a mechanism for users to forcibly use
       * textures with waste where it would be their responsibility to use
       * texture coords in the range [0,1] such that sampling outside isn't
       * required. We can then use a texture matrix (or a modification of
       * the users own matrix) to map 1 to the edge of the texture data.
       *
       * Potentially, given the same guarantee as above we could also
       * support a single sliced layer too. We would have to redraw the
       * vertices once for each layer, each time with a fiddled texture
       * matrix.
       */
      state->fallback_layers |= (1 << state->unit);
      state->options.flags |= COGL_PIPELINE_FLUSH_FALLBACK_MASK;
    }

validated:
  state->unit++;
  return status;
}

void
_cogl_flush_attributes_state (CoglFramebuffer *framebuffer,
                              CoglPipeline *pipeline,
                              CoglDrawFlags flags,
                              CoglAttribute **attributes,
                              int n_attributes)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglFlushLayerState layers_state;
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  if (!(flags & COGL_DRAW_SKIP_JOURNAL_FLUSH))
    _cogl_framebuffer_flush_journal (framebuffer);

  layers_state.unit = 0;
  layers_state.options.flags = 0;
  layers_state.fallback_layers = 0;

  if (!(flags & COGL_DRAW_SKIP_PIPELINE_VALIDATION))
    cogl_pipeline_foreach_layer (pipeline,
                                 validate_layer_cb,
                                 &layers_state);

  /* NB: cogl_context_flush_framebuffer_state may disrupt various state (such
   * as the pipeline state) when flushing the clip stack, so should
   * always be done first when preparing to draw. We need to do this
   * before setting up the array pointers because setting up the clip
   * stack can cause some drawing which would change the array
   * pointers. */
  if (!(flags & COGL_DRAW_SKIP_FRAMEBUFFER_FLUSH))
    {
      cogl_context_flush_framebuffer_state (ctx,
                                            framebuffer,
                                            framebuffer,
                                            COGL_FRAMEBUFFER_STATE_ALL);
    }

  /* In cogl_read_pixels we have a fast-path when reading a single
   * pixel and the scene is just comprised of simple rectangles still
   * in the journal. For this optimization to work we need to track
   * when the framebuffer really does get drawn to. */
  _cogl_framebuffer_mark_clear_clip_dirty (framebuffer);

  if (driver_klass->flush_attributes_state)
    {
      driver_klass->flush_attributes_state (driver,
                                            framebuffer,
                                            pipeline,
                                            &layers_state,
                                            flags,
                                            attributes,
                                            n_attributes);
    }
}

int
_cogl_attribute_get_n_components (CoglAttribute *attribute)
{
  return attribute->n_components;
}
