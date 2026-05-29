/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
 * Copyright (c) 2023 Canonical Ltd.
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
 *
 */

#include "config.h"

#include "backends/native/meta-renderer-native-gles3.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <gio/gio.h>
#include <string.h>

#include "cogl/cogl.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "meta/meta-debug.h"
#include "mtk/mtk.h"

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
#ifndef GL_TEXTURE_WRAP_R_OES
#define GL_TEXTURE_WRAP_R_OES 0x8072
#endif

#define TRIANGLE_COUNT_PER_RECTANGLE 2
#define VERTICES_PER_TRIANGLE 3
#define VALUES_PER_VERTEX 4
#define COMPONENTS_PER_RECTANGLE TRIANGLE_COUNT_PER_RECTANGLE * VERTICES_PER_TRIANGLE * VALUES_PER_VERTEX

typedef struct _ContextData
{
  GArray *buffer_support;
  GLuint shader_program;
} ContextData;

typedef struct
{
  uint32_t drm_format;
  uint64_t drm_modifier;
  gboolean can_blit;
} BufferTypeSupport;

static void
context_data_free (ContextData *context_data)
{
  g_array_free (context_data->buffer_support, TRUE);
  g_free (context_data);
}

static GQuark
get_quark_for_egl_context (EGLContext egl_context)
{
  char key[128];

  g_snprintf (key, sizeof key, "EGLContext %p", egl_context);

  return g_quark_from_string (key);
}

static gboolean
can_blit_buffer (ContextData     *context_data,
                 CoglRendererEGL *renderer_egl,
                 uint32_t         drm_format,
                 uint64_t         drm_modifier)
{
  EGLint num_modifiers;
  EGLuint64KHR *modifiers;
  EGLBoolean *external_only;
  g_autoptr (GError) error = NULL;
  int i;
  gboolean can_blit;
  BufferTypeSupport support;

  can_blit = drm_modifier == DRM_FORMAT_MOD_LINEAR;

  for (i = 0; i < context_data->buffer_support->len; i++)
    {
      BufferTypeSupport *other_support =
        &g_array_index (context_data->buffer_support, BufferTypeSupport, i);

      if (other_support->drm_format == drm_format &&
          other_support->drm_modifier == drm_modifier)
        return other_support->can_blit;
    }

  if (!cogl_renderer_egl_has_extensions (renderer_egl, NULL,
                                         "EGL_EXT_image_dma_buf_import_modifiers",
                                         NULL))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No support for EGL_EXT_image_dma_buf_import_modifiers, "
                  "assuming blitting linearly will still work.");
      goto out;
    }

  if (!cogl_renderer_egl_query_dma_buf_modifiers (renderer_egl,
                                                   drm_format, 0, NULL, NULL,
                                                   &num_modifiers, &error))
    {
      meta_topic (META_DEBUG_RENDER,
                  "Failed to query supported DMA buffer modifiers (%s), "
                  "assuming blitting linearly will still work.",
                  error->message);
      goto out;
    }

  if (num_modifiers == 0)
    goto out;

  modifiers = g_alloca0 (sizeof (EGLuint64KHR) * num_modifiers);
  external_only = g_alloca0 (sizeof (EGLBoolean) * num_modifiers);
  if (!cogl_renderer_egl_query_dma_buf_modifiers (renderer_egl,
                                                   drm_format, num_modifiers,
                                                   modifiers, external_only,
                                                   &num_modifiers, &error))
    {
      g_warning ("Failed to requery supported DMA buffer modifiers: %s",
                 error->message);
      can_blit = FALSE;
      goto out;
    }

  can_blit = FALSE;
  for (i = 0; i < num_modifiers; i++)
    {
      if (drm_modifier == modifiers[i])
        {
          can_blit = !external_only[i];
          goto out;
        }
    }

out:
  support = (BufferTypeSupport) {
    .drm_format = drm_format,
    .drm_modifier = drm_modifier,
    .can_blit = can_blit,
  };
  g_array_append_val (context_data->buffer_support, support);
  return can_blit;
}

