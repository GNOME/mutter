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

#define GL_GLEXT_PROTOTYPES

#include "backends/native/meta-renderer-native-gles3.h"

#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <gio/gio.h>
#include <string.h>

#include "backends/meta-egl-ext.h"
#include "backends/meta-gles3.h"
#include "backends/meta-gles3-table.h"
#include "meta/meta-debug.h"

/*
 * GL/gl.h being included may conflict with gl3.h on some architectures.
 * Make sure that hasn't happened on any architecture.
 */
#ifdef GL_VERSION_1_1
#error "Somehow included OpenGL headers when we shouldn't have"
#endif

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
can_blit_buffer (ContextData *context_data,
                 MetaEgl     *egl,
                 EGLDisplay   egl_display,
                 uint32_t     drm_format,
                 uint64_t     drm_modifier)
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

  if (!meta_egl_has_extensions (egl, egl_display, NULL,
                                "EGL_EXT_image_dma_buf_import_modifiers",
                                NULL))
    {
      meta_topic (META_DEBUG_RENDER,
                  "No support for EGL_EXT_image_dma_buf_import_modifiers, "
                  "assuming blitting linearly will still work.");
      goto out;
    }

  if (!meta_egl_query_dma_buf_modifiers (egl, egl_display,
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
  if (!meta_egl_query_dma_buf_modifiers (egl, egl_display,
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
load_shader (const char *src,
             GLenum      type)
{
  GLuint shader = glCreateShader (type);

  if (shader)
    {
      GLint compiled;

      glShaderSource (shader, 1, &src, NULL);
      glCompileShader (shader);
      glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
      if (!compiled)
        {
          GLchar log[1024];

          glGetShaderInfoLog (shader, sizeof (log) - 1, NULL, log);
          log[sizeof (log) - 1] = '\0';
          g_warning ("load_shader compile failed: %s", log);
          glDeleteShader (shader);
          shader = 0;
        }
    }

  return shader;
}

static void
ensure_shader_program (ContextData *context_data,
                       MetaGles3   *gles3)
{
  static const char vertex_shader_source[] =
    "#version 100\n"
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "  v_texcoord = texcoord;\n"
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

  static const GLfloat box[] =
  { /* position    texcoord */
    -1.0f, +1.0f, 0.0f, 0.0f,
    +1.0f, +1.0f, 1.0f, 0.0f,
    +1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
  };
  GLint linked;
  GLuint vertex_shader, fragment_shader;
  GLint position_attrib, texcoord_attrib;
  GLuint shader_program;

  if (context_data->shader_program)
    return;

  shader_program = glCreateProgram ();
  g_return_if_fail (shader_program);
  context_data->shader_program = shader_program;

  vertex_shader = load_shader (vertex_shader_source, GL_VERTEX_SHADER);
  g_return_if_fail (vertex_shader);
  fragment_shader = load_shader (fragment_shader_source, GL_FRAGMENT_SHADER);
  g_return_if_fail (fragment_shader);

  GLBAS (gles3, glAttachShader, (shader_program, vertex_shader));
  GLBAS (gles3, glAttachShader, (shader_program, fragment_shader));
  GLBAS (gles3, glLinkProgram, (shader_program));
  GLBAS (gles3, glGetProgramiv, (shader_program, GL_LINK_STATUS, &linked));
  if (!linked)
    {
      GLchar log[1024];

      glGetProgramInfoLog (shader_program, sizeof (log) - 1, NULL, log);
      log[sizeof (log) - 1] = '\0';
      g_warning ("Link failed: %s", log);
      return;
    }

  GLBAS (gles3, glUseProgram, (shader_program));

  position_attrib = glGetAttribLocation (shader_program, "position");
  GLBAS (gles3, glEnableVertexAttribArray, (position_attrib));
  GLBAS (gles3, glVertexAttribPointer,
         (position_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (GLfloat), box));

  texcoord_attrib = glGetAttribLocation (shader_program, "texcoord");
  GLBAS (gles3, glEnableVertexAttribArray, (texcoord_attrib));
  GLBAS (gles3, glVertexAttribPointer,
         (texcoord_attrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (GLfloat), box + 2));
}

static void
blit_egl_image (MetaGles3   *gles3,
                EGLImageKHR  egl_image,
                int          width,
                int          height)
{
  GLuint texture;
  GLuint framebuffer;

  meta_gles3_clear_error (gles3);

  GLBAS (gles3, glViewport, (0, 0, width, height));

  GLBAS (gles3, glGenFramebuffers, (1, &framebuffer));
  GLBAS (gles3, glBindFramebuffer, (GL_READ_FRAMEBUFFER, framebuffer));

  GLBAS (gles3, glActiveTexture, (GL_TEXTURE0));
  GLBAS (gles3, glGenTextures, (1, &texture));
  GLBAS (gles3, glBindTexture, (GL_TEXTURE_2D, texture));
  GLEXT (gles3, glEGLImageTargetTexture2DOES, (GL_TEXTURE_2D, egl_image));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                  GL_CLAMP_TO_EDGE));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                  GL_CLAMP_TO_EDGE));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_2D, GL_TEXTURE_WRAP_R_OES,
                                  GL_CLAMP_TO_EDGE));

  GLBAS (gles3, glFramebufferTexture2D, (GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, texture, 0));

  GLBAS (gles3, glBindFramebuffer, (GL_READ_FRAMEBUFFER, framebuffer));
  GLBAS (gles3, glBlitFramebuffer, (0, height, width, 0,
                                    0, 0, width, height,
                                    GL_COLOR_BUFFER_BIT,
                                    GL_NEAREST));

  GLBAS (gles3, glDeleteTextures, (1, &texture));
  GLBAS (gles3, glDeleteFramebuffers, (1, &framebuffer));
}

