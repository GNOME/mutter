/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013 Red Hat, Inc.
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
 * Author: Giovanni Campagna <gcampagn@redhat.com>
 */

#ifndef META_CURSOR_PRIVATE_H
#define META_CURSOR_PRIVATE_H

#include "meta-cursor.h"

#include <cogl/cogl.h>
#include <gbm.h>

#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xcursor/Xcursor.h>
#include <wayland-server.h>

typedef struct {
  CoglTexture2D *texture;
  struct gbm_bo *bo;
  int hot_x, hot_y;
} MetaCursorImage;

struct _MetaCursorReference {
  int ref_count;

  MetaCursorImage image;
};

CoglTexture *meta_cursor_reference_get_cogl_texture (MetaCursorReference *cursor,
                                                     int                 *hot_x,
                                                     int                 *hot_y);

struct gbm_bo *meta_cursor_reference_get_gbm_bo (MetaCursorReference *cursor,
                                                 int                 *hot_x,
                                                 int                 *hot_y);

void meta_cursor_reference_load_gbm_buffer (MetaCursorReference *cursor,
                                            struct gbm_device   *gbm,
                                            uint8_t             *pixels,
                                            int                  width,
                                            int                  height,
                                            int                  rowstride,
                                            uint32_t             gbm_format);

void meta_cursor_reference_import_gbm_buffer (MetaCursorReference *cursor,
                                              struct gbm_device   *gbm,
                                              struct wl_resource  *buffer,
                                              int                  width,
                                              int                  height);

MetaCursorReference *meta_cursor_reference_from_xfixes_cursor_image (XFixesCursorImage *cursor_image);

MetaCursorReference *meta_cursor_reference_from_xcursor_image (XcursorImage *xc_image);

MetaCursorReference *meta_cursor_reference_from_buffer (struct wl_resource *buffer,
                                                        int                 hot_x,
                                                        int                 hot_y);

#endif /* META_CURSOR_PRIVATE_H */
