/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#define GL_GLEXT_PROTOTYPES

#include "backends/native/meta-renderer-native-gles3.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <gio/gio.h>
#include <GLES3/gl3.h>
#include <string.h>

#include "backends/meta-egl-ext.h"
#include "backends/meta-gles3.h"
#include "backends/meta-gles3-table.h"

/*
 * GL/gl.h being included may conflit with gl3.h on some architectures.
 * Make sure that hasn't happened on any architecture.
 */
#ifdef GL_VERSION_1_1
#error "Somehow included OpenGL headers when we shouldn't have"
#endif

static EGLImageKHR
create_egl_image (MetaEgl       *egl,
                  EGLDisplay     egl_display,
                  EGLContext     egl_context,
                  unsigned int   width,
                  unsigned int   height,
                  uint32_t       n_planes,
                  uint32_t      *strides,
                  uint32_t      *offsets,
                  uint64_t      *modifiers,
                  uint32_t       format,
                  int            fd,
                  GError       **error)
{
  EGLint attribs[37];
  int atti = 0;
  gboolean has_modifier;

  /* This requires the Mesa commit in
   * Mesa 10.3 (08264e5dad4df448e7718e782ad9077902089a07) or
   * Mesa 10.2.7 (55d28925e6109a4afd61f109e845a8a51bd17652).
   * Otherwise Mesa closes the fd behind our back and re-importing
   * will fail.
   * https://bugs.freedesktop.org/show_bug.cgi?id=76188
   */

  attribs[atti++] = EGL_WIDTH;
  attribs[atti++] = width;
  attribs[atti++] = EGL_HEIGHT;
  attribs[atti++] = height;
  attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[atti++] = format;

  has_modifier = (modifiers[0] != DRM_FORMAT_MOD_INVALID &&
                  modifiers[0] != DRM_FORMAT_MOD_LINEAR);

  if (n_planes > 0)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
      attribs[atti++] = fd;
      attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
      attribs[atti++] = offsets[0];
      attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
      attribs[atti++] = strides[0];
      if (has_modifier)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[0] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[0] >> 32;
        }
    }

  if (n_planes > 1)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
      attribs[atti++] = fd;
      attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
      attribs[atti++] = offsets[1];
      attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
      attribs[atti++] = strides[1];
      if (has_modifier)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[1] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[1] >> 32;
        }
    }

  if (n_planes > 2)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
      attribs[atti++] = fd;
      attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
      attribs[atti++] = offsets[2];
      attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
      attribs[atti++] = strides[2];
      if (has_modifier)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[2] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[2] >> 32;
        }
    }

  attribs[atti++] = EGL_NONE;

  return meta_egl_create_image (egl, egl_display, EGL_NO_CONTEXT,
                                EGL_LINUX_DMA_BUF_EXT, NULL,
                                attribs,
                                error);
}

static void
paint_egl_image (MetaGles3   *gles3,
                 EGLImageKHR  egl_image,
                 int          width,
                 int          height)
{
  GLuint texture;
  GLuint framebuffer;

  meta_gles3_clear_error (gles3);

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
}