static GLuint
load_shader (CoglDriver *driver,
             const char *src,
             GLenum      type)
{
  GLuint shader;

  GE_RET (shader, driver, glCreateShader (type));

  if (shader)
    {
      GLint compiled;

      GE (driver, glShaderSource (shader, 1, &src, NULL));
      GE (driver, glCompileShader (shader));
      GE (driver, glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled));
      if (!compiled)
        {
          GLchar log[1024];

          GE (driver, glGetShaderInfoLog (shader, sizeof (log) - 1, NULL, log));
          log[sizeof (log) - 1] = '\0';
          g_warning ("load_shader compile failed: %s", log);
          GE (driver, glDeleteShader (shader));
          shader = 0;
        }
    }

  return shader;
}

static void
ensure_shader_program (ContextData *context_data,
                       CoglDriver  *driver)
{
  static const char vertex_shader_source[] =
    "#version 100\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "uniform float framebuffer_width;\n"
    "uniform float framebuffer_height;\n"
    "\n"
    "void main()\n"
    "{\n"
    "  gl_Position = vec4(position.x / framebuffer_width * 2.0 - 1.0, position.y / framebuffer_height * 2.0 - 1.0, 0.0, 1.0);\n"
    "  v_texcoord = vec2(texcoord.x / framebuffer_width, texcoord.y / framebuffer_height);\n"
    "}\n";

  static const char fragment_shader_source[] =
    "#version 100\n"
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES s_texture;\n"
    "varying vec2 v_texcoord;\n"
    "\n"
    " void main()\n"
    "{\n"
    "  gl_FragColor = texture2D(s_texture, v_texcoord);\n"
    "}\n";

  GLint linked;
  GLuint vertex_shader, fragment_shader;
  GLuint shader_program;

  if (context_data->shader_program)
    return;

  GE_RET (shader_program, driver, glCreateProgram ());
  g_return_if_fail (shader_program);
  context_data->shader_program = shader_program;

  vertex_shader = load_shader (driver, vertex_shader_source, GL_VERTEX_SHADER);
  g_return_if_fail (vertex_shader);
  fragment_shader = load_shader (driver, fragment_shader_source, GL_FRAGMENT_SHADER);
  g_return_if_fail (fragment_shader);

  GE (driver, glAttachShader (shader_program, vertex_shader));
  GE (driver, glAttachShader (shader_program, fragment_shader));
  GE (driver, glLinkProgram (shader_program));
  GE (driver, glGetProgramiv (shader_program, GL_LINK_STATUS, &linked));
  if (!linked)
    {
      GLchar log[1024];

      GE (driver, glGetProgramInfoLog (shader_program, sizeof (log) - 1, NULL, log));
      log[sizeof (log) - 1] = '\0';
      g_warning ("Link failed: %s", log);
      return;
    }

  GE (driver, glUseProgram (shader_program));
}

static void
clear_gl_errors (CoglDriver *driver)
{
  while (cogl_driver_gl_get_gl_error (COGL_DRIVER_GL (driver)) != GL_NO_ERROR)
    ;
}

static void
blit_egl_image (CoglDriver       *driver,
                EGLImageKHR       egl_image,
                int               width,
                int               height,
                const MtkRegion  *region)
{
  GLuint texture;
  GLuint framebuffer;
  int i;
  int n_rectangles = mtk_region_num_rectangles (region);

  clear_gl_errors (driver);

  GE (driver, glViewport (0, 0, width, height));

  GE (driver, glGenFramebuffers (1, &framebuffer));
  GE (driver, glBindFramebuffer (GL_READ_FRAMEBUFFER, framebuffer));

  GE (driver, glActiveTexture (GL_TEXTURE0));
  GE (driver, glGenTextures (1, &texture));
  GE (driver, glBindTexture (GL_TEXTURE_2D, texture));
  GE (driver, glEGLImageTargetTexture2D (GL_TEXTURE_2D, egl_image));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                               GL_NEAREST));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                               GL_NEAREST));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                               GL_CLAMP_TO_EDGE));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                               GL_CLAMP_TO_EDGE));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_R_OES,
                               GL_CLAMP_TO_EDGE));

  GE (driver, glFramebufferTexture2D (GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, texture, 0));

  for (i = 0; i < n_rectangles; ++i)
    {
      MtkRectangle rectangle;
      GLint x1, y1, x2, y2;

      rectangle = mtk_region_get_rectangle (region, i);

      x1 = rectangle.x;
      y1 = rectangle.y;
      x2 = x1 + rectangle.width;
      y2 = y1 + rectangle.height;

      GE (driver, glBlitFramebuffer (x1, y1, x2, y2,
                                     x1, y1, x2, y2,
                                     GL_COLOR_BUFFER_BIT,
                                     GL_NEAREST));
    }


  GE (driver, glDeleteTextures (1, &texture));
  GE (driver, glDeleteFramebuffers (1, &framebuffer));
}

