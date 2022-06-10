/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "drm-mock.h"

#include <dlfcn.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <xf86drmMode.h>

static GList *queued_errors[DRM_MOCK_N_CALLS];

static int
maybe_mock_error (DrmMockCall call)
{
  if (queued_errors[call])
    {
      int error_number =
        GPOINTER_TO_INT (queued_errors[call]->data);

      queued_errors[call] = g_list_remove_link (queued_errors[call],
                                                queued_errors[call]);

      errno = error_number;
      return -error_number;
    }

  return 0;
}

#define MOCK_FUNCTION(FunctionName, CALL_TYPE, args_type, args) \
\
DRM_MOCK_EXPORT int \
FunctionName args_type \
{ \
  static int (* real_function) args_type; \
  int ret; \
  if (G_UNLIKELY (!real_function)) \
    real_function = dlsym (RTLD_NEXT, #FunctionName); \
\
  ret = maybe_mock_error (CALL_TYPE); \
  if (ret != 0) \
    return ret; \
\
  return real_function args; \
}

MOCK_FUNCTION (drmModeAtomicCommit,
               DRM_MOCK_CALL_ATOMIC_COMMIT,
               (int                  fd,
                drmModeAtomicReqPtr  req,
                uint32_t             flags,
                void                *user_data),
               (fd, req, flags, user_data))

MOCK_FUNCTION (drmModePageFlip,
               DRM_MOCK_CALL_PAGE_FLIP,
               (int       fd,
                uint32_t  crtc_id,
                uint32_t  fb_id,
                uint32_t  flags,
                void     *user_data),
               (fd, crtc_id, fb_id, flags, user_data))

MOCK_FUNCTION (drmModeSetCrtc,
               DRM_MOCK_CALL_SET_CRTC,
               (int                 fd,
                uint32_t            crtc_id,
                uint32_t            fb_id,
                uint32_t            x,
                uint32_t            y,
                uint32_t           *connectors,
                int                 count,
                drmModeModeInfoPtr  mode),
               (fd, crtc_id, fb_id, x, y, connectors, count, mode))

void
drm_mock_queue_error (DrmMockCall call,
                      int         error_number)
{
  g_return_if_fail (call < DRM_MOCK_N_CALLS);

  queued_errors[call] = g_list_append (queued_errors[call],
                                       GINT_TO_POINTER (error_number));
}
