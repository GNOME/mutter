/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#pragma once

#include <gbm.h>

#include "cogl/cogl.h"
#include "mtk/mtk.h"

gboolean meta_renderer_native_gles3_blit_shared_bo (CoglDriver       *driver,
                                                    CoglRendererEGL  *renderer_egl,
                                                    EGLContext        egl_context,
                                                    EGLImageKHR       dst_egl_image,
                                                    EGLImageKHR       src_egl_image,
                                                    struct gbm_bo    *shared_bo,
                                                    const MtkRegion  *region,
                                                    GError          **error);

void meta_renderer_native_gles3_forget_context (CoglDriver *driver,
                                                EGLContext  egl_context);