static void
paint_egl_image (ContextData      *context_data,
                 CoglDriver       *driver,
                 EGLImageKHR       egl_image,
                 int               width,
                 int               height,
                 const MtkRegion  *region)
{
  int i;
  GLuint texture;
  GLint *vertices = NULL;
  GLuint vertex_buffer_object;
  GLuint vertex_array_object;
  GLint position_attrib, texcoord_attrib;
  GLint framebuffer_width_uniform, framebuffer_height_uniform;
  GLuint size_per_rectangle = COMPONENTS_PER_RECTANGLE * sizeof(GLint);
  GLuint vertices_size;
  int n_rectangles = mtk_region_num_rectangles (region);

  vertices_size = n_rectangles * size_per_rectangle;

  vertices = g_alloca (vertices_size);

  for (i = 0; i < n_rectangles; ++i)
    {
      GLint x1, y1, x2, y2;
      GLint u1, v1, u2, v2;
      MtkRectangle rectangle;
      GLint *rectangle_vertices;

      rectangle = mtk_region_get_rectangle (region, i);
      rectangle_vertices = &vertices[i * COMPONENTS_PER_RECTANGLE];

      x1 = rectangle.x;
      y1 = rectangle.y;
      x2 = rectangle.x + rectangle.width;
      y2 = rectangle.y + rectangle.height;

      u1 = rectangle.x;
      v1 = rectangle.y;
      u2 = rectangle.x + rectangle.width;
      v2 = rectangle.y + rectangle.height;

      rectangle_vertices[0] = x1;
      rectangle_vertices[1] = y1;
      rectangle_vertices[2] = u1;
      rectangle_vertices[3] = v1;

      rectangle_vertices[4] = x2;
      rectangle_vertices[5] = y1;
      rectangle_vertices[6] = u2;
      rectangle_vertices[7] = v1;

      rectangle_vertices[8] = x1;
      rectangle_vertices[9] = y2;
      rectangle_vertices[10] = u1;
      rectangle_vertices[11] = v2;

      rectangle_vertices[12] = x1;
      rectangle_vertices[13] = y2;
      rectangle_vertices[14] = u1;
      rectangle_vertices[15] = v2;

      rectangle_vertices[16] = x2;
      rectangle_vertices[17] = y1;
      rectangle_vertices[18] = u2;
      rectangle_vertices[19] = v1;

      rectangle_vertices[20] = x2;
      rectangle_vertices[21] = y2;
      rectangle_vertices[22] = u2;
      rectangle_vertices[23] = v2;
    }

  clear_gl_errors (driver);
  ensure_shader_program (context_data, driver);

  g_return_if_fail (context_data->shader_program);

  GE (driver, glViewport (0, 0, width, height));

  GE (driver, glGenVertexArrays (1, &vertex_array_object));
  GE (driver, glBindVertexArray (vertex_array_object));

  GE (driver, glGenBuffers (1, &vertex_buffer_object));
  GE (driver, glBindBuffer (GL_ARRAY_BUFFER,
                            vertex_buffer_object));
  GE (driver, glBufferData (GL_ARRAY_BUFFER,
                            vertices_size,
                            vertices,
                            GL_DYNAMIC_DRAW));

  GE_RET (position_attrib, driver,
          glGetAttribLocation (context_data->shader_program, "position"));
  GE (driver, glEnableVertexAttribArray (position_attrib));
  GE (driver, glVertexAttribPointer (position_attrib, 2, GL_INT, GL_FALSE,
                                     4 * sizeof (GLint), NULL));

  GE_RET (texcoord_attrib, driver,
          glGetAttribLocation (context_data->shader_program, "texcoord"));
  GE (driver, glEnableVertexAttribArray (texcoord_attrib));
  GE (driver, glVertexAttribPointer (texcoord_attrib, 2, GL_INT, GL_FALSE,
                                     4 * sizeof (GLint),
                                     (void*)(sizeof(GLint) * 2)));

  GE_RET (framebuffer_width_uniform, driver,
          glGetUniformLocation (context_data->shader_program,
                                "framebuffer_width"));
  GE (driver, glUniform1f (framebuffer_width_uniform, width));

  GE_RET (framebuffer_height_uniform, driver,
          glGetUniformLocation (context_data->shader_program,
                                "framebuffer_height"));
  GE (driver, glUniform1f (framebuffer_height_uniform, height));

  GE (driver, glActiveTexture (GL_TEXTURE0));
  GE (driver, glGenTextures (1, &texture));
  GE (driver, glBindTexture (GL_TEXTURE_EXTERNAL_OES, texture));
  GE (driver, glEGLImageTargetTexture2D (GL_TEXTURE_EXTERNAL_OES, egl_image));
  GE (driver, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                               GL_TEXTURE_MAG_FILTER,
                               GL_NEAREST));
  GE (driver, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                               GL_TEXTURE_MIN_FILTER,
                               GL_NEAREST));
  GE (driver, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                               GL_TEXTURE_WRAP_S,
                               GL_CLAMP_TO_EDGE));
  GE (driver, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                               GL_TEXTURE_WRAP_T,
                               GL_CLAMP_TO_EDGE));

  GE (driver, glDrawArrays (GL_TRIANGLES, 0,
                            TRIANGLE_COUNT_PER_RECTANGLE *
                            VERTICES_PER_TRIANGLE *
                            n_rectangles));

  GE (driver, glDeleteTextures (1, &texture));
  GE (driver, glDeleteBuffers (1, &vertex_buffer_object));
  GE (driver, glDeleteVertexArrays (1, &vertex_array_object));
}

