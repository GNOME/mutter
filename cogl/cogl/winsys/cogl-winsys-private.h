/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#include "cogl/cogl-renderer.h"
#include "cogl/cogl-scanout.h"


COGL_EXPORT uint32_t
_cogl_winsys_error_quark (void);

#define COGL_WINSYS_ERROR (_cogl_winsys_error_quark ())

typedef enum /*< prefix=COGL_WINSYS_ERROR >*/
{
  COGL_WINSYS_ERROR_INIT,
  COGL_WINSYS_ERROR_CREATE_CONTEXT,
  COGL_WINSYS_ERROR_CREATE_ONSCREEN,
  COGL_WINSYS_ERROR_MAKE_CURRENT,
} CoglWinsysError;


typedef struct _CoglWinsysVtable
{
  const char *name;

  /* Required functions */

  GCallback
  (*renderer_get_proc_address) (CoglRenderer *renderer,
                                const char   *name);

  gboolean
  (*renderer_connect) (CoglRenderer *renderer,
                       GError      **error);

  void
  (*renderer_disconnect) (CoglRenderer *renderer);

  void
  (*renderer_outputs_changed) (CoglRenderer *renderer);

  gboolean
  (*display_setup) (CoglDisplay *display,
                    GError     **error);

  void
  (*display_destroy) (CoglDisplay *display);

  GArray *
  (* renderer_query_drm_modifiers) (CoglRenderer           *renderer,
                                    CoglPixelFormat         format,
                                    CoglDrmModifierFilter   filter,
                                    GError                **error);

  uint64_t
  (* renderer_get_implicit_drm_modifier) (CoglRenderer *renderer);

  CoglDmaBufHandle *
  (*renderer_create_dma_buf) (CoglRenderer     *renderer,
                              CoglPixelFormat   format,
                              uint64_t         *modifiers,
                              int               n_modifiers,
                              int               width,
                              int               height,
                              GError          **error);

  gboolean
  (*renderer_is_dma_buf_supported) (CoglRenderer *renderer);

  void
  (*renderer_bind_api) (CoglRenderer *renderer);

  gboolean
  (*context_init) (CoglContext *context,
                   GError     **error);

  void
  (*context_deinit) (CoglContext *context);

  void
  (*update_sync) (CoglContext *ctx);

  int
  (*get_sync_fd) (CoglContext *ctx);

} CoglWinsysVtable;

typedef const CoglWinsysVtable *(*CoglWinsysVtableGetter) (void);
