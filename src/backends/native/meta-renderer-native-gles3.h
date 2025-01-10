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

#include "backends/meta-egl.h"
#include "backends/meta-gles3.h"

gboolean meta_renderer_native_gles3_blit_shared_bo (MetaEgl        *egl,
                                                    MetaGles3      *gles3,
                                                    EGLDisplay      egl_display,
                                                    EGLContext      egl_context,
                                                    EGLImageKHR     egl_image,
                                                    struct gbm_bo  *shared_bo,
                                                    GError        **error);

void meta_renderer_native_gles3_forget_context (MetaGles3  *gles3,
                                                EGLContext  egl_context);
