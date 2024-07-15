/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 * Authors:
 *  Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-poll.h"
#include "cogl/cogl-poll-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/winsys/cogl-winsys-private.h"

gboolean
cogl_poll_renderer_has_idle_closures (CoglRenderer *renderer)
{
  g_return_val_if_fail (COGL_IS_RENDERER (renderer), FALSE);

  return !_cogl_list_empty (&renderer->idle_closures);
}

void
cogl_poll_renderer_dispatch (CoglRenderer *renderer)
{
  g_return_if_fail (COGL_IS_RENDERER (renderer));

  _cogl_closure_list_invoke_no_args (&renderer->idle_closures);
}

CoglClosure *
_cogl_poll_renderer_add_idle (CoglRenderer *renderer,
                              CoglIdleCallback idle_cb,
                              void *user_data,
                              GDestroyNotify destroy_cb)
{
  return _cogl_closure_list_add (&renderer->idle_closures,
                                idle_cb,
                                user_data,
                                destroy_cb);
}