gboolean
meta_renderer_native_gles3_draw_pixels (MetaEgl        *egl,
                                        MetaGles3      *gles3,
                                        unsigned int    width,
                                        unsigned int    height,
                                        uint8_t        *pixels,
                                        GError        **error)
{
  GLuint vertex_array;
  GLuint vertex_buffer;
  GLuint triangle_buffer;
  static const float view_left = -1.0f, view_right = 1.0f, view_top = 1.0f, view_bottom = -1.0f;
  static const float texture_left = 0.0f, texture_right = 1.0f, texture_top = 0.0f, texture_bottom = 1.0f;

  struct __attribute__ ((__packed__)) position
  {
    float x, y;
  };

  struct __attribute__ ((__packed__)) texture_coordinate
  {
    float u, v;
  };

  struct __attribute__ ((__packed__)) vertex
  {
    struct position position;
    struct texture_coordinate texture_coordinate;
  };

  struct __attribute__ ((__packed__)) triangle
  {
    unsigned int first_vertex;
    unsigned int middle_vertex;
    unsigned int last_vertex;
  };

  enum
  {
    RIGHT_TOP_VERTEX = 0,
    BOTTOM_RIGHT_VERTEX,
    BOTTOM_LEFT_VERTEX,
    TOP_LEFT_VERTEX
  };

  struct vertex vertices[] = {
    [RIGHT_TOP_VERTEX] = {{view_right, view_top},
                          {texture_right, texture_top}},
    [BOTTOM_RIGHT_VERTEX] = {{view_right, view_bottom},
                             {texture_right, texture_bottom}},
    [BOTTOM_LEFT_VERTEX] = {{view_left, view_bottom},
                            {texture_left, texture_bottom}},
    [TOP_LEFT_VERTEX] = {{view_left, view_top},
                         {texture_left, texture_top}},
  };

  struct triangle triangles[] = {
    {TOP_LEFT_VERTEX, BOTTOM_RIGHT_VERTEX, BOTTOM_LEFT_VERTEX},
    {TOP_LEFT_VERTEX, RIGHT_TOP_VERTEX, BOTTOM_RIGHT_VERTEX},
  };

  GLuint texture;

  meta_gles3_clear_error (gles3);

  GLBAS (gles3, glClearColor, (0.0,1.0,0.0,1.0));
  GLBAS (gles3, glClear, (GL_COLOR_BUFFER_BIT));

  GLBAS (gles3, glViewport, (0, 0, width, height));
  GLBAS (gles3, glGenVertexArrays, (1, &vertex_array));
  GLBAS (gles3, glBindVertexArray, (vertex_array));

  GLBAS (gles3, glGenBuffers, (1, &vertex_buffer));
  GLBAS (gles3, glBindBuffer, (GL_ARRAY_BUFFER, vertex_buffer));
  GLBAS (gles3, glBufferData, (GL_ARRAY_BUFFER, sizeof (vertices), vertices, GL_STREAM_DRAW));

  GLBAS (gles3, glGenBuffers, (1, &triangle_buffer));
  GLBAS (gles3, glBindBuffer, (GL_ELEMENT_ARRAY_BUFFER, triangle_buffer));
  GLBAS (gles3, glBufferData, (GL_ELEMENT_ARRAY_BUFFER, sizeof (triangles), triangles, GL_STREAM_DRAW));

  GLBAS (gles3, glActiveTexture, (GL_TEXTURE0));
  GLBAS (gles3, glGenTextures, (1, &texture));
  GLBAS (gles3, glBindTexture, (GL_TEXTURE_2D, texture));
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
  GLBAS (gles3, glTexImage2D, (GL_TEXTURE_2D, 0, GL_RGBA,
                               width, height, 0, GL_RGBA,
                               GL_UNSIGNED_BYTE, pixels));

  GLBAS (gles3, glBindBuffer, (GL_ARRAY_BUFFER, vertex_buffer));
  GLBAS (gles3, glEnableVertexAttribArray, (0));
  GLBAS (gles3, glVertexAttribPointer, (0, 2, GL_FLOAT, GL_FALSE, sizeof (struct vertex), (void *) offsetof (struct vertex, position)));
  GLBAS (gles3, glEnableVertexAttribArray, (1));
  GLBAS (gles3, glVertexAttribPointer, (1, 2, GL_FLOAT, GL_FALSE, sizeof (struct vertex), (void *) offsetof (struct vertex, texture_coordinate)));
  GLBAS (gles3, glDrawElements, (GL_TRIANGLES, G_N_ELEMENTS (triangles) * (sizeof (struct triangle) / sizeof (unsigned int)), GL_UNSIGNED_INT, 0));

  return TRUE;
}

