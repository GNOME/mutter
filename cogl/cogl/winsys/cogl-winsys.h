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


typedef enum _CoglDrmModifierFilter
{
  COGL_DRM_MODIFIER_FILTER_NONE = 0,
  COGL_DRM_MODIFIER_FILTER_SINGLE_PLANE = 1 << 0,
  COGL_DRM_MODIFIER_FILTER_NOT_EXTERNAL_ONLY = 1 << 1,
} CoglDrmModifierFilter;

#define COGL_TYPE_WINSYS (cogl_winsys_get_type ())

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglWinsys,
                          cogl_winsys,
                          COGL,
                          WINSYS,
                          GObject)

struct _CoglWinsysClass
{
  GObjectClass parent_class;

  gboolean (*renderer_connect) (CoglWinsys   *winsys,
                                CoglRenderer *renderer,
                                GError      **error);

  gboolean (*display_setup) (CoglWinsys   *winsys,
                             CoglDisplay  *display,
                             GError      **error);

  void (*display_destroy) (CoglWinsys  *winsys,
                           CoglDisplay *display);

  GArray * (* renderer_query_drm_modifiers) (CoglWinsys             *winsys,
                                             CoglRenderer           *renderer,
                                             CoglPixelFormat         format,
                                             CoglDrmModifierFilter   filter,
                                             GError                **error);

  uint64_t (* renderer_get_implicit_drm_modifier) (CoglWinsys   *winsys,
                                                   CoglRenderer *renderer);

  CoglDmaBufHandle * (*renderer_create_dma_buf) (CoglWinsys       *winsys,
                                                 CoglRenderer     *renderer,
                                                 CoglPixelFormat   format,
                                                 uint64_t         *modifiers,
                                                 int               n_modifiers,
                                                 int               width,
                                                 int               height,
                                                 GError          **error);

  gboolean (*renderer_is_dma_buf_supported) (CoglWinsys   *winsys,
                                             CoglRenderer *renderer);

  void (*renderer_bind_api) (CoglWinsys   *winsys,
                             CoglRenderer *renderer);

  gboolean (*context_init) (CoglWinsys   *winsys,
                            CoglContext  *context,
                            GError      **error);

  void (*update_sync) (CoglWinsys  *winsys,
                       CoglContext *ctx);

  int (*get_sync_fd) (CoglWinsys  *winsys,
                      CoglContext *ctx);
};
