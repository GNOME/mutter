/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
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

#include <gmodule.h>

#include "cogl/cogl-driver.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-closure-list-private.h"
#include "cogl/winsys/cogl-winsys-private.h"

typedef const CoglWinsysVtable *(*CoglCustomWinsysVtableGetter) (CoglRenderer *renderer);

struct _CoglRenderer
{
  GObject parent_instance;
  gboolean connected;
  CoglDriver driver_override;
  const CoglDriverVtable *driver_vtable;
  const CoglTextureDriver *texture_driver;
  const CoglWinsysVtable *winsys_vtable;
  void *custom_winsys_user_data;
  CoglCustomWinsysVtableGetter custom_winsys_vtable_getter;
  CoglWinsysID winsys_id_override;
  GList *constraints;

  GArray *poll_fds;
  int poll_fds_age;
  GList *poll_sources;

  CoglList idle_closures;

  GList *outputs;

#ifdef HAVE_X11
  Display *foreign_xdpy;
  gboolean xlib_enable_event_retrieval;
  gboolean xlib_want_reset_on_video_memory_purge;
#endif

  CoglDriver driver;
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)];
  GModule *libgl_module;

  /* List of callback functions that will be given every native event */
  GSList *event_filters;
  void *winsys;
};

typedef CoglFilterReturn (* CoglNativeFilterFunc) (void *native_event,
                                                   void *data);

CoglFilterReturn
_cogl_renderer_handle_native_event (CoglRenderer *renderer,
                                    void *event);

void
_cogl_renderer_add_native_filter (CoglRenderer *renderer,
                                  CoglNativeFilterFunc func,
                                  void *data);

void
_cogl_renderer_remove_native_filter (CoglRenderer *renderer,
                                     CoglNativeFilterFunc func,
                                     void *data);

void *
_cogl_renderer_get_proc_address (CoglRenderer *renderer,
                                 const char *name,
                                 gboolean in_core);