gboolean
meta_renderer_native_gles3_blit_shared_bo (MetaEgl        *egl,
                                           MetaGles3      *gles3,
                                           EGLDisplay      egl_display,
                                           EGLContext      egl_context,
                                           EGLSurface      egl_surface,
                                           struct gbm_bo  *shared_bo,
                                           GError        **error)
{
  int shared_bo_fd;
  unsigned int width;
  unsigned int height;
  uint32_t i, n_planes;
  uint32_t strides[4] = { 0 };
  uint32_t offsets[4] = { 0 };
  uint64_t modifiers[4] = { 0 };
  uint32_t format;
  EGLImageKHR egl_image;

  shared_bo_fd = gbm_bo_get_fd (shared_bo);
  if (shared_bo_fd < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to export gbm_bo: %s", strerror (errno));
      return FALSE;
    }

  width = gbm_bo_get_width (shared_bo);
  height = gbm_bo_get_height (shared_bo);
  format = gbm_bo_get_format (shared_bo);

  n_planes = gbm_bo_get_plane_count (shared_bo);
  for (i = 0; i < n_planes; i++)
    {
      strides[i] = gbm_bo_get_stride_for_plane (shared_bo, i);
      offsets[i] = gbm_bo_get_offset (shared_bo, i);
      modifiers[i] = gbm_bo_get_modifier (shared_bo);
    }

  egl_image = create_egl_image (egl,
                                egl_display,
                                egl_context,
                                width, height,
                                n_planes,
                                strides, offsets,
                                modifiers, format,
                                shared_bo_fd,
                                error);
  close (shared_bo_fd);

  if (!egl_image)
    return FALSE;

  paint_egl_image (gles3, egl_image, width, height);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  return TRUE;
}

void
meta_renderer_native_gles3_read_pixels (MetaEgl   *egl,
                                        MetaGles3 *gles3,
                                        int        width,
                                        int        height,
                                        uint8_t   *target_data)
{
  int y;

  GLBAS (gles3, glFinish, ());

  for (y = 0; y < height; y++)
    {
      GLBAS (gles3, glReadPixels, (0, height - y, width, 1,
                                   GL_RGBA, GL_UNSIGNED_BYTE,
                                   target_data + width * y * 4));
    }
}

void
meta_renderer_native_gles3_load_basic_shaders (MetaEgl   *egl,
                                               MetaGles3 *gles3)
{
  GLuint vertex_shader = 0, fragment_shader = 0, shader_program;
  gboolean status = FALSE;
  const char *vertex_shader_source =
"#version 330 core\n"
"layout (location = 0) in vec2 position;\n"
"layout (location = 1) in vec2 input_texture_coords;\n"
"out vec2 texture_coords;\n"
"void main()\n"
"{\n"
"  gl_Position = vec4(position.x, position.y, 0.0f, 1.0f);\n"
"  texture_coords = input_texture_coords;\n"
"}\n";

  const char *fragment_shader_source =
"#version 330 core\n"
"uniform sampler2D input_texture;\n"
"in vec2 texture_coords;\n"
"out vec4 output_color;\n"
"void main()\n"
"{\n"
"  output_color = texture(input_texture, texture_coords);\n"
"}\n";

  vertex_shader = glCreateShader (GL_VERTEX_SHADER);
  glShaderSource (vertex_shader, 1, &vertex_shader_source, NULL);
  glCompileShader (vertex_shader);
  glGetShaderiv (vertex_shader, GL_COMPILE_STATUS, &status);

  if (!status)
    {
      char compile_log[1024] = "";
      glGetShaderInfoLog (vertex_shader, sizeof (compile_log), NULL, compile_log);
      g_warning ("vertex shader compilation failed:\n %s\n", compile_log);
      goto out;
    }

  fragment_shader = glCreateShader (GL_FRAGMENT_SHADER);
  glShaderSource (fragment_shader, 1, &fragment_shader_source, NULL);
  glCompileShader (fragment_shader);
  glGetShaderiv (fragment_shader, GL_COMPILE_STATUS, &status);

  if (!status)
    {
      char compile_log[1024] = "";
      glGetShaderInfoLog (fragment_shader, sizeof (compile_log), NULL, compile_log);
      g_warning ("fragment shader compilation failed:\n %s\n", compile_log);
      goto out;
    }

  shader_program = glCreateProgram ();
  glAttachShader (shader_program, vertex_shader);
  glAttachShader (shader_program, fragment_shader);
  glLinkProgram (shader_program);

  glGetProgramiv (shader_program, GL_LINK_STATUS, &status);

  if (!status)
    {
      char link_log[1024] = "";

      glGetProgramInfoLog (shader_program, sizeof (link_log), NULL, link_log);
      g_warning ("shader link failed:\n %s\n", link_log);
      goto out;
    }

  glUseProgram (shader_program);

out:
  if (vertex_shader)
    glDeleteShader (vertex_shader);
  if (fragment_shader)
    glDeleteShader (fragment_shader);
}
