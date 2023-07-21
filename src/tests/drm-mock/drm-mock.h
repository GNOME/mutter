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

#pragma once

#include <glib.h>
#include <xf86drmMode.h>

#define DRM_MOCK_EXPORT __attribute__((visibility("default"))) extern

typedef enum _DrmMockCall
{
  DRM_MOCK_CALL_ATOMIC_COMMIT,
  DRM_MOCK_CALL_PAGE_FLIP,
  DRM_MOCK_CALL_SET_CRTC,

  DRM_MOCK_N_CALLS
} DrmMockCall;

typedef enum _DrmMockCallFilter
{
  DRM_MOCK_CALL_FILTER_GET_CONNECTOR,

  DRM_MOCK_N_CALL_FILTERS
} DrmMockCallFilter;

typedef void (* DrmMockResourceFilterFunc) (gpointer resource,
                                            gpointer user_data);

DRM_MOCK_EXPORT
void drm_mock_queue_error (DrmMockCall call,
                           int         error_number);

DRM_MOCK_EXPORT
void drm_mock_set_resource_filter (DrmMockCallFilter         call_filter,
                                   DrmMockResourceFilterFunc filter_func,
                                   gpointer                  user_data);

DRM_MOCK_EXPORT
void drm_mock_unset_resource_filter (DrmMockCallFilter call_filter);
