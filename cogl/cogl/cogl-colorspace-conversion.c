/*
 * Authored By Niels De Graef <niels.degraef@barco.com>
 *
 * Copyright (C) 2018 Barco NV
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cogl-config.h"

#include "cogl-object-private.h"
#include "cogl-gtype-private.h"
#include "cogl-colorspace-conversion.h"
#include "cogl-snippet.h"
#include "cogl-pipeline-layer-state.h"
#include "cogl-pipeline-state.h"

#define _COGL_YUV_TO_RGBA(res, y, u, v)                     \
    res ".r = " y " + 1.59765625 * " v ";\n"                \
    res ".g = " y " - 0.390625 * " u " - 0.8125 * " v ";\n" \
    res ".b = " y " + 2.015625 * " u ";\n"                  \
    res ".a = 1.0;\n"

static const gchar nv12_to_rgba_shader[] =
    "vec4\n"
    "cogl_nv12_to_rgba (vec2 UV)\n"
    "{\n"
    "  vec4 color;\n"
    "  float y = 1.1640625 * (texture2D (cogl_sampler0, UV).x - 0.0625);\n"
    "  vec2 uv = texture2D (cogl_sampler1, UV).rg;\n"
    "  uv -= 0.5;\n"
    "  float u = uv.x;\n"
    "  float v = uv.y;\n"
       _COGL_YUV_TO_RGBA ("color", "y", "u", "v")
    "  return color;\n"
    "}\n";

static const gchar yuv_to_rgba_shader[] =
    "vec4\n"
    "cogl_yuv_to_rgba (vec2 UV)\n"
    "{\n"
    "  vec4 color;\n"
    "  float y = 1.16438356 * (texture2D(cogl_sampler0, UV).x - 0.0625);\n"
    "  float u = texture2D(cogl_sampler1, UV).x - 0.5;\n"
    "  float v = texture2D(cogl_sampler2, UV).x - 0.5;\n"
       _COGL_YUV_TO_RGBA ("color", "y", "u", "v")
    "  return color;\n"
    "}\n";

struct _CoglColorspaceConversion
{
  CoglObject _parent;

  CoglSnippet *vertex_declaration_snippet;
  CoglSnippet *fragment_declaration_snippet;

  CoglSnippet *fragment_execution_snippet;
};

static void
_cogl_colorspace_conversion_free (CoglColorspaceConversion *self);

COGL_OBJECT_DEFINE (ColorspaceConversion, colorspace_conversion);
COGL_GTYPE_DEFINE_CLASS (ColorspaceConversion, colorspace_conversion);


void
cogl_colorspace_conversion_attach_to_pipeline (CoglColorspaceConversion *self,
                                               CoglPipeline *pipeline,
                                               gint layer)
{
  cogl_pipeline_add_snippet (pipeline, self->fragment_declaration_snippet);
  cogl_pipeline_add_snippet (pipeline, self->vertex_declaration_snippet);

  cogl_pipeline_add_layer_snippet (pipeline,
                                   layer,
                                   self->fragment_execution_snippet);
}

static gboolean
get_cogl_snippets (CoglPixelFormat format,
                   CoglSnippet **vertex_snippet_out,
                   CoglSnippet **fragment_snippet_out,
                   CoglSnippet **layer_snippet_out)
{
  const gchar *global_hook;
  const gchar *layer_hook;

  switch (format)
    {
    case COGL_PIXEL_FORMAT_YUV444:
      global_hook = yuv_to_rgba_shader;
      layer_hook =  "cogl_layer = cogl_yuv_to_rgba(cogl_tex_coord0_in.st);\n";
      break;
    case COGL_PIXEL_FORMAT_NV12:
      /* XXX are we using Y_UV or Y_xUxV? Maybe check for RG support? */
      global_hook = nv12_to_rgba_shader;
      layer_hook =  "cogl_layer = cogl_nv12_to_rgba(cogl_tex_coord0_in.st);\n";
      break;
    default:
      *vertex_snippet_out = NULL;
      *fragment_snippet_out = NULL;
      *layer_snippet_out = NULL;
      return FALSE;
    }

  *vertex_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX_GLOBALS,
                                          global_hook,
                                          NULL);

  *fragment_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                                            global_hook,
                                            NULL);

  *layer_snippet_out = cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
                                         NULL,
                                         layer_hook);

  return TRUE;
}

static void
_cogl_colorspace_conversion_free (CoglColorspaceConversion *self)
{
  cogl_clear_object (&self->vertex_declaration_snippet);
  cogl_clear_object (&self->fragment_declaration_snippet);
  cogl_clear_object (&self->fragment_execution_snippet);
}

CoglColorspaceConversion *
cogl_colorspace_conversion_new (CoglPixelFormat format)
{
  CoglColorspaceConversion *self;
  CoglSnippet *vertex_declaration_snippet;
  CoglSnippet *fragment_declaration_snippet;
  CoglSnippet *fragment_execution_snippet;

  if (!get_cogl_snippets (format,
                          &vertex_declaration_snippet,
                          &fragment_declaration_snippet,
                          &fragment_execution_snippet))
    return NULL;

  self = g_slice_new0 (CoglColorspaceConversion);
  _cogl_colorspace_conversion_object_new (self);

  self->vertex_declaration_snippet = vertex_declaration_snippet;
  self->fragment_declaration_snippet = fragment_declaration_snippet;
  self->fragment_execution_snippet = fragment_execution_snippet;

  return self;
}
