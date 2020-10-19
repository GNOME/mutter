/*
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
 * Copyright (C) 2020 Red Hat
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
 */

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-offscreen-private.h"
#include "cogl-texture-private.h"

struct _CoglOffscreen
{
  CoglFramebuffer parent;

  CoglTexture *texture;
  int texture_level;
};

G_DEFINE_TYPE (CoglOffscreen, cogl_offscreen,
               COGL_TYPE_FRAMEBUFFER)

CoglOffscreen *
_cogl_offscreen_new_with_texture_full (CoglTexture       *texture,
                                       CoglOffscreenFlags flags,
                                       int                level)
{
  CoglContext *ctx = texture->context;
  CoglFramebufferDriverConfig driver_config;
  CoglOffscreen *offscreen;
  CoglFramebuffer *fb;

  g_return_val_if_fail (cogl_is_texture (texture), NULL);

  driver_config = (CoglFramebufferDriverConfig) {
    .type = COGL_FRAMEBUFFER_DRIVER_TYPE_FBO,
    .disable_depth_and_stencil =
      !!(flags & COGL_OFFSCREEN_DISABLE_DEPTH_AND_STENCIL),
  };
  offscreen = g_object_new (COGL_TYPE_OFFSCREEN,
                            "context", ctx,
                            "driver-config", &driver_config,
                            NULL);
  offscreen->texture = cogl_object_ref (texture);
  offscreen->texture_level = level;

  fb = COGL_FRAMEBUFFER (offscreen);

  /* NB: we can't assume we can query the texture's width yet, since
   * it might not have been allocated yet and for example if the
   * texture is being loaded from a file then the file might not
   * have been read yet. */

  _cogl_texture_associate_framebuffer (texture, fb);

  return offscreen;
}

CoglOffscreen *
cogl_offscreen_new_with_texture (CoglTexture *texture)
{
  return _cogl_offscreen_new_with_texture_full (texture, 0, 0);
}

CoglTexture *
cogl_offscreen_get_texture (CoglOffscreen *offscreen)
{
  return offscreen->texture;
}

int
cogl_offscreen_get_texture_level (CoglOffscreen *offscreen)
{
  return offscreen->texture_level;
}

static gboolean
cogl_offscreen_allocate (CoglFramebuffer  *framebuffer,
                         GError          **error)
{
  CoglOffscreen *offscreen = COGL_OFFSCREEN (framebuffer);
  CoglPixelFormat texture_format;
  int width, height;

  if (!cogl_texture_allocate (offscreen->texture, error))
    return FALSE;

  /* NB: it's only after allocating the texture that we will
   * determine whether a texture needs slicing... */
  if (cogl_texture_is_sliced (offscreen->texture))
    {
      g_set_error (error, COGL_SYSTEM_ERROR, COGL_SYSTEM_ERROR_UNSUPPORTED,
                   "Can't create offscreen framebuffer from "
                   "sliced texture");
      return FALSE;
    }

  width = cogl_texture_get_width (offscreen->texture);
  height = cogl_texture_get_height (offscreen->texture);
  cogl_framebuffer_update_size (framebuffer, width, height);

  texture_format = _cogl_texture_get_format (offscreen->texture);
  _cogl_framebuffer_set_internal_format (framebuffer, texture_format);

  return TRUE;
}

static gboolean
cogl_offscreen_is_y_flipped (CoglFramebuffer *framebuffer)
{
  return TRUE;
}

static void
cogl_offscreen_dispose (GObject *object)
{
  CoglOffscreen *offscreen = COGL_OFFSCREEN (object);

  G_OBJECT_CLASS (cogl_offscreen_parent_class)->dispose (object);

  cogl_clear_object (&offscreen->texture);
}

static void
cogl_offscreen_init (CoglOffscreen *offscreen)
{
}

static void
cogl_offscreen_class_init (CoglOffscreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferClass *framebuffer_class = COGL_FRAMEBUFFER_CLASS (klass);

  object_class->dispose = cogl_offscreen_dispose;

  framebuffer_class->allocate = cogl_offscreen_allocate;
  framebuffer_class->is_y_flipped = cogl_offscreen_is_y_flipped;
}