gboolean
meta_renderer_native_gles3_blit_shared_bo (CoglDriver       *driver,
                                           CoglRendererEGL  *renderer_egl,
                                           EGLContext        egl_context,
                                           EGLImageKHR       dst_egl_image,
                                           EGLImageKHR       src_egl_image,
                                           struct gbm_bo    *shared_bo,
                                           const MtkRegion  *region,
                                           GError          **error)
{
  unsigned int width;
  unsigned int height;
  GQuark context_data_quark;
  ContextData *context_data;
  GLuint dst_texture, dst_framebuffer;
  gboolean can_blit;

  context_data_quark = get_quark_for_egl_context (egl_context);
  context_data = g_object_get_qdata (G_OBJECT (driver), context_data_quark);
  if (!context_data)
    {
      context_data = g_new0 (ContextData, 1);
      context_data->buffer_support = g_array_new (FALSE, FALSE,
                                                  sizeof (BufferTypeSupport));

      g_object_set_qdata_full (G_OBJECT (driver),
                               context_data_quark,
                               context_data,
                               (GDestroyNotify) context_data_free);
    }

  can_blit = can_blit_buffer (context_data,
                              renderer_egl,
                              gbm_bo_get_format (shared_bo),
                              gbm_bo_get_modifier (shared_bo));

  width = gbm_bo_get_width (shared_bo);
  height = gbm_bo_get_height (shared_bo);

  GE (driver, glGenFramebuffers (1, &dst_framebuffer));
  GE (driver, glBindFramebuffer (GL_DRAW_FRAMEBUFFER, dst_framebuffer));
  GE (driver, glActiveTexture (GL_TEXTURE0));
  GE (driver, glGenTextures (1, &dst_texture));
  GE (driver, glBindTexture (GL_TEXTURE_2D, dst_texture));
  GE (driver, glEGLImageTargetTexture2D (GL_TEXTURE_2D, dst_egl_image));
  GE (driver, glFramebufferTexture2D (GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, dst_texture, 0));

  if (can_blit)
    blit_egl_image (driver, src_egl_image, width, height, region);
  else
    paint_egl_image (context_data, driver, src_egl_image, width, height, region);

  GE (driver, glDeleteTextures (1, &dst_texture));
  GE (driver, glDeleteFramebuffers (1, &dst_framebuffer));
  return TRUE;
}

void
meta_renderer_native_gles3_forget_context (CoglDriver *driver,
                                           EGLContext  egl_context)
{
  GQuark context_data_quark = get_quark_for_egl_context (egl_context);

  g_object_set_qdata (G_OBJECT (driver), context_data_quark, NULL);
}