static void
paint_egl_image (ContextData *context_data,
                 MetaGles3   *gles3,
                 EGLImageKHR  egl_image,
                 int          width,
                 int          height)
{
  GLuint texture;

  meta_gles3_clear_error (gles3);
  ensure_shader_program (context_data, gles3);

  GLBAS (gles3, glViewport, (0, 0, width, height));

  GLBAS (gles3, glActiveTexture, (GL_TEXTURE0));
  GLBAS (gles3, glGenTextures, (1, &texture));
  GLBAS (gles3, glBindTexture, (GL_TEXTURE_EXTERNAL_OES, texture));
  GLEXT (gles3, glEGLImageTargetTexture2DOES, (GL_TEXTURE_EXTERNAL_OES,
                                               egl_image));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_EXTERNAL_OES,
                                  GL_TEXTURE_MAG_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_EXTERNAL_OES,
                                  GL_TEXTURE_MIN_FILTER,
                                  GL_NEAREST));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_EXTERNAL_OES,
                                  GL_TEXTURE_WRAP_S,
                                  GL_CLAMP_TO_EDGE));
  GLBAS (gles3, glTexParameteri, (GL_TEXTURE_EXTERNAL_OES,
                                  GL_TEXTURE_WRAP_T,
                                  GL_CLAMP_TO_EDGE));

  GLBAS (gles3, glDrawArrays, (GL_TRIANGLE_FAN, 0, 4));

  GLBAS (gles3, glDeleteTextures, (1, &texture));
}

gboolean
meta_renderer_native_gles3_blit_shared_bo (MetaEgl        *egl,
                                           MetaGles3      *gles3,
                                           EGLDisplay      egl_display,
                                           EGLContext      egl_context,
                                           EGLImageKHR     egl_image,
                                           struct gbm_bo  *shared_bo,
                                           GError        **error)
{
  unsigned int width;
  unsigned int height;
  GQuark context_data_quark;
  ContextData *context_data;
  gboolean can_blit;

  context_data_quark = get_quark_for_egl_context (egl_context);
  context_data = g_object_get_qdata (G_OBJECT (gles3), context_data_quark);
  if (!context_data)
    {
      context_data = g_new0 (ContextData, 1);
      context_data->buffer_support = g_array_new (FALSE, FALSE,
                                                  sizeof (BufferTypeSupport));

      g_object_set_qdata_full (G_OBJECT (gles3),
                               context_data_quark,
                               context_data,
                               (GDestroyNotify) context_data_free);
    }

  can_blit = can_blit_buffer (context_data,
                              egl, egl_display,
                              gbm_bo_get_format (shared_bo),
                              gbm_bo_get_modifier (shared_bo));

  width = gbm_bo_get_width (shared_bo);
  height = gbm_bo_get_height (shared_bo);

  if (can_blit)
    blit_egl_image (gles3, egl_image, width, height);
  else
    paint_egl_image (context_data, gles3, egl_image, width, height);

  return TRUE;
}

void
meta_renderer_native_gles3_forget_context (MetaGles3  *gles3,
                                           EGLContext  egl_context)
{
  GQuark context_data_quark = get_quark_for_egl_context (egl_context);

  g_object_set_qdata (G_OBJECT (gles3), context_data_quark, NULL);
}
